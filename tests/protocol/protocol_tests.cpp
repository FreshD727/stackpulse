#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <stackpulse/protocol.hpp>
using namespace stackpulse::net;
using namespace stackpulse::net::wire;
TEST_CASE("SPD3 revision 2 envelope validates exact bounded lengths and tags", "[network][codec]") {
    Packet p{Tag::hello, 0, {5, 0, 0, 0, 0, 0}}, out;
    auto bytes = encode(p);
    REQUIRE(bytes.size() == 22);
    REQUIRE(decode(bytes, out));
    REQUIRE(out.body == p.body);
    for (std::size_t n = 0; n < bytes.size(); ++n)
        REQUIRE_FALSE(decode(std::span(bytes).first(n), out));
    auto bad = bytes;
    bad.push_back(0);
    REQUIRE_FALSE(decode(bad, out));
    bad = bytes;
    bad[5] = 255;
    REQUIRE_FALSE(decode(bad, out));
    REQUIRE(encode({Tag::input, 1, std::vector<std::uint8_t>(max_packet, 0)}).empty());
    std::uint32_t rng = 7;
    for (int i = 0; i < 3000; ++i) {
        rng = rng * 1664525 + 1013904223;
        std::vector<std::uint8_t> fuzz(rng % 1300);
        for (auto& b : fuzz) {
            rng = rng * 1664525 + 1013904223;
            b = static_cast<std::uint8_t>(rng >> 24);
        }
        REQUIRE_FALSE(decode(fuzz, out));
    }
}
TEST_CASE("Full snapshot assembly rejects overlap gaps conflicts and oversize",
          "[network][codec]") {
    std::vector<std::uint8_t> object(15000);
    for (std::size_t i = 0; i < object.size(); ++i)
        object[i] = static_cast<std::uint8_t>(i);
    auto packets = chunks(12, 3, object);
    Assembly a;
    std::vector<std::uint8_t> complete;
    std::reverse(packets.begin(), packets.end());
    for (auto& p : packets) {
        Reader r{p.body};
        REQUIRE(a.add(r, 4, complete));
    }
    REQUIRE(complete == object);
    auto p = packets.front();
    Reader r{p.body};
    REQUIRE(a.add(r, 5, complete));
    p.body.back() ^= 1;
    Reader conflict{p.body};
    REQUIRE_FALSE(a.add(conflict, 6, complete));
    REQUIRE(a.parts.empty());
    p = packets.front();
    p.body[12] = 1;
    Reader overlap{p.body};
    REQUIRE_FALSE(a.add(overlap, 7, complete));
    REQUIRE(chunks(1, 1, std::vector<std::uint8_t>(max_object + 1)).empty());
}
TEST_CASE("Input admission first wins bounded future missing six then neutral",
          "[network][admission]") {
    Admission a;
    REQUIRE_FALSE(a.accept({9, 1, 1}, 10));
    REQUIRE(a.accept({130, 1, 1}, 10));
    REQUIRE_FALSE(a.accept({131, 1, 1}, 10));
    REQUIRE_FALSE(a.accept({10, 1, 256}, 10));
    REQUIRE(a.accept({10, 2, 1}, 10));
    REQUIRE(a.accept({10, 2, 1}, 10));
    REQUIRE_FALSE(a.accept({10, 3, 2}, 10));
    REQUIRE(a.consume(10) == 1);
    for (std::uint64_t tick = 11; tick <= 16; ++tick)
        REQUIRE(a.consume(tick) == 1);
    REQUIRE(a.consume(17) == 0);
    REQUIRE(a.accept({18, 4, 2}, 18));
    REQUIRE(a.consume(18) == 2);
    REQUIRE(a.pending.size() <= 121);
}

TEST_CASE("M4 codec rejects old revision and assembles maximum eight-board object",
          "[network][codec]") {
    auto bytes = encode({Tag::hello, 0, {}});
    Packet out;
    bytes[4] = 1;
    REQUIRE_FALSE(decode(bytes, out));
    std::vector<std::uint8_t> object(max_object, 73);
    auto packets = chunks(1, 9, object);
    REQUIRE(packets.size() <= 1024);
    Assembly a;
    std::vector<std::uint8_t> complete;
    for (auto& p : packets) {
        Reader r{p.body};
        REQUIRE(a.add(r, 1, complete));
    }
    REQUIRE(complete == object);
}
