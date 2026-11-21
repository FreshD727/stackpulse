#include <algorithm>
#include <array>
#include <enet/enet.h>
#include <limits>
#include <map>
#include <stackpulse/protocol.hpp>

namespace stackpulse::net {
namespace {
using namespace wire;
struct Slot {
    ENetPeer* peer{};
    std::array<std::uint8_t, 32> credential{};
    Admission input;
    bool occupied{}, ready{};
};
struct Connection {
    std::uint64_t session{}, opened{}, window{};
    std::uint32_t snapshot{}, next_transfer{1}, inputs{}, controls{}, bytes{}, violations{};
    std::uint64_t sent_at{};
    std::uint16_t player{};
    bool hello{}, recovering{}, result_sent{}, closing{};
};
void send(ENetPeer* peer, const Packet& p, enet_uint8 channel = 0, bool reliable = true) {
    auto bytes = encode(p);
    if (bytes.empty())
        return;
    auto* packet =
        enet_packet_create(bytes.data(), bytes.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    if (!packet)
        return;
    if (enet_peer_send(peer, channel, packet) != 0)
        enet_packet_destroy(packet);
}
} // namespace
struct Server::Impl {
    ServerOptions options;
    ENetHost* host{};
    View view;
    std::string failure;
    std::uint64_t room_tick{}, match_id{};
    std::array<Slot, 2> slots;
    std::map<ENetPeer*, Connection> connections;
    explicit Impl(const ServerOptions& o) : options(o) {
        view.phase = Phase::waiting;
        if (options.initial_state) {
            const auto& fixture = *options.initial_state;
            if (!match::valid_state(fixture) || fixture.tick ||
                fixture.result != match::Result::playing || fixture.players.size() != 2 ||
                fixture.players[0].id != 1 || fixture.players[1].id != 2 ||
                std::any_of(fixture.players.begin(), fixture.players.end(), [&](const auto& p) {
                    return p.board.preset != options.preset || p.disconnected;
                })) {
                failure = "Invalid constructor duel fixture";
                return;
            }
        }
        if (!initialize()) {
            failure = "ENet initialization failed";
            return;
        }
        match_id = random_id();
        if (!match_id) {
            failure = "Secure random source unavailable";
            return;
        }
        ENetAddress address{};
        address.port = options.port;
        if (enet_address_set_host_ip(&address, options.bind.c_str()) != 0) {
            failure = "Invalid bind address";
            return;
        }
        host = enet_host_create(&address, 8, 2, 0, 0);
        if (!host)
            failure = "Cannot bind UDP duel server";
    }
    ~Impl() {
        if (host)
            enet_host_destroy(host);
    }
    void reject(ENetPeer* peer, const char* detail) {
        Writer w;
        for (const char* p = detail; *p && w.bytes.size() < 128; ++p)
            w.put(static_cast<unsigned char>(*p), 1);
        auto it = connections.find(peer);
        send(peer,
             {Tag::reject, it == connections.end() ? 0 : it->second.session, std::move(w.bytes)});
        enet_peer_disconnect_later(peer, 1);
        if (it != connections.end()) {
            auto& c = it->second;
            c.closing = true;
            if (c.player) {
                const auto id = c.player;
                auto& slot = slots[id - 1];
                if (slot.peer == peer) {
                    slot.peer = nullptr;
                    slot.ready = false;
                    slot.input.clear();
                    if (view.phase == Phase::playing)
                        match::disconnect(view.authoritative, id);
                    else if (view.phase == Phase::waiting || view.phase == Phase::countdown) {
                        slot.occupied = false;
                        slot.credential = {};
                        view.phase = Phase::waiting;
                        view.countdown = 0;
                    }
                }
                c.player = 0;
                lobby();
            }
        }
    }
    void welcome(ENetPeer* peer, Connection& c) {
        Writer w;
        w.put(c.player, 2);
        w.put(match_id, 8);
        std::array<std::uint8_t, 32> empty{};
        w.data(c.player ? slots[c.player - 1].credential : empty);
        send(peer, {Tag::welcome, c.session, std::move(w.bytes)});
    }
    void lobby() {
        for (auto& [peer, c] : connections) {
            if (!c.hello || !c.player)
                continue;
            Writer w;
            w.put(static_cast<unsigned>(view.phase), 1);
            w.put(view.countdown, 4);
            w.put(view.has_snapshot ? view.authoritative.tick : 0, 8);
            w.put(slots[c.player - 1].ready, 1);
            w.put(slots[2 - c.player].ready, 1);
            send(peer, {Tag::lobby, c.session, std::move(w.bytes)});
        }
    }
    void drop(ENetPeer* peer) {
        auto it = connections.find(peer);
        if (it == connections.end())
            return;
        auto id = it->second.player;
        if (id && slots[id - 1].peer == peer) {
            auto& slot = slots[id - 1];
            slot.peer = nullptr;
            slot.ready = false;
            slot.input.clear();
            if (view.phase == Phase::playing)
                match::disconnect(view.authoritative, id);
            else if (view.phase == Phase::waiting || view.phase == Phase::countdown) {
                slot.occupied = false;
                slot.credential = {};
                view.phase = Phase::waiting;
                view.countdown = 0;
            }
        }
        connections.erase(it);
        lobby();
    }
    void snapshot(ENetPeer* peer, Connection& c) {
        if (!c.player || !view.has_snapshot || c.snapshot ||
            (view.phase == Phase::results && c.result_sent))
            return;
        Writer w;
        w.put(match_id, 8);
        w.put(modern::rules_version, 4);
        w.put(static_cast<unsigned>(options.preset), 1);
        w.put(match::state_hash(view.authoritative), 8);
        w.data(match::canonical_bytes(view.authoritative));
        auto id = c.next_transfer++;
        if (!id)
            id = c.next_transfer++;
        auto parts = chunks(c.session, id, w.bytes);
        if (parts.empty()) {
            reject(peer, "Snapshot exceeds supported duel bound");
            return;
        }
        for (auto& p : parts)
            send(peer, p);
        c.snapshot = id;
        c.sent_at = room_tick;
        if (view.phase == Phase::results) {
            Writer result;
            result.put(match_id, 8);
            result.put(view.authoritative.tick, 8);
            result.put(match::state_hash(view.authoritative), 8);
            send(peer, {Tag::result, c.session, std::move(result.bytes)});
            c.result_sent = true;
        }
    }
    void receive(ENetPeer* peer, const ENetPacket* data, enet_uint8 channel) {
        auto it = connections.find(peer);
        if (it == connections.end())
            return;
        auto& c = it->second;
        if (c.closing)
            return;
        if (room_tick - c.window >= 60) {
            c.window = room_tick;
            c.inputs = c.controls = c.bytes = 0;
            c.violations = 0;
        }
        c.bytes += static_cast<std::uint32_t>(data->dataLength);
        Packet p;
        if (!decode({data->data, data->dataLength}, p)) {
            reject(peer, "Malformed packet");
            return;
        }
        if ((p.tag == Tag::input ? channel != 1 : channel != 0)) {
            reject(peer, "Wrong channel");
            return;
        }
        const bool quota =
            c.bytes > 256 * 1024 || (p.tag == Tag::input ? ++c.inputs > 120 : ++c.controls > 80);
        if (quota) {
            if (++c.violations >= 3) {
                enet_peer_disconnect_now(peer, 2);
                drop(peer);
            }
            return;
        }
        Reader r{p.body};
        if (p.tag == Tag::hello) {
            auto version = r.get(4), preset = r.get(1), recovery = r.get(1);
            if (c.hello || p.session || !r.done() || version != modern::rules_version ||
                preset != static_cast<unsigned>(options.preset) || recovery > 1) {
                reject(peer, "Incompatible rules or handshake");
                return;
            }
            c.session = random_id();
            if (!c.session) {
                reject(peer, "Secure random unavailable");
                return;
            }
            c.hello = true;
            c.recovering = recovery != 0;
            if (!c.recovering) {
                if (view.phase != Phase::waiting) {
                    reject(peer, "Duel already started");
                    return;
                }
                for (std::size_t i = 0; i < slots.size(); ++i)
                    if (!slots[i].occupied) {
                        auto& slot = slots[i];
                        if (!random_bytes(slot.credential)) {
                            reject(peer, "Secure random unavailable");
                            return;
                        }
                        slot.occupied = true;
                        slot.peer = peer;
                        c.player = static_cast<std::uint16_t>(i + 1);
                        break;
                    }
                if (!c.player) {
                    reject(peer, "Duel full");
                    return;
                }
            }
            welcome(peer, c);
            lobby();
            return;
        }
        if (!c.hello || !p.session || p.session != c.session)
            return;
        if (p.tag == Tag::reconnect) {
            auto match = r.get(8), id = r.get(2);
            auto token = r.data(32);
            if (!r.done() || !c.recovering || c.player || match != match_id || id < 1 || id > 2 ||
                view.phase != Phase::playing) {
                reject(peer, "Reconnect unavailable");
                return;
            }
            auto& slot = slots[id - 1];
            auto& board = view.authoritative.players[id - 1];
            // Compare every credential byte; credentials never enter snapshots or logs.
            unsigned difference = 0;
            for (std::size_t i = 0; i < 32; ++i)
                difference |= slot.credential[i] ^ token[i];
            if (!slot.occupied || difference || board.placement ||
                (board.disconnected && board.deadline <= view.authoritative.tick)) {
                reject(peer, "Reconnect credential expired or invalid");
                return;
            }
            std::array<std::uint8_t, 32> replacement{};
            if (!random_bytes(replacement)) {
                reject(peer, "Secure random unavailable");
                return;
            }
            if (slot.peer && slot.peer != peer) {
                auto* old_peer = slot.peer;
                enet_peer_disconnect_now(old_peer, 0);
                drop(old_peer);
            }
            if (!match::reconnect(view.authoritative, static_cast<std::uint16_t>(id))) {
                reject(peer, "Reconnect state unavailable");
                return;
            }
            slot.credential = replacement;
            c.player = static_cast<std::uint16_t>(id);
            c.recovering = false;
            slot.peer = peer;
            slot.input.clear();
            welcome(peer, c);
            snapshot(peer, c);
            lobby();
            return;
        }
        if (!c.player)
            return;
        if (p.tag == Tag::ready) {
            auto ready = r.get(1);
            if (!r.done() || ready > 1) {
                reject(peer, "Invalid ready");
                return;
            }
            if (view.phase == Phase::waiting) {
                slots[c.player - 1].ready = ready != 0;
                lobby();
            }
        } else if (p.tag == Tag::input) {
            auto match = r.get(8), count = r.get(1);
            if (match != match_id || count < 1 || count > 8 || p.body.size() != 9 + count * 16) {
                reject(peer, "Invalid input batch");
                return;
            }
            std::vector<Input> inputs;
            for (std::uint64_t i = 0; i < count; ++i)
                inputs.push_back({r.get(8),
                                  static_cast<std::uint32_t>(r.get(4)),
                                  static_cast<std::uint32_t>(r.get(4))});
            if (!r.done()) {
                reject(peer, "Truncated input");
                return;
            }
            if (view.phase == Phase::playing)
                for (auto in : inputs)
                    slots[c.player - 1].input.accept(in, view.authoritative.tick);
        } else if (p.tag == Tag::snapshot_ack) {
            auto id = r.get(4);
            if (!r.done()) {
                reject(peer, "Invalid snapshot ack");
                return;
            }
            if (id == c.snapshot)
                c.snapshot = 0;
        } else {
            reject(peer, "Unexpected message");
        }
    }
    void tick() {
        if (!host)
            return;
        ENetEvent event{};
        unsigned count = 0;
        while (count++ < 256 && enet_host_service(host, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                Connection c;
                c.opened = c.window = room_tick;
                connections.emplace(event.peer, c);
                enet_peer_timeout(event.peer, 32, 5000, 10000);
            } else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
                drop(event.peer);
            else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                receive(event.peer, event.packet, event.channelID);
                enet_packet_destroy(event.packet);
            }
        }
        ++room_tick;
        std::vector<ENetPeer*> expired;
        for (auto& [peer, c] : connections)
            if (((!c.hello || !c.player) && room_tick - c.opened > 300) ||
                (c.snapshot && room_tick - c.sent_at > 300))
                expired.push_back(peer);
        for (auto* peer : expired) {
            enet_peer_disconnect_now(peer, 3);
            drop(peer);
        }
        if (view.phase == Phase::waiting && slots[0].occupied && slots[1].occupied &&
            slots[0].ready && slots[1].ready) {
            view.phase = Phase::countdown;
            view.countdown = 180;
            lobby();
        } else if (view.phase == Phase::countdown) {
            if (view.countdown)
                --view.countdown;
            if (!view.countdown) {
                view.authoritative =
                    options.initial_state
                        ? *options.initial_state
                        : match::initial_state({1, 2}, options.seed, options.preset);
                view.has_snapshot = true;
                view.phase = Phase::playing;
                lobby();
            } else if (view.countdown % 30 == 0)
                lobby();
        } else if (view.phase == Phase::playing) {
            std::vector<std::uint32_t> held;
            for (auto& slot : slots)
                held.push_back(slot.peer ? slot.input.consume(view.authoritative.tick) : 0);
            std::string error;
            if (!match::advance(view.authoritative, held, nullptr, error)) {
                failure = error;
                view.phase = Phase::error;
            } else if (view.authoritative.result != match::Result::playing) {
                view.phase = Phase::results;
                for (auto& slot : slots)
                    slot.credential = {};
            }
        }
        if (view.has_snapshot) {
            view.server_tick = view.accepted_through = view.authoritative.tick;
            view.snapshot_revision++;
        }
        if (view.has_snapshot && (view.authoritative.tick % 3 == 0 || view.phase == Phase::results))
            for (auto& [peer, c] : connections)
                snapshot(peer, c);
        enet_host_flush(host);
    }
};
Server::Server(const ServerOptions& o) : impl_(std::make_unique<Impl>(o)) {}
Server::~Server() = default;
bool Server::valid() const {
    return impl_->host && impl_->failure.empty();
}
const std::string& Server::error() const {
    return impl_->failure;
}
void Server::tick() {
    impl_->tick();
}
const View& Server::view() const {
    return impl_->view;
}
} // namespace stackpulse::net
