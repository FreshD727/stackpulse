#include <algorithm>
#include <enet/enet.h>
#include <fstream>
#include <stackpulse/protocol.hpp>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <bcrypt.h>
#include <windows.h>
#endif
namespace stackpulse::net::wire {
bool initialize() {
    static const bool initialized = enet_initialize() == 0;
    return initialized;
}
void Writer::put(std::uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        bytes.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
}
void Writer::data(std::span<const std::uint8_t> v) {
    bytes.insert(bytes.end(), v.begin(), v.end());
}
std::uint64_t Reader::get(unsigned n) {
    if (n > 8 || offset > bytes.size() || n > bytes.size() - offset) {
        ok = false;
        return 0;
    }
    std::uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i)
        v |= std::uint64_t(bytes[offset++]) << (i * 8);
    return v;
}
std::vector<std::uint8_t> Reader::data(std::size_t n) {
    if (offset > bytes.size() || n > bytes.size() - offset) {
        ok = false;
        return {};
    }
    std::vector<std::uint8_t> out(bytes.begin() + offset, bytes.begin() + offset + n);
    offset += n;
    return out;
}
bool Reader::done() const {
    return ok && offset == bytes.size();
}
std::vector<std::uint8_t> encode(const Packet& p) {
    if (p.body.size() > max_packet - 16)
        return {};
    Writer w;
    w.put(0x33445053, 4);
    w.put(1, 1);
    w.put(static_cast<unsigned>(p.tag), 1);
    w.put(p.body.size(), 2);
    w.put(p.session, 8);
    w.data(p.body);
    return w.bytes;
}
bool decode(std::span<const std::uint8_t> b, Packet& out) {
    if (b.size() < 16 || b.size() > max_packet)
        return false;
    Reader r{b};
    if (r.get(4) != 0x33445053 || r.get(1) != 1)
        return false;
    auto t = r.get(1), n = r.get(2), s = r.get(8);
    if (t < 1 || t > 10 || n != b.size() - 16)
        return false;
    Packet p{static_cast<Tag>(t), s, r.data(static_cast<std::size_t>(n))};
    if (!r.done())
        return false;
    out = std::move(p);
    return true;
}
bool random_bytes(std::span<std::uint8_t> out) {
#ifdef _WIN32
    return BCryptGenRandom(nullptr,
                           out.data(),
                           static_cast<ULONG>(out.size()),
                           BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    std::ifstream f("/dev/urandom", std::ios::binary);
    return static_cast<bool>(
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size())));
#endif
}
std::uint64_t random_id() {
    std::uint64_t v = 0;
    if (!random_bytes({reinterpret_cast<std::uint8_t*>(&v), sizeof(v)}))
        return 0;
    return v ? v : 1;
}
void Assembly::reset() {
    transfer = total = count = 0;
    started = 0;
    parts.clear();
}
bool Assembly::add(Reader& r, std::uint64_t now, std::vector<std::uint8_t>& complete) {
    complete.clear();
    auto id = static_cast<std::uint32_t>(r.get(4)), index = static_cast<std::uint32_t>(r.get(2)),
         c = static_cast<std::uint32_t>(r.get(2)), n = static_cast<std::uint32_t>(r.get(4)),
         offset = static_cast<std::uint32_t>(r.get(4));
    auto data = r.data(r.bytes.size() - r.offset);
    if (!r.done() || !id || !n || n > max_object || !c || c > 256 ||
        c != (n + chunk_size - 1) / chunk_size || index >= c || offset != index * chunk_size ||
        offset >= n || data.size() != std::min<std::size_t>(chunk_size, n - offset)) {
        reset();
        return false;
    }
    if (transfer && (now - started > 300 || transfer != id))
        reset();
    if (!transfer) {
        transfer = id;
        total = n;
        count = c;
        started = now;
    }
    if (total != n || count != c) {
        reset();
        return false;
    }
    auto it = parts.find(index);
    if (it != parts.end() && it->second != data) {
        reset();
        return false;
    }
    parts[index] = std::move(data);
    if (parts.size() == count) {
        complete.reserve(total);
        for (auto& [i, b] : parts) {
            (void)i;
            complete.insert(complete.end(), b.begin(), b.end());
        }
        reset();
    }
    return true;
}
std::vector<Packet>
chunks(std::uint64_t session, std::uint32_t id, const std::vector<std::uint8_t>& b) {
    std::vector<Packet> out;
    if (b.empty() || b.size() > max_object || !id)
        return out;
    const auto count = (b.size() + chunk_size - 1) / chunk_size;
    for (std::size_t i = 0; i < count; ++i) {
        Writer w;
        w.put(id, 4);
        w.put(i, 2);
        w.put(count, 2);
        w.put(b.size(), 4);
        w.put(i * chunk_size, 4);
        w.data(
            std::span(b).subspan(i * chunk_size, std::min(chunk_size, b.size() - i * chunk_size)));
        out.push_back({Tag::chunk, session, std::move(w.bytes)});
    }
    return out;
}
bool Admission::accept(Input in, std::uint64_t tick) {
    if (in.held & ~modern::action_mask || in.tick < tick || in.tick - tick > 120)
        return false;
    auto it = pending.find(in.tick);
    if (it != pending.end())
        return it->second.held == in.held && it->second.sequence == in.sequence;
    pending.emplace(in.tick, in);
    return true;
}
std::uint32_t Admission::consume(std::uint64_t tick) {
    auto it = pending.find(tick);
    if (it != pending.end()) {
        held = it->second.held;
        missing = 0;
        accepted_through = tick + 1;
        pending.erase(it);
    } else if (missing < 7)
        ++missing;
    while (!pending.empty() && pending.begin()->first < tick)
        pending.erase(pending.begin());
    accepted_through = tick + 1;
    return missing > 6 ? 0 : held;
}
void Admission::clear() {
    pending.clear();
    held = missing = 0;
    accepted_through = 0;
}
} // namespace stackpulse::net::wire
