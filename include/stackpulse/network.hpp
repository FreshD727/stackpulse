#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <stackpulse/match.hpp>
#include <string>
#include <vector>

namespace stackpulse::net {
inline constexpr std::uint16_t default_port = 27877;
enum class Phase : std::uint8_t {
    connecting,
    waiting,
    countdown,
    playing,
    results,
    disconnected,
    error,
    browsing
};
enum class Role : std::uint8_t { player, spectator };
struct RoomOptions {
    std::string name{"Lobby"}, password;
    std::uint8_t players{8};
    modern::Preset preset{modern::Preset::fusion};
    std::uint8_t start_level{};
};
struct RoomInfo {
    std::uint64_t id{};
    std::string code, name;
    bool password_required{};
    modern::Preset preset{modern::Preset::fusion};
    std::uint8_t start_level{}, players{}, capacity{}, spectators{};
    Phase state{Phase::waiting};
};
struct ConnectOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{default_port};
    modern::Preset preset{modern::Preset::fusion};
    std::string name{"Player"};
    Role role{Role::player};
    std::string room_code;
    bool create_room{}, browse_only{};
};
struct Member {
    std::uint16_t id{}, player{};
    std::string name;
    Role role{Role::player};
    bool ready{}, connected{};
};
struct Input {
    std::uint64_t tick{};
    std::uint32_t sequence{}, held{};
};
struct View {
    Phase phase{Phase::connecting};
    std::string detail;
    std::uint16_t player{};
    Role role{Role::player};
    std::uint16_t member_id{}, manager_id{}, featured_player{};
    std::uint64_t room_id{}, match_id{};
    std::string room_code;
    std::uint8_t player_limit{}, spectator_limit{};
    std::vector<Member> members;
    RoomOptions room;
    std::vector<RoomInfo> rooms;
    std::string operation_error;
    std::uint64_t operation_revision{};
    // Match simulation boundary (next input tick), never the room countdown clock.
    // accepted_through is the simulated boundary: frames < it are irrevocably past,
    // not the highest future frame received. Revision strictly increases per Client
    // lifetime, including reconnect; stale snapshots never increment it.
    std::uint64_t server_tick{}, snapshot_revision{}, accepted_through{};
    std::uint32_t countdown{};
    std::uint32_t round_trip_ms{}; // ENet smoothed RTT; presentation clock estimate only.
    bool connected{}, ready{}, opponent_ready{}, has_snapshot{};
    match::State authoritative;
};
// Owns ENet socket/session and credentials. No board/score/attack uplink exists.
class Client {
  public:
    Client();
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    bool connect(const ConnectOptions&);
    void create_room();
    void create_room(const RoomOptions&);
    void configure_room(const RoomOptions&);
    void list_rooms();
    void join_room(const std::string& code, const std::string& password, Role role = Role::player);
    void join_room(const std::string& code, Role role = Role::player);
    void leave_room();
    void return_to_lobby();
    void set_featured(std::uint16_t player);
    bool connect(const std::string& host, std::uint16_t port, modern::Preset preset);
    void poll();
    void set_ready(bool ready);
    void submit(const std::vector<Input>& rolling_frames);
    // Retains private credential for a fresh handshake and rotates it on recovery.
    void reconnect();
    void disconnect();
    const View& view() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct ServerOptions {
    std::string bind{"127.0.0.1"};
    std::uint16_t port{default_port};
    modern::Preset preset{modern::Preset::fusion};
    std::uint32_t seed{727};
    std::uint8_t players{8}, spectators{16}, rooms{8};
    // Constructor-only deterministic integration fixture; never accepted over wire
    // or exposed by the server CLI. Must be a valid playing tick-zero duel with
    // roster {1,2} and the selected preset. Invalid fixtures reject construction.
    std::optional<match::State> initial_state;
};
struct ServerMetrics {
    std::uint64_t sent_bytes{}, received_bytes{};
    std::size_t connections{}, rooms{}, snapshots_inflight{};
};
class Server {
  public:
    explicit Server(const ServerOptions&);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    ServerMetrics metrics() const;
    bool valid() const;
    const std::string& error() const;
    // Poll sockets without blocking, then advance the 60Hz room clock by one tick.
    void tick();
    const View& view() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace stackpulse::net
