#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <stackpulse/network.hpp>
#include <thread>
using namespace stackpulse;
namespace {
void pump(net::Server& server, net::Client& a, net::Client& b, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        a.poll();
        b.poll();
        server.tick();
        a.poll();
        b.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void start(net::Server& server, net::Client& a, net::Client& b, std::uint16_t port) {
    REQUIRE(a.connect("127.0.0.1", port, modern::Preset::fusion));
    REQUIRE(b.connect("127.0.0.1", port, modern::Preset::fusion));
    pump(server, a, b, 30);
    REQUIRE(a.view().connected);
    REQUIRE(b.view().connected);
    a.set_ready(true);
    b.set_ready(true);
    pump(server, a, b, 210);
    REQUIRE(a.view().has_snapshot);
    REQUIRE(b.view().has_snapshot);
    REQUIRE(server.view().phase == net::Phase::playing);
}
} // namespace
TEST_CASE("Actual ENet UDP duel snapshots survive reconnect beyond prediction history",
          "[network][socket]") {
    net::ServerOptions options;
    options.port = 28778;
    net::Server server(options);
    REQUIRE(server.valid());
    net::Client a, b;
    start(server, a, b, options.port);
    REQUIRE(a.view().authoritative.players.size() == 2);
    const auto revision = a.view().snapshot_revision;
    a.disconnect();
    pump(server, a, b, 20);
    REQUIRE(server.view().authoritative.players[0].disconnected);
    pump(server, a, b, 260);
    a.reconnect();
    pump(server, a, b, 40);
    REQUIRE(a.view().connected);
    REQUIRE_FALSE(server.view().authoritative.players[0].disconnected);
    REQUIRE(a.view().snapshot_revision > revision);
    REQUIRE(a.view().authoritative.tick > 240);
    a.disconnect();
    pump(server, a, b, 20);
    a.reconnect();
    pump(server, a, b, 40);
    REQUIRE(a.view().connected);
    REQUIRE_FALSE(server.view().authoritative.players[0].disconnected);
    pump(server, a, b, 6);
    REQUIRE(a.view().phase == net::Phase::playing);
    REQUIRE(match::valid_state(a.view().authoritative));
}
TEST_CASE("Authoritative UDP server expires disconnected players and sends matching results",
          "[network][socket]") {
    net::ServerOptions options;
    options.port = 28779;
    net::Server server(options);
    REQUIRE(server.valid());
    net::Client a, b;
    start(server, a, b, options.port);
    a.disconnect();
    pump(server, a, b, 630);
    REQUIRE(server.view().phase == net::Phase::results);
    REQUIRE(b.view().phase == net::Phase::results);
    REQUIRE(b.view().authoritative == server.view().authoritative);
    a.reconnect();
    pump(server, a, b, 40);
    REQUIRE(a.view().phase == net::Phase::error);
    REQUIRE_FALSE(a.view().connected);
}
TEST_CASE("Rules incompatibility fails explicitly over actual UDP", "[network][socket]") {
    net::ServerOptions options;
    options.port = 28780;
    net::Server server(options);
    REQUIRE(server.valid());
    net::Client a, b;
    REQUIRE(a.connect("127.0.0.1", options.port, modern::Preset::modern));
    pump(server, a, b, 40);
    REQUIRE(a.view().phase == net::Phase::error);
    REQUIRE_FALSE(a.view().connected);
}

#include <enet/enet.h>
#include <stackpulse/protocol.hpp>
namespace {
struct RawPeer {
    ENetHost* host{};
    ENetPeer* peer{};
    bool disconnected{};
    std::vector<net::wire::Packet> received;
    explicit RawPeer(std::uint16_t port) {
        REQUIRE(net::wire::initialize());
        host = enet_host_create(nullptr, 1, 2, 0, 0);
        REQUIRE(host);
        ENetAddress address{};
        address.port = port;
        REQUIRE(enet_address_set_host_ip(&address, "127.0.0.1") == 0);
        peer = enet_host_connect(host, &address, 2, 0);
        REQUIRE(peer);
    }
    ~RawPeer() {
        if (host)
            enet_host_destroy(host);
    }
    void send(net::wire::Packet packet, enet_uint8 channel = 0) {
        auto bytes = net::wire::encode(packet);
        auto* data = enet_packet_create(
            bytes.data(), bytes.size(), channel == 0 ? ENET_PACKET_FLAG_RELIABLE : 0);
        REQUIRE(data);
        REQUIRE(enet_peer_send(peer, channel, data) == 0);
        enet_host_flush(host);
    }
    void poll() {
        ENetEvent e{};
        while (enet_host_service(host, &e, 0) > 0) {
            if (e.type == ENET_EVENT_TYPE_DISCONNECT)
                disconnected = true;
            else if (e.type == ENET_EVENT_TYPE_RECEIVE) {
                net::wire::Packet packet;
                if (net::wire::decode({e.packet->data, e.packet->dataLength}, packet))
                    received.push_back(std::move(packet));
                enet_packet_destroy(e.packet);
            }
        }
    }
    const net::wire::Packet* latest(net::wire::Tag tag) const {
        for (auto i = received.rbegin(); i != received.rend(); ++i)
            if (i->tag == tag)
                return &*i;
        return nullptr;
    }
};
void pump_raw(net::Server& server, RawPeer& a, net::Client& b, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        a.poll();
        b.poll();
        server.tick();
        a.poll();
        b.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void hello(RawPeer& a, bool recovery = false) {
    net::wire::Writer w;
    w.put(modern::rules_version, 4);
    w.put(static_cast<unsigned>(modern::Preset::fusion), 1);
    w.put(recovery, 1);
    a.send({net::wire::Tag::hello, 0, std::move(w.bytes)});
}
} // namespace
TEST_CASE("Raw UDP wrong-session input is ignored and unacknowledged snapshot backlog times out",
          "[network][socket][security]") {
    net::ServerOptions options;
    options.port = 28781;
    net::Server server(options);
    REQUIRE(server.valid());
    RawPeer a(options.port);
    net::Client b;
    REQUIRE(b.connect("127.0.0.1", options.port, modern::Preset::fusion));
    pump_raw(server, a, b, 20);
    hello(a);
    pump_raw(server, a, b, 20);
    auto welcome = a.latest(net::wire::Tag::welcome);
    REQUIRE(welcome);
    const auto session = welcome->session;
    net::wire::Reader wr{welcome->body};
    auto id = wr.get(2), match_id = wr.get(8);
    REQUIRE(id >= 1);
    REQUIRE(id <= 2);
    net::wire::Writer ready;
    ready.put(1, 1);
    a.send({net::wire::Tag::ready, session, std::move(ready.bytes)});
    b.set_ready(true);
    pump_raw(server, a, b, 190);
    REQUIRE(server.view().phase == net::Phase::playing);
    const auto before = server.view().authoritative.players[id - 1].board.active.x;
    net::wire::Writer input;
    input.put(match_id, 8);
    input.put(1, 1);
    input.put(server.view().authoritative.tick + 3, 8);
    input.put(1, 4);
    input.put(modern::left, 4);
    a.send({net::wire::Tag::input, session ^ 1, std::move(input.bytes)}, 1);
    pump_raw(server, a, b, 10);
    REQUIRE(server.view().authoritative.players[id - 1].board.active.x == before);
    net::wire::Writer valid;
    valid.put(match_id, 8);
    valid.put(1, 1);
    valid.put(server.view().authoritative.tick + 3, 8);
    valid.put(2, 4);
    valid.put(modern::left, 4);
    a.send({net::wire::Tag::input, session, std::move(valid.bytes)}, 1);
    pump_raw(server, a, b, 10);
    REQUIRE(server.view().authoritative.players[id - 1].board.active.x < before);
    std::size_t chunks = 0;
    for (auto& p : a.received)
        chunks += p.tag == net::wire::Tag::chunk;
    REQUIRE(chunks > 0);
    REQUIRE(chunks < 100);
    pump_raw(server, a, b, 310);
    REQUIRE(server.view().authoritative.players[id - 1].disconnected);
    std::size_t later = 0;
    for (auto& p : a.received)
        later += p.tag == net::wire::Tag::chunk;
    REQUIRE(later == chunks);
}
TEST_CASE("Raw UDP reconnect rotates credentials and rejects reuse of old token",
          "[network][socket][security]") {
    using namespace net::wire;
    net::ServerOptions options;
    options.port = 28782;
    net::Server server(options);
    REQUIRE(server.valid());
    RawPeer original(options.port);
    net::Client b;
    REQUIRE(b.connect("127.0.0.1", options.port, modern::Preset::fusion));
    pump_raw(server, original, b, 20);
    hello(original);
    pump_raw(server, original, b, 20);
    const auto first = original.latest(Tag::welcome);
    REQUIRE(first);
    const auto first_session = first->session;
    Reader identity{first->body};
    const auto id = identity.get(2), match_id = identity.get(8);
    const auto old_token = identity.data(32);
    identity.get(8);
    identity.get(1);
    REQUIRE(identity.done());
    Writer ready;
    ready.put(1, 1);
    original.send({Tag::ready, first_session, std::move(ready.bytes)});
    b.set_ready(true);
    pump_raw(server, original, b, 190);
    REQUIRE(server.view().phase == net::Phase::playing);
    enet_peer_disconnect(original.peer, 0);
    enet_host_flush(original.host);
    pump_raw(server, original, b, 20);
    REQUIRE(server.view().authoritative.players[id - 1].disconnected);
    RawPeer recovery(options.port);
    pump_raw(server, recovery, b, 20);
    hello(recovery, true);
    pump_raw(server, recovery, b, 20);
    const auto provisional = recovery.latest(Tag::welcome);
    REQUIRE(provisional);
    Writer reconnect;
    reconnect.put(match_id, 8);
    reconnect.put(id, 2);
    reconnect.data(old_token);
    recovery.send({Tag::reconnect, provisional->session, std::move(reconnect.bytes)});
    pump_raw(server, recovery, b, 30);
    const auto second = recovery.latest(Tag::welcome);
    REQUIRE(second);
    Reader rotated{second->body};
    REQUIRE(rotated.get(2) == id);
    REQUIRE(rotated.get(8) == match_id);
    const auto new_token = rotated.data(32);
    rotated.get(8);
    rotated.get(1);
    REQUIRE(rotated.done());
    REQUIRE(new_token != old_token);
    REQUIRE_FALSE(server.view().authoritative.players[id - 1].disconnected);
    enet_peer_disconnect(recovery.peer, 0);
    enet_host_flush(recovery.host);
    pump_raw(server, recovery, b, 20);
    REQUIRE(server.view().authoritative.players[id - 1].disconnected);
    RawPeer attacker(options.port);
    pump_raw(server, attacker, b, 20);
    hello(attacker, true);
    pump_raw(server, attacker, b, 20);
    const auto third = attacker.latest(Tag::welcome);
    REQUIRE(third);
    Writer reused;
    reused.put(match_id, 8);
    reused.put(id, 2);
    reused.data(old_token);
    attacker.send({Tag::reconnect, third->session, std::move(reused.bytes)});
    pump_raw(server, attacker, b, 30);
    REQUIRE(attacker.latest(Tag::reject));
    REQUIRE(server.view().authoritative.players[id - 1].disconnected);
}
TEST_CASE("Actual UDP snapshots carry server-generated attack and garbage insertion",
          "[network][socket][battle]") {
    net::ServerOptions options;
    options.port = 28783;
    options.preset = modern::Preset::modern;
    auto initial = match::initial_state({1, 2}, 727, modern::Preset::modern);
    initial.players[0].board = modern::spin_fixture(17, modern::Preset::modern, 2);
    REQUIRE(match::valid_state(initial));
    options.initial_state = initial;
    net::Server server(options);
    REQUIRE(server.valid());
    net::Client a, b;
    REQUIRE(a.connect("127.0.0.1", options.port, modern::Preset::modern));
    REQUIRE(b.connect("127.0.0.1", options.port, modern::Preset::modern));
    pump(server, a, b, 30);
    REQUIRE(a.view().player == 1);
    a.set_ready(true);
    b.set_ready(true);
    for (unsigned i = 0; i < 190 && server.view().phase != net::Phase::playing; ++i)
        pump(server, a, b, 1);
    REQUIRE(server.view().phase == net::Phase::playing);
    REQUIRE(server.view().authoritative.tick == 0);
    a.submit({{0, 1, modern::clockwise}});
    pump(server, a, b, 1);
    a.submit({{1, 2, modern::hard_drop}});
    pump(server, a, b, 1);
    REQUIRE(server.view().authoritative.next_packet_id == 2);
    REQUIRE(server.view().authoritative.scheduled.size() == 1);
    REQUIRE(server.view().authoritative.scheduled[0].recipient == 2);
    const auto rows = server.view().authoritative.scheduled[0].packet.rows;
    REQUIRE(rows >= 4);
    pump(server, a, b, 8);
    REQUIRE_FALSE(b.view().authoritative.players[1].board.incoming.empty());
    REQUIRE(b.view().authoritative.players[1].board.incoming[0].rows == rows);
    REQUIRE(b.view().authoritative.next_packet_id == 2);
    while (server.view().authoritative.tick < 35)
        pump(server, a, b, 1);
    const auto tick = server.view().authoritative.tick;
    b.submit({{tick, 1, modern::hard_drop}});
    pump(server, a, b, 8);
    REQUIRE(server.view().authoritative.players[1].board.incoming.empty());
    REQUIRE(std::count(server.view().authoritative.players[1].board.board.begin(),
                       server.view().authoritative.players[1].board.board.end(),
                       8) >= rows * 9);
    REQUIRE(b.view().authoritative.players[1].board.incoming.empty());
    REQUIRE(match::valid_state(a.view().authoritative));
    REQUIRE(match::valid_state(b.view().authoritative));
}
TEST_CASE("Invalid constructor fixtures reject before binding a UDP socket", "[network][battle]") {
    net::ServerOptions options;
    options.initial_state = match::initial_state({1, 3}, 4);
    net::Server server(options);
    REQUIRE_FALSE(server.valid());
    REQUIRE_FALSE(server.error().empty());
}
TEST_CASE("Raw UDP control quota disconnect releases its waiting player slot",
          "[network][socket][security]") {
    using namespace net::wire;
    net::ServerOptions options;
    options.port = 28784;
    net::Server server(options);
    REQUIRE(server.valid());
    RawPeer excess(options.port);
    net::Client opponent;
    REQUIRE(opponent.connect("127.0.0.1", options.port, modern::Preset::fusion));
    pump_raw(server, excess, opponent, 20);
    hello(excess);
    pump_raw(server, excess, opponent, 20);
    const auto welcome = excess.latest(Tag::welcome);
    REQUIRE(welcome);
    const auto session = welcome->session;
    Reader identity{welcome->body};
    const auto id = identity.get(2);
    // All controls arrive within one room-clock quota window; the third excess
    // packet must clean up the slot even though disconnect_now emits no local event.
    for (unsigned i = 0; i < 100; ++i) {
        Writer ready;
        ready.put(0, 1);
        excess.send({Tag::ready, session, std::move(ready.bytes)});
    }
    pump_raw(server, excess, opponent, 30);
    REQUIRE(excess.disconnected);
    REQUIRE(server.view().phase == net::Phase::waiting);
    net::Client replacement;
    REQUIRE(replacement.connect("127.0.0.1", options.port, modern::Preset::fusion));
    pump(server, replacement, opponent, 30);
    REQUIRE(replacement.view().connected);
    REQUIRE(replacement.view().player > id);
    replacement.set_ready(true);
    opponent.set_ready(true);
    pump(server, replacement, opponent, 210);
    REQUIRE(server.view().phase == net::Phase::playing);
    REQUIRE(replacement.view().has_snapshot);
    REQUIRE(opponent.view().has_snapshot);
}
TEST_CASE("Raw UDP provisional recovery handshake expires without a credential",
          "[network][socket][security]") {
    using namespace net::wire;
    net::ServerOptions options;
    options.port = 28785;
    net::Server server(options);
    REQUIRE(server.valid());
    RawPeer provisional(options.port);
    net::Client observer;
    pump_raw(server, provisional, observer, 20);
    hello(provisional, true);
    pump_raw(server, provisional, observer, 20);
    const auto welcome = provisional.latest(Tag::welcome);
    REQUIRE(welcome);
    Reader identity{welcome->body};
    REQUIRE(identity.get(2) == 0);
    pump_raw(server, provisional, observer, 240);
    REQUIRE_FALSE(provisional.disconnected);
    pump_raw(server, provisional, observer, 40);
    REQUIRE(provisional.disconnected);
    REQUIRE(server.view().phase == net::Phase::waiting);
    net::Client a, b;
    start(server, a, b, options.port);
    REQUIRE(a.view().connected);
    REQUIRE(b.view().connected);
}
TEST_CASE("Fresh UDP connect clears the old match view and preserves revision monotonicity",
          "[network][socket]") {
    net::ServerOptions first_options;
    first_options.port = 28786;
    net::Server first(first_options);
    REQUIRE(first.valid());
    net::Client a, b;
    start(first, a, b, first_options.port);
    const auto revision = a.view().snapshot_revision;
    REQUIRE(revision > 0);
    REQUIRE(a.view().server_tick > 0);
    net::ServerOptions next_options;
    next_options.port = 28787;
    net::Server next(next_options);
    REQUIRE(next.valid());
    net::Client c;
    REQUIRE(a.connect("127.0.0.1", next_options.port, modern::Preset::fusion));
    REQUIRE(a.view().phase == net::Phase::connecting);
    REQUIRE_FALSE(a.view().connected);
    REQUIRE_FALSE(a.view().has_snapshot);
    REQUIRE(a.view().player == 0);
    REQUIRE(a.view().server_tick == 0);
    REQUIRE(a.view().accepted_through == 0);
    REQUIRE(a.view().authoritative.players.empty());
    REQUIRE(c.connect("127.0.0.1", next_options.port, modern::Preset::fusion));
    pump(next, a, c, 30);
    REQUIRE(a.view().connected);
    REQUIRE(a.view().phase == net::Phase::waiting);
    REQUIRE_FALSE(a.view().has_snapshot);
    a.set_ready(true);
    c.set_ready(true);
    pump(next, a, c, 210);
    REQUIRE(a.view().has_snapshot);
    REQUIRE(a.view().snapshot_revision > revision);
    REQUIRE(next.view().phase == net::Phase::playing);
}
TEST_CASE("Raw UDP valid recovery migrates a still connected peer", "[network][socket][security]") {
    using namespace net::wire;
    net::ServerOptions options;
    options.port = 28788;
    net::Server server(options);
    REQUIRE(server.valid());
    RawPeer original(options.port);
    net::Client opponent;
    REQUIRE(opponent.connect("127.0.0.1", options.port, modern::Preset::fusion));
    pump_raw(server, original, opponent, 20);
    hello(original);
    pump_raw(server, original, opponent, 20);
    const auto first = original.latest(Tag::welcome);
    REQUIRE(first);
    const auto session = first->session;
    Reader identity{first->body};
    const auto id = identity.get(2), match_id = identity.get(8);
    const auto token = identity.data(32);
    identity.get(8);
    identity.get(1);
    REQUIRE(identity.done());
    Writer ready;
    ready.put(1, 1);
    original.send({Tag::ready, session, std::move(ready.bytes)});
    opponent.set_ready(true);
    pump_raw(server, original, opponent, 190);
    REQUIRE(server.view().phase == net::Phase::playing);
    REQUIRE_FALSE(original.disconnected);
    REQUIRE_FALSE(server.view().authoritative.players[id - 1].disconnected);
    RawPeer replacement(options.port);
    pump_raw(server, replacement, opponent, 20);
    hello(replacement, true);
    pump_raw(server, replacement, opponent, 20);
    const auto provisional = replacement.latest(Tag::welcome);
    REQUIRE(provisional);
    Writer recovery;
    recovery.put(match_id, 8);
    recovery.put(id, 2);
    recovery.data(token);
    replacement.send({Tag::reconnect, provisional->session, std::move(recovery.bytes)});
    pump_raw(server, replacement, opponent, 30);
    original.poll();
    const auto migrated = replacement.latest(Tag::welcome);
    REQUIRE(migrated);
    Reader rotated{migrated->body};
    REQUIRE(rotated.get(2) == id);
    REQUIRE(rotated.get(8) == match_id);
    REQUIRE(rotated.data(32) != token);
    rotated.get(8);
    rotated.get(1);
    REQUIRE(rotated.done());
    REQUIRE(original.disconnected);
    REQUIRE_FALSE(server.view().authoritative.players[id - 1].disconnected);
    REQUIRE(server.view().authoritative.players[id - 1].deadline == 0);
    REQUIRE(replacement.latest(Tag::chunk));
    const auto before = server.view().authoritative.players[id - 1].board.active.x;
    Writer input;
    input.put(match_id, 8);
    input.put(1, 1);
    input.put(server.view().authoritative.tick + 3, 8);
    input.put(1, 4);
    input.put(modern::left, 4);
    replacement.send({Tag::input, migrated->session, std::move(input.bytes)}, 1);
    pump_raw(server, replacement, opponent, 10);
    REQUIRE(server.view().authoritative.players[id - 1].board.active.x < before);
    REQUIRE(opponent.view().connected);
}
namespace {
void pump_many(net::Server& server, const std::vector<net::Client*>& clients, unsigned ticks) {
    for (unsigned i = 0; i < ticks; ++i) {
        for (auto* c : clients)
            c->poll();
        server.tick();
        for (auto* c : clients)
            c->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace
TEST_CASE("M4 actual UDP admits eight players sixteen late spectators and bounds overflow",
          "[network][socket][rooms]") {
    net::ServerOptions o;
    o.port = 28790;
    net::Server server(o);
    REQUIRE(server.valid());
    std::vector<std::unique_ptr<net::Client>> owned;
    std::vector<net::Client*> peers;
    for (unsigned i = 0; i < 8; ++i) {
        owned.push_back(std::make_unique<net::Client>());
        peers.push_back(owned.back().get());
        REQUIRE(peers.back()->connect("127.0.0.1", o.port, o.preset));
    }
    pump_many(server, peers, 40);
    for (auto* c : peers) {
        REQUIRE(c->view().connected);
        REQUIRE(c->view().members.size() == 8);
    }
    net::Client full;
    REQUIRE(full.connect("127.0.0.1", o.port, o.preset));
    peers.push_back(&full);
    pump_many(server, peers, 10);
    REQUIRE(full.view().phase == net::Phase::error);
    peers.pop_back();
    for (auto* c : peers)
        c->set_ready(true);
    pump_many(server, peers, 220);
    REQUIRE(server.view().phase == net::Phase::playing);
    REQUIRE(server.view().authoritative.players.size() == 8);
    const auto code = peers.front()->view().room_code;
    for (unsigned i = 0; i < 16; ++i) {
        owned.push_back(std::make_unique<net::Client>());
        peers.push_back(owned.back().get());
        net::ConnectOptions co;
        co.port = o.port;
        co.room_code = code;
        REQUIRE(peers.back()->connect(co));
    }
    pump_many(server, peers, 50);
    for (unsigned i = 8; i < 24; ++i) {
        REQUIRE(peers[i]->view().connected);
        REQUIRE(peers[i]->view().role == net::Role::spectator);
        REQUIRE(peers[i]->view().player == 0);
        REQUIRE(peers[i]->view().has_snapshot);
        REQUIRE(peers[i]->view().authoritative.players.size() == 8);
        peers[i]->set_ready(true);
        peers[i]->submit({{server.view().authoritative.tick + 2, 1, modern::hard_drop}});
    }
    pump_many(server, peers, 12);
    REQUIRE(server.view().members.size() == 24);
    REQUIRE(server.metrics().snapshots_inflight <= 24);
    net::Client excess;
    net::ConnectOptions co;
    co.port = o.port;
    co.role = net::Role::spectator;
    REQUIRE(excess.connect(co));
    peers.push_back(&excess);
    pump_many(server, peers, 30);
    REQUIRE(excess.view().phase == net::Phase::error);
}
TEST_CASE("M4 private rooms isolate snapshots and manager transfer allows rematch",
          "[network][socket][rooms]") {
    net::ServerOptions o;
    o.port = 28791;
    net::Server server(o);
    REQUIRE(server.valid());
    net::Client a, b, c, d;
    std::vector<net::Client*> peers{&a, &b, &c, &d};
    REQUIRE(a.connect("127.0.0.1", o.port, o.preset));
    REQUIRE(b.connect("127.0.0.1", o.port, o.preset));
    net::ConnectOptions co;
    co.port = o.port;
    co.create_room = true;
    REQUIRE(c.connect(co));
    pump_many(server, peers, 30);
    REQUIRE(a.view().room_id != c.view().room_id);
    co.create_room = false;
    co.room_code = c.view().room_code;
    REQUIRE(d.connect(co));
    pump_many(server, peers, 30);
    for (auto* p : peers)
        p->set_ready(true);
    pump_many(server, peers, 215);
    REQUIRE(a.view().has_snapshot);
    REQUIRE(c.view().has_snapshot);
    REQUIRE(a.view().match_id != c.view().match_id);
    auto old = c.view().match_id;
    const auto a_match = a.view().match_id;
    auto c_id = c.view().player;
    c.disconnect();
    pump_many(server, peers, 20);
    REQUIRE(d.view().manager_id == d.view().member_id);
    c.reconnect();
    pump_many(server, peers, 35);
    REQUIRE(c.view().connected);
    REQUIRE(c.view().player == c_id);
    REQUIRE(c.view().match_id == old);
    REQUIRE(a.view().match_id == a_match);
    c.disconnect();
    pump_many(server, peers, 635);
    REQUIRE(d.view().phase == net::Phase::results);
    d.return_to_lobby();
    pump_many(server, peers, 20);
    REQUIRE(d.view().phase == net::Phase::waiting);
    REQUIRE_FALSE(d.view().has_snapshot);
    REQUIRE(d.view().match_id != old);
    co.room_code = d.view().room_code;
    REQUIRE(c.connect(co));
    pump_many(server, peers, 25);
    c.set_ready(true);
    d.set_ready(true);
    pump_many(server, peers, 215);
    REQUIRE(c.view().phase == net::Phase::playing);
    REQUIRE(d.view().phase == net::Phase::playing);
    REQUIRE(c.view().match_id != old);
    REQUIRE(c.view().authoritative.players.size() == 2);
    REQUIRE(a.view().room_id != c.view().room_id);
    REQUIRE(a.view().match_id == a_match);
}
TEST_CASE("M4 spectator raw input has no player authority", "[network][socket][security][rooms]") {
    using namespace net::wire;
    net::ServerOptions o;
    o.port = 28792;
    net::Server server(o);
    net::Client a, b;
    start(server, a, b, o.port);
    RawPeer spectator(o.port);
    pump_raw(server, spectator, b, 20);
    hello(spectator);
    pump_raw(server, spectator, b, 25);
    const auto* welcome = spectator.latest(Tag::welcome);
    REQUIRE(welcome);
    Reader rd{welcome->body};
    REQUIRE(rd.get(2) != 0);
    auto mid = rd.get(8);
    rd.data(32);
    rd.get(8);
    REQUIRE(rd.get(1) == static_cast<unsigned>(net::Role::spectator));
    auto before = server.view().authoritative.players[0].board.active.x;
    Writer w;
    w.put(mid, 8);
    w.put(1, 1);
    w.put(server.view().authoritative.tick + 3, 8);
    w.put(1, 4);
    w.put(modern::left, 4);
    spectator.send({Tag::input, welcome->session, std::move(w.bytes)}, 1);
    pump_raw(server, spectator, b, 10);
    REQUIRE(server.view().authoritative.players[0].board.active.x == before);
    REQUIRE(server.view().authoritative.players.size() == 2);
}

TEST_CASE("M4 empty private rooms are recycled across repeated creates",
          "[network][socket][rooms]") {
    net::ServerOptions o;
    o.port = 28793;
    o.rooms = 2;
    net::Server server(o);
    net::Client a, b;
    REQUIRE(a.connect("127.0.0.1", o.port, o.preset));
    pump(server, a, b, 25);
    for (unsigned i = 0; i < 12; ++i) {
        a.create_room();
        pump(server, a, b, 20);
        REQUIRE(a.view().connected);
        REQUIRE(a.view().room_id != server.view().room_id);
        a.leave_room();
        pump(server, a, b, 15);
        REQUIRE(a.view().connected);
        REQUIRE(a.view().room_id == 0);
        REQUIRE(server.metrics().rooms == 1);
    }
}
TEST_CASE("M4 credentials from another private room cannot recover its player",
          "[network][socket][security][rooms]") {
    using namespace net::wire;
    net::ServerOptions o;
    o.port = 28794;
    net::Server server(o);
    REQUIRE(server.valid());
    RawPeer original(o.port);
    net::Client b, c, d;
    std::vector<net::Client*> clients{&b, &c, &d};
    REQUIRE(b.connect("127.0.0.1", o.port, o.preset));
    pump_raw(server, original, b, 20);
    hello(original);
    pump_raw(server, original, b, 20);
    const auto* first = original.latest(Tag::welcome);
    REQUIRE(first);
    const auto session = first->session;
    Reader rd{first->body};
    auto id = rd.get(2);
    rd.get(8);
    auto token = rd.data(32);
    net::ConnectOptions co;
    co.port = o.port;
    co.create_room = true;
    REQUIRE(c.connect(co));
    pump_many(server, clients, 25);
    co.create_room = false;
    co.room_code = c.view().room_code;
    REQUIRE(d.connect(co));
    pump_many(server, clients, 25);
    Writer ready;
    ready.put(1, 1);
    original.send({Tag::ready, session, std::move(ready.bytes)});
    b.set_ready(true);
    c.set_ready(true);
    d.set_ready(true);
    pump_many(server, clients, 215);
    REQUIRE(c.view().phase == net::Phase::playing);
    REQUIRE(c.view().match_id != b.view().match_id);
    RawPeer attacker(o.port);
    pump_raw(server, attacker, b, 20);
    hello(attacker, true);
    pump_raw(server, attacker, b, 20);
    const auto* provisional = attacker.latest(Tag::welcome);
    REQUIRE(provisional);
    Writer spoof;
    spoof.put(c.view().match_id, 8);
    spoof.put(id, 2);
    spoof.data(token);
    attacker.send({Tag::reconnect, provisional->session, std::move(spoof.bytes)});
    pump_raw(server, attacker, b, 25);
    REQUIRE(attacker.latest(Tag::reject));
    REQUIRE(c.view().connected);
    REQUIRE(c.view().room_id != b.view().room_id);
}
TEST_CASE("M4 explicit leave revokes credentials including the zero token",
          "[network][socket][security][rooms]") {
    using namespace net::wire;
    net::ServerOptions o;
    o.port = 28795;
    net::Server server(o);
    net::Client a, b;
    start(server, a, b, o.port);
    const auto mid = a.view().match_id;
    const auto id = a.view().player;
    a.leave_room();
    pump(server, a, b, 20);
    REQUIRE(a.view().room_id == 0);
    RawPeer attacker(o.port);
    pump_raw(server, attacker, b, 20);
    hello(attacker, true);
    pump_raw(server, attacker, b, 20);
    const auto* provisional = attacker.latest(Tag::welcome);
    REQUIRE(provisional);
    Writer forged;
    forged.put(mid, 8);
    forged.put(id, 2);
    std::array<std::uint8_t, 32> zero{};
    forged.data(zero);
    attacker.send({Tag::reconnect, provisional->session, std::move(forged.bytes)});
    pump_raw(server, attacker, b, 20);
    REQUIRE(attacker.latest(Tag::reject));
    auto player = std::find_if(server.view().authoritative.players.begin(),
                               server.view().authoritative.players.end(),
                               [&](auto& p) { return p.id == id; });
    REQUIRE(player != server.view().authoritative.players.end());
    REQUIRE(player->disconnected);
}
TEST_CASE("M4 spectator leaving countdown cannot cancel the player roster",
          "[network][socket][rooms]") {
    net::ServerOptions o;
    o.port = 28796;
    net::Server server(o);
    net::Client a, b, s;
    std::vector<net::Client*> peers{&a, &b, &s};
    REQUIRE(a.connect("127.0.0.1", o.port, o.preset));
    REQUIRE(b.connect("127.0.0.1", o.port, o.preset));
    net::ConnectOptions co;
    co.port = o.port;
    co.role = net::Role::spectator;
    REQUIRE(s.connect(co));
    pump_many(server, peers, 25);
    a.set_ready(true);
    b.set_ready(true);
    pump_many(server, peers, 10);
    REQUIRE(server.view().phase == net::Phase::countdown);
    const auto before = server.view().countdown;
    s.leave_room();
    pump_many(server, peers, 10);
    REQUIRE(server.view().phase == net::Phase::countdown);
    REQUIRE(server.view().countdown < before);
}
TEST_CASE("M4 abandoned terminal rooms reset default and reap private capacity",
          "[network][socket][rooms]") {
    net::ServerOptions o;
    o.port = 28797;
    o.rooms = 2;
    net::Server server(o);
    net::Client a, b;
    start(server, a, b, o.port);
    const auto old = server.view().match_id;
    a.disconnect();
    b.disconnect();
    pump(server, a, b, 20);
    REQUIRE(server.view().phase == net::Phase::playing);
    pump(server, a, b, 620);
    REQUIRE(server.view().phase == net::Phase::waiting);
    REQUIRE_FALSE(server.view().has_snapshot);
    REQUIRE(server.view().members.empty());
    REQUIRE(server.view().match_id != old);
    REQUIRE(server.view().match_id != 0);
    net::ConnectOptions co;
    co.port = o.port;
    co.create_room = true;
    REQUIRE(a.connect(co));
    pump(server, a, b, 25);
    const auto code = a.view().room_code;
    co.create_room = false;
    co.room_code = code;
    REQUIRE(b.connect(co));
    pump(server, a, b, 25);
    a.set_ready(true);
    b.set_ready(true);
    pump(server, a, b, 210);
    REQUIRE(a.view().phase == net::Phase::playing);
    REQUIRE(server.metrics().rooms == 2);
    a.disconnect();
    b.disconnect();
    pump(server, a, b, 20);
    REQUIRE(server.metrics().rooms == 2);
    pump(server, a, b, 620);
    REQUIRE(server.metrics().rooms == 1);
    co.create_room = true;
    co.room_code.clear();
    REQUIRE(a.connect(co));
    pump(server, a, b, 25);
    REQUIRE(a.view().connected);
    REQUIRE(server.metrics().rooms == 2);
}
