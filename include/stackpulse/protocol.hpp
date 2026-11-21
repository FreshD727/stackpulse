#pragma once
#include <array>
#include <map>
#include <span>
#include <stackpulse/network.hpp>

namespace stackpulse::net::wire {
bool initialize();
constexpr std::size_t max_packet = 1200, max_object = 1024 * 1024, chunk_size = 1120;
enum class Tag : std::uint8_t {
    hello = 1,
    welcome,
    ready,
    lobby,
    input,
    chunk,
    snapshot_ack,
    reconnect,
    reject,
    result,
    create_room,
    join_room,
    leave_room,
    return_lobby,
    featured
};
struct Packet {
    Tag tag{};
    std::uint64_t session{};
    std::vector<std::uint8_t> body;
};
struct Writer {
    std::vector<std::uint8_t> bytes;
    void put(std::uint64_t v, unsigned n);
    void data(std::span<const std::uint8_t> value);
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t offset{};
    bool ok{true};
    std::uint64_t get(unsigned n);
    std::vector<std::uint8_t> data(std::size_t n);
    bool done() const;
};
std::vector<std::uint8_t> encode(const Packet&);
bool decode(std::span<const std::uint8_t>, Packet&);
bool random_bytes(std::span<std::uint8_t>);
std::uint64_t random_id();
struct Assembly {
    std::uint32_t transfer{}, total{}, count{};
    std::uint64_t started{};
    std::map<std::uint32_t, std::vector<std::uint8_t>> parts;
    void reset();
    // Fixed partition, duplicates identical, no allocation before bounds validation.
    bool add(Reader&, std::uint64_t now, std::vector<std::uint8_t>& complete);
};
std::vector<Packet>
chunks(std::uint64_t session, std::uint32_t transfer, const std::vector<std::uint8_t>&);
struct Admission {
    std::map<std::uint64_t, Input> pending;
    std::uint32_t held{}, missing{};
    std::uint64_t accepted_through{};
    bool accept(Input, std::uint64_t tick);
    std::uint32_t consume(std::uint64_t tick);
    void clear();
};
} // namespace stackpulse::net::wire
