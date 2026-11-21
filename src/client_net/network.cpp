#include <algorithm>
#include <array>
#include <chrono>
#include <enet/enet.h>
#include <stackpulse/protocol.hpp>
namespace stackpulse::net {
namespace {
using namespace wire;
void send(ENetPeer* peer, const Packet& p, enet_uint8 channel = 0, bool reliable = true) {
    if (!peer)
        return;
    auto b = encode(p);
    if (b.empty())
        return;
    auto* packet = enet_packet_create(b.data(), b.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet)
        return;
    if (enet_peer_send(peer, channel, packet) != 0)
        enet_packet_destroy(packet);
}
std::uint64_t clock_tick() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count()) *
           60 / 1000;
}
} // namespace
struct Client::Impl {
    ENetHost* socket{};
    ENetPeer* peer{};
    View view;
    std::string hostname;
    std::uint16_t port{};
    modern::Preset preset{};
    std::uint64_t session{}, match_id{}, revision{}, opened{};
    std::array<std::uint8_t, 32> credential{};
    std::uint16_t old_player{};
    bool recovering{}, result_seen{};
    Assembly assembly;
    ~Impl() {
        if (socket)
            enet_host_destroy(socket);
    }
    bool open(const std::string& host, std::uint16_t p, modern::Preset rules, bool recovery) {
        if (!initialize()) {
            view.phase = Phase::error;
            view.detail = "ENet initialization failed";
            return false;
        }
        if (socket && peer) {
            enet_peer_disconnect(peer, 0);
            enet_host_flush(socket);
        }
        if (socket)
            enet_host_destroy(socket);
        socket = nullptr;
        peer = nullptr;
        session = 0;
        assembly.reset();
        hostname = host;
        port = p;
        preset = rules;
        recovering = recovery;
        result_seen = false;
        if (!recovery)
            view = View{};
        view.phase = Phase::connecting;
        view.connected = false;
        view.ready = false;
        view.detail = recovery ? "Reconnecting" : "Connecting";
        socket = enet_host_create(nullptr, 1, 2, 0, 0);
        ENetAddress addr{};
        addr.port = port;
        if (!socket || enet_address_set_host(&addr, hostname.c_str()) != 0 ||
            !(peer = enet_host_connect(socket, &addr, 2, 0))) {
            view.phase = Phase::error;
            view.detail = "Cannot resolve or connect to server";
            return false;
        }
        enet_peer_timeout(peer, 32, 5000, 10000);
        opened = clock_tick();
        return true;
    }
    void fail(const char* detail) {
        view.phase = Phase::error;
        view.detail = detail;
        view.connected = false;
        if (peer) {
            enet_peer_reset(peer);
            peer = nullptr;
        }
    }
    void receive(const ENetPacket* data, enet_uint8 channel) {
        Packet p;
        if (channel != 0 || !decode({data->data, data->dataLength}, p)) {
            fail("Malformed server packet");
            return;
        }
        Reader r{p.body};
        if (p.tag == Tag::reject) {
            if (p.body.size() > 128)
                return;
            if (session && p.session != session)
                return;
            view.detail.assign(p.body.begin(), p.body.end());
            view.phase = Phase::error;
            view.connected = false;
            if (peer) {
                enet_peer_reset(peer);
                peer = nullptr;
            }
            return;
        }
        if (p.tag == Tag::welcome) {
            const auto id = r.get(2), match = r.get(8);
            auto token = r.data(32);
            if (!r.done() || !p.session || id > 2 || !match || (session && p.session != session)) {
                fail("Invalid welcome");
                return;
            }
            session = p.session;
            if (recovering && !id) {
                Writer w;
                w.put(match_id, 8);
                w.put(old_player, 2);
                w.data(credential);
                send(peer, {Tag::reconnect, session, std::move(w.bytes)});
                return;
            }
            if (!id) {
                fail("Missing player identity");
                return;
            }
            view.player = static_cast<std::uint16_t>(id);
            old_player = view.player;
            match_id = match;
            std::copy(token.begin(), token.end(), credential.begin());
            recovering = false;
            view.connected = true;
            view.phase = Phase::waiting;
            view.detail = "Connected";
            return;
        }
        if (!session || p.session != session || !view.connected)
            return;
        if (p.tag == Tag::lobby) {
            const auto phase = r.get(1), countdown = r.get(4), tick = r.get(8), ready = r.get(1),
                       opponent = r.get(1);
            if (!r.done() || phase > static_cast<unsigned>(Phase::results) || ready > 1 ||
                opponent > 1 || countdown > 180) {
                fail("Invalid lobby");
                return;
            }
            view.countdown = static_cast<std::uint32_t>(countdown);
            view.server_tick = std::max(view.server_tick, tick);
            view.ready = ready != 0;
            view.opponent_ready = opponent != 0;
            if (phase != static_cast<unsigned>(Phase::results) || result_seen)
                view.phase = static_cast<Phase>(phase);
        } else if (p.tag == Tag::chunk) {
            const auto transfer_bytes = p.body;
            Reader id_reader{transfer_bytes};
            auto transfer = id_reader.get(4);
            std::vector<std::uint8_t> object;
            if (!assembly.add(r, clock_tick(), object)) {
                fail("Invalid snapshot chunks");
                return;
            }
            if (object.empty())
                return;
            Reader full{object};
            const auto match = full.get(8), version = full.get(4), rules = full.get(1),
                       hash = full.get(8);
            auto bytes = full.data(full.bytes.size() - full.offset);
            match::State state;
            if (!full.done() || match != match_id || version != modern::rules_version ||
                rules != static_cast<unsigned>(preset) || !match::decode_state(bytes, state) ||
                state.players.size() != 2 || state.players[0].id != 1 || state.players[1].id != 2 ||
                match::state_hash(state) != hash ||
                std::any_of(state.players.begin(), state.players.end(), [&](const auto& b) {
                    return b.board.preset != preset;
                })) {
                fail("Invalid authoritative snapshot");
                return;
            }
            if (!view.has_snapshot || state.tick >= view.authoritative.tick) {
                view.authoritative = std::move(state);
                view.has_snapshot = true;
                view.server_tick = view.accepted_through = view.authoritative.tick;
                view.snapshot_revision = ++revision;
                if (!result_seen)
                    view.phase = Phase::playing;
            }
            Writer ack;
            ack.put(transfer, 4);
            send(peer, {Tag::snapshot_ack, session, std::move(ack.bytes)});
        } else if (p.tag == Tag::result) {
            auto match = r.get(8), tick = r.get(8), hash = r.get(8);
            if (!r.done() || match != match_id || !view.has_snapshot ||
                tick != view.authoritative.tick || hash != match::state_hash(view.authoritative) ||
                view.authoritative.result == match::Result::playing) {
                fail("Result does not match authority");
                return;
            }
            if (!result_seen) {
                result_seen = true;
                view.phase = Phase::results;
                view.detail = "Authoritative result";
                credential = {};
            }
        } else
            fail("Unexpected server message");
    }
    void poll() {
        if (!socket)
            return;
        ENetEvent event{};
        unsigned count = 0;
        while (count++ < 256 && enet_host_service(socket, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                Writer w;
                w.put(modern::rules_version, 4);
                w.put(static_cast<unsigned>(preset), 1);
                w.put(recovering, 1);
                send(peer, {Tag::hello, 0, std::move(w.bytes)});
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                if (view.phase != Phase::error)
                    receive(event.packet, event.channelID);
                enet_packet_destroy(event.packet);
            } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
                view.connected = false;
                if (view.phase != Phase::error) {
                    view.phase = Phase::disconnected;
                    view.detail = "Disconnected - reconnect within 10 seconds";
                }
                peer = nullptr;
            }
        }
        if (view.phase == Phase::connecting && clock_tick() - opened > 300)
            fail("Connection timed out");
        if (peer)
            view.round_trip_ms = peer->roundTripTime;
        enet_host_flush(socket);
    }
};
Client::Client() : impl_(std::make_unique<Impl>()) {}
Client::~Client() = default;
bool Client::connect(const std::string& host, std::uint16_t port, modern::Preset preset) {
    impl_->view = View{};
    impl_->credential = {};
    impl_->match_id = 0;
    impl_->old_player = 0;
    return impl_->open(host, port, preset, false);
}
void Client::poll() {
    impl_->poll();
}
void Client::set_ready(bool ready) {
    if (!impl_->view.connected)
        return;
    Writer w;
    w.put(ready, 1);
    send(impl_->peer, {Tag::ready, impl_->session, std::move(w.bytes)});
    if (impl_->socket)
        enet_host_flush(impl_->socket);
}
void Client::submit(const std::vector<Input>& frames) {
    if (!impl_->view.connected || impl_->view.phase != Phase::playing || frames.empty() ||
        frames.size() > 8)
        return;
    Writer w;
    w.put(impl_->match_id, 8);
    w.put(frames.size(), 1);
    for (auto in : frames) {
        w.put(in.tick, 8);
        w.put(in.sequence, 4);
        w.put(in.held, 4);
    }
    send(impl_->peer, {Tag::input, impl_->session, std::move(w.bytes)}, 1, false);
    if (impl_->socket)
        enet_host_flush(impl_->socket);
}
void Client::disconnect() {
    if (impl_->peer) {
        enet_peer_disconnect(impl_->peer, 0);
        if (impl_->socket)
            enet_host_flush(impl_->socket);
    }
    impl_->view.connected = false;
    impl_->view.phase = Phase::disconnected;
    impl_->view.detail = "Disconnected - reconnect within 10 seconds";
}
void Client::reconnect() {
    const auto host = impl_->hostname;
    const auto port = impl_->port;
    const auto preset = impl_->preset;
    if (!impl_->old_player || !impl_->match_id) {
        impl_->fail("No reconnect credential");
        return;
    }
    impl_->open(host, port, preset, true);
}
const View& Client::view() const {
    return impl_->view;
}
} // namespace stackpulse::net
