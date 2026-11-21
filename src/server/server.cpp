#include <algorithm>
#include <array>
#include <enet/enet.h>
#include <limits>
#include <map>
#include <stackpulse/protocol.hpp>
#include <stackpulse/seeds.hpp>

namespace stackpulse::net {
namespace {
using namespace wire;
struct Slot {
    ENetPeer* peer{};
    std::array<std::uint8_t, 32> credential{};
    Admission input;
    bool occupied{}, ready{};
    std::uint16_t id{};
    Role role{Role::player};
    std::string name;
};
struct Connection {
    std::uint64_t session{}, opened{}, window{};
    std::uint32_t snapshot{}, next_transfer{1}, inputs{}, controls{}, bytes{}, violations{};
    std::uint64_t sent_at{};
    std::uint16_t player{};
    bool hello{}, recovering{}, result_sent{}, closing{};
    std::uint64_t room{};
    std::string name{"Player"};
    Role role{Role::player};
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
    struct Room {
        View view;
        std::map<std::uint16_t, Slot> slots;
        std::uint16_t next_member{1};
        std::uint32_t last_seed{};
        std::string password;
    };
    ServerOptions options;
    ENetHost* host{};
    View view;
    std::string failure;
    std::uint64_t room_tick{}, default_room{};
    ServerMetrics metrics;
    enet_uint32 previous_sent{}, previous_received{};
    std::map<std::uint64_t, Room> rooms;
    std::map<ENetPeer*, Connection> connections;
    explicit Impl(const ServerOptions& o) : options(o) {
        view.phase = Phase::waiting;
        if (o.players < 2 || o.players > 8 || o.spectators > 16 || !o.rooms || o.rooms > 8) {
            failure = "Invalid room capacity";
            return;
        }
        if (o.initial_state &&
            (!match::valid_state(*o.initial_state) || o.initial_state->tick ||
             o.initial_state->result != match::Result::playing ||
             o.initial_state->players.size() != 2 || o.initial_state->players[0].id != 1 ||
             o.initial_state->players[1].id != 2 ||
             std::any_of(o.initial_state->players.begin(),
                         o.initial_state->players.end(),
                         [&](auto& p) { return p.board.preset != o.preset || p.disconnected; }))) {
            failure = "Invalid constructor duel fixture";
            return;
        }
        if (!initialize()) {
            failure = "ENet initialization failed";
            return;
        }
        ENetAddress address{};
        address.port = o.port;
        if (enet_address_set_host_ip(&address, o.bind.c_str())) {
            failure = "Invalid bind address";
            return;
        }
        host = enet_host_create(
            &address, static_cast<std::size_t>(o.rooms) * (o.players + o.spectators) + 16, 2, 0, 0);
        if (!host)
            failure = "Cannot bind UDP room server";
        else {
            auto* r = make_room();
            if (!r)
                failure = "Secure random source unavailable";
            else
                default_room = r->view.room_id;
        }
    }
    ~Impl() {
        if (host)
            enet_host_destroy(host);
    }
    Room* make_room(const RoomOptions* settings = nullptr) {
        if (rooms.size() >= options.rooms)
            return nullptr;
        const auto id = random_id();
        if (!id || rooms.contains(id))
            return nullptr;
        Room r;
        r.view.phase = Phase::waiting;
        r.view.room_id = id;
        r.view.match_id = random_id();
        r.view.room.players = options.players;
        r.view.room.preset = options.preset;
        r.view.room.name = "Default lobby";
        if (settings) {
            r.view.room = *settings;
            r.password = settings->password;
            r.view.room.password.clear();
        }
        r.view.player_limit = r.view.room.players;
        r.view.spectator_limit = options.spectators;
        static constexpr char hex[] = "0123456789ABCDEF";
        for (unsigned i = 0; i < 16; ++i)
            r.view.room_code += hex[(id >> (i * 4)) & 15];
        return &rooms.emplace(id, std::move(r)).first->second;
    }
    Room* room(Connection& c) {
        auto i = rooms.find(c.room);
        return i == rooms.end() ? nullptr : &i->second;
    }
    void refresh(Room& r) {
        r.view.members.clear();
        r.view.manager_id = 0;
        for (auto& [id, s] : r.slots) {
            r.view.members.push_back({id,
                                      s.role == Role::player ? id : std::uint16_t{},
                                      s.name,
                                      s.role,
                                      s.ready,
                                      s.peer != nullptr});
            if (s.peer && !r.view.manager_id)
                r.view.manager_id = id;
        }
    }
    void lobby(Room& r) {
        refresh(r);
        for (auto& [peer, c] : connections)
            if (c.hello && !c.closing && c.room == r.view.room_id && c.player) {
                Writer w;
                auto& v = r.view;
                w.put(static_cast<unsigned>(v.phase), 1);
                w.put(v.countdown, 4);
                w.put(v.has_snapshot ? v.authoritative.tick : 0, 8);
                w.put(v.room_id, 8);
                w.put(v.match_id, 8);
                w.put(v.manager_id, 2);
                w.put(v.player_limit, 1);
                w.put(options.spectators, 1);
                w.put(v.room_code.size(), 1);
                w.data({reinterpret_cast<const std::uint8_t*>(v.room_code.data()),
                        v.room_code.size()});
                write_options(w, v.room, false);
                w.put(v.members.size(), 1);
                for (auto& m : v.members) {
                    w.put(m.id, 2);
                    w.put(m.player, 2);
                    w.put(static_cast<unsigned>(m.role), 1);
                    w.put(m.ready, 1);
                    w.put(m.connected, 1);
                    w.put(m.name.size(), 1);
                    w.data({reinterpret_cast<const std::uint8_t*>(m.name.data()), m.name.size()});
                }
                send(peer, {Tag::lobby, c.session, std::move(w.bytes)});
            }
    }
    void detach(ENetPeer* peer, Connection& c, bool explicit_leave = false) {
        auto* r = room(c);
        if (!r || !c.player) {
            c.room = 0;
            c.player = 0;
            return;
        }
        auto i = r->slots.find(c.player);
        if (i != r->slots.end() && i->second.peer == peer) {
            auto& s = i->second;
            const bool was_player = s.role == Role::player;
            s.peer = nullptr;
            s.ready = false;
            s.input.clear();
            if (r->view.phase == Phase::playing && s.role == Role::player) {
                match::disconnect(r->view.authoritative, s.id);
                if (explicit_leave) {
                    s.credential = {};
                    s.occupied = false;
                }
            } else
                r->slots.erase(i);
            if (was_player && r->view.phase == Phase::countdown) {
                r->view.phase = Phase::waiting;
                r->view.countdown = 0;
                for (auto& [id, s2] : r->slots)
                    s2.ready = false;
            }
        }
        c.room = 0;
        c.player = 0;
        c.snapshot = 0;
        c.result_sent = false;
        lobby(*r);
    }
    void operation_error(ENetPeer* peer, Connection& c, const char* text) {
        if (!c.room)
            welcome(peer, c);
        send(peer,
             {Tag::operation_error,
              c.session,
              std::vector<std::uint8_t>(text, text + std::char_traits<char>::length(text))});
        directory(peer, c);
    }
    void directory(ENetPeer* peer, Connection& c) {
        Writer w;
        w.put(rooms.size(), 1);
        for (auto& [id, r] : rooms) {
            refresh(r);
            w.put(id, 8);
            w.put(r.view.room_code.size(), 1);
            w.data({reinterpret_cast<const std::uint8_t*>(r.view.room_code.data()),
                    r.view.room_code.size()});
            write_options(w, r.view.room, false);
            w.put(!r.password.empty(), 1);
            unsigned players = 0, spectators = 0;
            for (auto& m : r.view.members)
                m.role == Role::player ? ++players : ++spectators;
            w.put(players, 1);
            w.put(spectators, 1);
            w.put(static_cast<unsigned>(r.view.phase), 1);
        }
        send(peer, {Tag::directory, c.session, std::move(w.bytes)});
    }
    void reject(ENetPeer* peer, const char* detail) {
        auto i = connections.find(peer);
        send(peer,
             {Tag::reject,
              i == connections.end() ? 0 : i->second.session,
              std::vector<std::uint8_t>(
                  detail,
                  detail + std::min<std::size_t>(128, std::char_traits<char>::length(detail)))});
        if (i != connections.end()) {
            detach(peer, i->second);
            i->second.closing = true;
        }
        enet_peer_disconnect_later(peer, 1);
    }
    void drop(ENetPeer* peer) {
        auto i = connections.find(peer);
        if (i == connections.end())
            return;
        detach(peer, i->second);
        connections.erase(i);
    }
    void welcome(ENetPeer* peer, Connection& c) {
        auto* r = room(c);
        Writer w;
        w.put(c.player, 2);
        w.put(r ? r->view.match_id : 1, 8);
        std::array<std::uint8_t, 32> empty{};
        w.data(r && c.player ? r->slots.at(c.player).credential : empty);
        w.put(c.room, 8);
        w.put(static_cast<unsigned>(c.role), 1);
        w.put(r ? static_cast<unsigned>(r->view.room.preset) : 0, 1);
        send(peer, {Tag::welcome, c.session, std::move(w.bytes)});
    }
    bool join(ENetPeer* peer, Connection& c, Room& r, Role requested) {
        Role role = requested;
        if (role == Role::player && r.view.phase != Phase::waiting) {
            operation_error(peer, c, "Match started; choose Watch to spectate");
            return false;
        }
        unsigned n = 0;
        for (auto& [id, s] : r.slots)
            if (s.role == role)
                ++n;
        if (n >= (role == Role::player ? r.view.player_limit : options.spectators) ||
            !r.next_member) {
            operation_error(peer, c, "Room full");
            return false;
        }
        Slot s;
        s.id = r.next_member++;
        s.role = role;
        s.name = c.name;
        s.peer = peer;
        s.occupied = true;
        if (!random_bytes(s.credential)) {
            reject(peer, "Secure random unavailable");
            return false;
        }
        detach(peer, c, true);
        c.room = r.view.room_id;
        c.player = s.id;
        c.role = role;
        c.snapshot = 0;
        c.result_sent = false;
        r.slots.emplace(s.id, std::move(s));
        welcome(peer, c);
        lobby(r);
        return true;
    }
    void snapshot(ENetPeer* peer, Connection& c) {
        auto* r = room(c);
        if (!r || !c.player || !r->view.has_snapshot || c.snapshot ||
            (r->view.phase == Phase::results && c.result_sent))
            return;
        Writer w;
        w.put(r->view.match_id, 8);
        w.put(modern::rules_version, 4);
        w.put(static_cast<unsigned>(r->view.room.preset), 1);
        w.put(match::state_hash(r->view.authoritative), 8);
        write_sounds(w, r->view.sounds);
        w.data(match::canonical_bytes(r->view.authoritative));
        auto id = c.next_transfer++;
        if (!id)
            id = c.next_transfer++;
        auto parts = chunks(c.session, id, w.bytes);
        if (parts.empty()) {
            reject(peer, "Snapshot exceeds room bound");
            return;
        }
        for (auto& p : parts)
            send(peer, p);
        c.snapshot = id;
        c.sent_at = room_tick;
        if (r->view.phase == Phase::results) {
            Writer result;
            result.put(r->view.match_id, 8);
            result.put(r->view.authoritative.tick, 8);
            result.put(match::state_hash(r->view.authoritative), 8);
            send(peer, {Tag::result, c.session, std::move(result.bytes)});
            c.result_sent = true;
        }
    }
    void receive(ENetPeer* peer, const ENetPacket* data, enet_uint8 channel) {
        auto it = connections.find(peer);
        if (it == connections.end() || it->second.closing)
            return;
        auto& c = it->second;
        if (room_tick - c.window >= 60) {
            c.window = room_tick;
            c.inputs = c.controls = c.bytes = c.violations = 0;
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
        if (c.bytes > 256 * 1024 || (p.tag == Tag::input ? ++c.inputs > 120 : ++c.controls > 80)) {
            if (++c.violations >= 3) {
                enet_peer_disconnect_now(peer, 2);
                drop(peer);
            }
            return;
        }
        Reader rd{p.body};
        if (p.tag == Tag::hello) {
            auto version = rd.get(4), preset = rd.get(1), recovery = rd.get(1);
            Role role = Role::player;
            bool create = false;
            std::string code;
            if (rd.offset < rd.bytes.size()) {
                auto v = rd.get(1), cr = rd.get(1), n = rd.get(1);
                auto name = rd.data(n);
                auto len = rd.get(1);
                auto b = rd.data(len);
                if (v > 1 || cr > 1 || !n || n > 24 || len > 16 ||
                    std::any_of(
                        name.begin(), name.end(), [](auto x) { return x < 32 || x > 126; })) {
                    reject(peer, "Invalid room handshake");
                    return;
                }
                role = static_cast<Role>(v);
                create = cr != 0;
                c.name.assign(name.begin(), name.end());
                code.assign(b.begin(), b.end());
            }
            const auto browse = rd.offset < rd.bytes.size() ? rd.get(1) : 0;
            if (c.hello || p.session || !rd.done() || version != modern::rules_version ||
                preset > 2 || recovery > 1 || browse > 1) {
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
            if (c.recovering) {
                welcome(peer, c);
                return;
            }
            if (browse) {
                welcome(peer, c);
                directory(peer, c);
                return;
            }
            Room* target = nullptr;
            if (create)
                target = make_room();
            else if (code.empty())
                target = &rooms.at(default_room);
            else
                for (auto& [id, r] : rooms)
                    if (r.view.room_code == code)
                        target = &r;
            if (!target) {
                reject(peer, "Room unavailable");
                return;
            }
            if (!target->password.empty()) {
                operation_error(
                    peer, c, "Password required; select the lobby and enter its password");
                return;
            }
            join(peer, c, *target, role);
            return;
        }
        if (!c.hello || !p.session || p.session != c.session)
            return;
        if (p.tag == Tag::reconnect) {
            auto mid = rd.get(8), id = rd.get(2);
            auto token = rd.data(32);
            Room* r = nullptr;
            for (auto& [rid, r2] : rooms)
                if (r2.view.match_id == mid)
                    r = &r2;
            if (!rd.done() || !c.recovering || c.player || !r || r->view.phase != Phase::playing) {
                reject(peer, "Reconnect unavailable");
                return;
            }
            auto i = r->slots.find(static_cast<std::uint16_t>(id));
            if (i == r->slots.end() || i->second.role != Role::player) {
                reject(peer, "Reconnect unavailable");
                return;
            }
            auto& s = i->second;
            unsigned diff = 0;
            for (unsigned j = 0; j < 32; ++j)
                diff |= s.credential[j] ^ token[j];
            auto b = std::find_if(r->view.authoritative.players.begin(),
                                  r->view.authoritative.players.end(),
                                  [&](auto& x) { return x.id == id; });
            if (!s.occupied || diff || b == r->view.authoritative.players.end() || b->placement ||
                (b->disconnected && b->deadline <= r->view.authoritative.tick)) {
                reject(peer, "Reconnect credential expired or invalid");
                return;
            }
            std::array<std::uint8_t, 32> replacement{};
            if (!random_bytes(replacement)) {
                reject(peer, "Secure random unavailable");
                return;
            }
            if (s.peer && s.peer != peer) {
                auto* old = s.peer;
                enet_peer_disconnect_now(old, 0);
                drop(old);
            }
            if (!match::reconnect(r->view.authoritative, static_cast<std::uint16_t>(id))) {
                reject(peer, "Reconnect state unavailable");
                return;
            }
            s.credential = replacement;
            s.peer = peer;
            s.input.clear();
            c.player = static_cast<std::uint16_t>(id);
            c.role = Role::player;
            c.room = r->view.room_id;
            c.recovering = false;
            welcome(peer, c);
            lobby(*r);
            snapshot(peer, c);
            return;
        }
        if (p.tag == Tag::list_rooms) {
            if (!rd.done()) {
                reject(peer, "Invalid directory request");
                return;
            }
            directory(peer, c);
            return;
        }
        if (p.tag == Tag::create_room || p.tag == Tag::join_room) {
            Role role = Role::player;
            Room* target = nullptr;
            if (p.tag == Tag::create_room) {
                RoomOptions settings;
                if (!read_options(rd, settings) || !rd.done()) {
                    reject(peer, "Invalid create");
                    return;
                }
                target = make_room(&settings);
            } else {
                auto v = rd.get(1), n = rd.get(1);
                auto b = rd.data(n);
                auto len = rd.get(1);
                auto secret = rd.data(len);
                if (!rd.done() || v > 1 || n > 16 || len > 32) {
                    reject(peer, "Invalid join");
                    return;
                }
                role = static_cast<Role>(v);
                std::string code(b.begin(), b.end()), password(secret.begin(), secret.end());
                for (auto& [id, r] : rooms)
                    if (r.view.room_code == code)
                        target = &r;
                if (target && target->password != password) {
                    operation_error(peer, c, "Incorrect password");
                    return;
                }
            }
            if (!target) {
                operation_error(peer, c, "Room unavailable");
                return;
            }
            join(peer, c, *target, role);
            return;
        }
        if (p.tag == Tag::leave_room) {
            if (!rd.done()) {
                reject(peer, "Invalid leave");
                return;
            }
            detach(peer, c, true);
            welcome(peer, c);
            return;
        }
        auto* r = room(c);
        if (!r || !c.player)
            return;
        auto& v = r->view;
        auto& s = r->slots.at(c.player);
        if (p.tag == Tag::configure_room) {
            RoomOptions settings;
            if (!read_options(rd, settings) || !rd.done()) {
                reject(peer, "Invalid room settings");
                return;
            }
            refresh(*r);
            unsigned players = 0;
            for (auto& [id, member] : r->slots)
                if (member.role == Role::player)
                    ++players;
            if (v.phase != Phase::waiting || c.player != v.manager_id ||
                players > settings.players) {
                operation_error(
                    peer, c, "Only the waiting lobby manager can change valid settings");
                return;
            }
            // Public settings deliberately omit the secret. An empty configure
            // password means retain it, so adjusting level cannot unlock a room.
            if (!settings.password.empty())
                r->password = settings.password;
            settings.password.clear();
            v.room = settings;
            v.player_limit = settings.players;
            for (auto& [id, member] : r->slots)
                member.ready = false;
            lobby(*r);
        } else if (p.tag == Tag::snapshot_ack) {
            auto id = rd.get(4);
            if (!rd.done()) {
                reject(peer, "Invalid snapshot ack");
                return;
            }
            if (id == c.snapshot)
                c.snapshot = 0;
        } else if (p.tag == Tag::ready) {
            auto ready = rd.get(1);
            if (!rd.done() || ready > 1) {
                reject(peer, "Invalid ready");
                return;
            }
            if (s.role == Role::player && v.phase == Phase::waiting) {
                s.ready = ready != 0;
                lobby(*r);
            }
        } else if (p.tag == Tag::return_lobby) {
            if (!rd.done()) {
                reject(peer, "Invalid return");
                return;
            }
            refresh(*r);
            if (v.phase == Phase::results && c.player == v.manager_id) {
                for (auto i = r->slots.begin(); i != r->slots.end();)
                    if (!i->second.peer)
                        i = r->slots.erase(i);
                    else {
                        i->second.ready = false;
                        i->second.input.clear();
                        if (!random_bytes(i->second.credential)) {
                            failure = "Secure random unavailable";
                            return;
                        }
                        ++i;
                    }
                v.phase = Phase::waiting;
                v.countdown = 0;
                v.has_snapshot = false;
                v.authoritative = {};
                v.match_id = random_id();
                for (auto& [p2, c2] : connections)
                    if (c2.room == v.room_id) {
                        c2.snapshot = 0;
                        c2.result_sent = false;
                        welcome(p2, c2);
                    }
                lobby(*r);
            }
        } else if (p.tag == Tag::featured) {
            auto id = rd.get(2);
            if (!rd.done() || id > 65535) {
                reject(peer, "Invalid featured");
                return;
            }
        } else if (p.tag == Tag::input) {
            auto mid = rd.get(8), count = rd.get(1);
            if (mid != v.match_id || count < 1 || count > 8 || p.body.size() != 9 + count * 16) {
                reject(peer, "Invalid input batch");
                return;
            }
            for (std::uint64_t i = 0; i < count; ++i) {
                Input in{rd.get(8),
                         static_cast<std::uint32_t>(rd.get(4)),
                         static_cast<std::uint32_t>(rd.get(4))};
                if (s.role == Role::player && v.phase == Phase::playing)
                    s.input.accept(in, v.authoritative.tick);
            }
        } else
            reject(peer, "Unexpected message");
    }
    void tick() {
        if (!host)
            return;
        ENetEvent event{};
        unsigned count = 0;
        while (count++ < 4096 && enet_host_service(host, &event, 0) > 0) {
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
        for (auto& [p, c] : connections)
            if ((!c.hello && room_tick - c.opened > 300) ||
                (c.recovering && room_tick - c.opened > 300) ||
                (c.snapshot && room_tick - c.sent_at > 300))
                expired.push_back(p);
        for (auto* p : expired) {
            enet_peer_disconnect_now(p, 3);
            drop(p);
        }
        for (auto& [rid, r] : rooms) {
            auto& v = r.view;
            unsigned players = 0;
            bool ready = true;
            for (auto& [id, s] : r.slots)
                if (s.role == Role::player) {
                    ++players;
                    ready &= s.peer && s.ready;
                }
            if (v.phase == Phase::waiting && players >= 2 && ready) {
                v.phase = Phase::countdown;
                v.countdown = 180;
                lobby(r);
            } else if (v.phase == Phase::countdown) {
                if (v.countdown)
                    --v.countdown;
                if (!v.countdown) {
                    std::vector<std::uint16_t> ids;
                    for (auto& [id, s] : r.slots)
                        if (s.role == Role::player)
                            ids.push_back(id);
                    r.last_seed = options.seed ? *options.seed : seeds::random(r.last_seed);
                    v.authoritative =
                        options.initial_state && v.room.preset == options.preset &&
                                v.room.start_level == 0 && ids == std::vector<std::uint16_t>{1, 2}
                            ? *options.initial_state
                            : match::initial_state(
                                  ids, r.last_seed, v.room.preset, v.room.start_level);
                    v.has_snapshot = true;
                    v.sounds.clear();
                    for (const auto& player : v.authoritative.players)
                        v.sounds.push_back({player.id, {}});
                    v.phase = Phase::playing;
                    lobby(r);
                } else if (v.countdown % 30 == 0)
                    lobby(r);
            } else if (v.phase == Phase::playing) {
                std::vector<std::uint32_t> held;
                for (auto& p : v.authoritative.players) {
                    auto i = r.slots.find(p.id);
                    held.push_back(i != r.slots.end() && i->second.peer
                                       ? i->second.input.consume(v.authoritative.tick)
                                       : 0);
                }
                std::string err;
                match::Events events;
                if (!match::advance(v.authoritative, held, &events, err)) {
                    failure = err;
                    v.phase = Phase::error;
                } else if (v.authoritative.result != match::Result::playing) {
                    v.phase = Phase::results;
                    for (auto& [id, s] : r.slots)
                        s.credential = {};
                    lobby(r);
                }
                for (std::size_t i = 0; i < events.boards.size(); ++i) {
                    const auto& e = events.boards[i];
                    auto& counts = v.sounds[i].counts;
                    if (e.locked)
                        ++counts[static_cast<std::size_t>(SoundEvent::locked)];
                    if (e.cleared >= 1 && e.cleared <= 4) {
                        const auto index = e.spin && e.cleared <= 3 ? 4 + e.cleared : e.cleared;
                        ++counts[index];
                    }
                    if (e.inserted)
                        ++counts[static_cast<std::size_t>(SoundEvent::garbage_hit)];
                }
                for (const auto& arrival : events.dispatched)
                    for (auto& player : v.sounds)
                        if (player.player == arrival.recipient)
                            ++player.counts[static_cast<std::size_t>(SoundEvent::garbage_warning)];
            }
            if (v.has_snapshot) {
                v.server_tick = v.accepted_through = v.authoritative.tick;
                ++v.snapshot_revision;
            }
            if (v.has_snapshot)
                for (auto& [p, c] : connections)
                    if (c.room == rid &&
                        (v.phase == Phase::results ||
                         v.authoritative.tick % (c.role == Role::spectator ? 12 : 3) == 0))
                        snapshot(p, c);
        }
        // Preserve disconnected slots for the complete core reconnect grace period.
        // Once a deserted match ends there is nobody left to request a rematch.
        for (auto& [rid, r] : rooms)
            if (r.view.phase == Phase::results &&
                std::none_of(r.slots.begin(), r.slots.end(), [](const auto& entry) {
                    return entry.second.peer != nullptr;
                })) {
                r.slots.clear();
                if (rid == default_room) {
                    auto& v = r.view;
                    v.phase = Phase::waiting;
                    v.countdown = 0;
                    v.has_snapshot = false;
                    v.authoritative = {};
                    v.server_tick = v.accepted_through = 0;
                    v.match_id = random_id();
                    if (!v.match_id)
                        failure = "Secure random unavailable";
                    refresh(r);
                }
            }
        for (auto i = rooms.begin(); i != rooms.end();)
            if (i->first != default_room && i->second.slots.empty())
                i = rooms.erase(i);
            else
                ++i;
        if (!rooms.empty())
            view = rooms.at(default_room).view;
        enet_host_flush(host);
        metrics.sent_bytes += static_cast<enet_uint32>(host->totalSentData - previous_sent);
        previous_sent = host->totalSentData;
        metrics.received_bytes +=
            static_cast<enet_uint32>(host->totalReceivedData - previous_received);
        previous_received = host->totalReceivedData;
        metrics.connections = connections.size();
        metrics.rooms = rooms.size();
        metrics.snapshots_inflight = 0;
        for (auto& [p, c] : connections)
            if (c.snapshot)
                ++metrics.snapshots_inflight;
    }
};
Server::Server(const ServerOptions& o) : impl_(std::make_unique<Impl>(o)) {}
Server::~Server() = default;
ServerMetrics Server::metrics() const {
    return impl_->metrics;
}
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
