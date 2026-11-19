#include "../../apps/client/client_logic.hpp"
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <stdexcept>

namespace client = stackpulse::client;

namespace {
bool same_color(SDL_Color a, SDL_Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
} // namespace

TEST_CASE("Every gravity palette has seven distinct piece colors", "[client][palette]") {
    using Piece = stackpulse::classic::Piece;
    constexpr std::array pieces{
        Piece::I, Piece::O, Piece::T, Piece::S, Piece::Z, Piece::J, Piece::L};
    for (std::uint32_t interval = 1; interval <= 60; ++interval) {
        CAPTURE(interval);
        for (std::size_t a = 0; a < pieces.size(); ++a) {
            const auto color = client::piece_color(pieces[a], interval);
            REQUIRE(color.a == 255);
            for (std::size_t b = a + 1; b < pieces.size(); ++b)
                REQUIRE_FALSE(same_color(color, client::piece_color(pieces[b], interval)));
        }
    }
    const auto saturated = client::block_highlight({250, 225, 42, 255});
    REQUIRE(same_color(saturated, SDL_Color{255, 255, 77, 255}));
}

TEST_CASE("Classic and Fusion speed plateaus keep the palette", "[client][palette]") {
    namespace classic = stackpulse::classic;
    namespace modern = stackpulse::modern;
    for (std::uint32_t level = 0; level < 40; ++level) {
        CAPTURE(level);
        const auto before = classic::gravity_frames(level);
        const auto after = classic::gravity_frames(level + 1);
        if (before == after)
            REQUIRE(same_color(client::piece_color(classic::Piece::T, before),
                               client::piece_color(classic::Piece::T, after)));
        if (level <= 29) {
            const auto fusion = modern::initial_state(1, modern::Preset::fusion, level);
            REQUIRE(modern::gravity_interval(fusion) == before);
        }
    }
    REQUIRE_FALSE(same_color(client::piece_color(classic::Piece::T, classic::gravity_frames(9)),
                             client::piece_color(classic::Piece::T, classic::gravity_frames(10))));
    // Modern also keys colors to its clamped interval, rather than a line-level counter.
    auto modern_state = modern::initial_state(1);
    modern_state.lines = 1000;
    const auto capped = modern::gravity_interval(modern_state);
    modern_state.lines += 10;
    REQUIRE(modern::gravity_interval(modern_state) == capped);
}

TEST_CASE("line-clear events select a distinct four-line cue", "[client][audio]") {
    REQUIRE_FALSE(client::clear_cue(0));
    for (unsigned lines = 1; lines <= 3; ++lines)
        REQUIRE(client::clear_cue(lines) == client::Cue::clear);
    REQUIRE(client::clear_cue(4) == client::Cue::four_line_clear);
    REQUIRE(client::clear_cue(4) != client::clear_cue(3));
    REQUIRE_FALSE(client::clear_cue(5));
    REQUIRE(client::clear_cue(1, true) == client::Cue::spin_single);
    REQUIRE(client::clear_cue(2, true) == client::Cue::spin_double);
    REQUIRE(client::clear_cue(3, true) == client::Cue::spin_triple);
    REQUIRE_FALSE(client::clear_cue(0, true));
}

TEST_CASE("keyboard and controller can adjust start level in both directions",
          "[client][settings]") {
    for (bool controller : {false, true}) {
        const int left =
            controller ? static_cast<int>(SDL_GAMEPAD_BUTTON_DPAD_LEFT) : SDL_SCANCODE_LEFT;
        const int right =
            controller ? static_cast<int>(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) : SDL_SCANCODE_RIGHT;
        REQUIRE(client::level_direction(left, controller) == -1);
        REQUIRE(client::level_direction(right, controller) == 1);
        REQUIRE(client::adjusted_start_level(15, client::level_direction(left, controller)) == 14);
        REQUIRE(client::adjusted_start_level(15, client::level_direction(right, controller)) == 16);
    }
    REQUIRE(client::level_direction(SDL_SCANCODE_UP, false) == 0);
    REQUIRE(client::level_direction(SDL_GAMEPAD_BUTTON_DPAD_UP, true) == 0);
    REQUIRE(client::adjusted_start_level(0, -1) == 0);
    REQUIRE(client::adjusted_start_level(29, 1) == 29);
    REQUIRE(client::adjusted_start_level(0, 1) == 1);
    REQUIRE(client::adjusted_start_level(29, -1) == 28);
    REQUIRE(client::adjusted_start_level(15, 0) == 15);
}

TEST_CASE("resized presentation uses the largest proportional viewport", "[client][layout]") {
    struct Viewport {
        int width, height;
        float x, y, w, h;
    };
    // Noninteger 16:9 sizes previously left unused borders on every edge.
    // Other aspect ratios retain only the bars needed to avoid distortion.
    constexpr std::array<Viewport, 7> cases{{{640, 360, 0, 0, 640, 360},
                                             {1280, 720, 0, 0, 1280, 720},
                                             {1536, 864, 0, 0, 1536, 864},
                                             {1920, 1080, 0, 0, 1920, 1080},
                                             {1920, 1040, 36, 0, 1848, 1040},
                                             {1920, 1200, 0, 60, 1920, 1080},
                                             {2560, 1080, 320, 0, 1920, 1080}}};
    for (const auto& viewport : cases) {
        CAPTURE(viewport.width, viewport.height);
        std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
            SDL_CreateSurface(viewport.width, viewport.height, SDL_PIXELFORMAT_RGBA8888),
            SDL_DestroySurface);
        REQUIRE(surface);
        std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(
            SDL_CreateSoftwareRenderer(surface.get()), SDL_DestroyRenderer);
        REQUIRE(renderer);
        REQUIRE(client::configure_presentation(renderer.get()));
        SDL_FRect actual{};
        REQUIRE(SDL_GetRenderLogicalPresentationRect(renderer.get(), &actual));
        REQUIRE(actual.x == Catch::Approx(viewport.x));
        REQUIRE(actual.y == Catch::Approx(viewport.y));
        REQUIRE(actual.w == Catch::Approx(viewport.w));
        REQUIRE(actual.h == Catch::Approx(viewport.h));
    }
}

TEST_CASE("Modern input translates platform drop before hold without swapping bits",
          "[client][input]") {
    namespace in = stackpulse::input;
    namespace modern = stackpulse::modern;
    in::TickInput sample;
    sample.held[static_cast<std::size_t>(in::Action::drop)] = true;
    REQUIRE(client::modern_input(sample, modern::Preset::modern) == modern::hard_drop);
    REQUIRE(client::modern_input(sample, modern::Preset::fusion) == 0);
    sample.held[static_cast<std::size_t>(in::Action::drop)] = false;
    sample.held[static_cast<std::size_t>(in::Action::hold)] = true;
    REQUIRE(client::modern_input(sample, modern::Preset::modern) == modern::hold);
    REQUIRE(client::modern_input(sample, modern::Preset::fusion) == modern::hold);
    sample.held[static_cast<std::size_t>(in::Action::left)] = true;
    sample.held[static_cast<std::size_t>(in::Action::clockwise)] = true;
    sample.held[static_cast<std::size_t>(in::Action::pause)] = true;
    REQUIRE(client::modern_input(sample, modern::Preset::fusion) ==
            (modern::left | modern::clockwise | modern::hold));
}

TEST_CASE("Modern pieces use the named Classic palette groups", "[client][palette]") {
    namespace modern = stackpulse::modern;
    namespace classic = stackpulse::classic;
    constexpr std::array pairs{std::pair{modern::Piece::I, classic::Piece::I},
                               std::pair{modern::Piece::J, classic::Piece::J},
                               std::pair{modern::Piece::L, classic::Piece::L},
                               std::pair{modern::Piece::O, classic::Piece::O},
                               std::pair{modern::Piece::S, classic::Piece::S},
                               std::pair{modern::Piece::T, classic::Piece::T},
                               std::pair{modern::Piece::Z, classic::Piece::Z}};
    for (const auto& [modern_piece, classic_piece] : pairs)
        REQUIRE(client::classic_palette_piece(modern_piece) == classic_piece);
}

TEST_CASE("Replay saves gate loads and preserve newer dirty input", "[client][replay]") {
    client::SaveWorker saves;
    saves.set_dirty(true);
    std::promise<void> release;
    auto gate = release.get_future().share();
    REQUIRE(saves.submit([gate] {
        gate.wait();
        return std::string{};
    }));
    CHECK(saves.pending());
    CHECK_FALSE(saves.dirty());
    CHECK_FALSE(saves.ready_to_load());
    CHECK_FALSE(saves.poll()); // Must return while the controlled worker is blocked.
    CHECK_FALSE(saves.submit([] { return std::string{}; }));
    saves.set_dirty(true); // The player records more input while the snapshot saves.
    release.set_value();
    const auto result = saves.poll(true);
    REQUIRE(result);
    REQUIRE(result->empty());
    REQUIRE(saves.dirty());
    REQUIRE_FALSE(saves.ready_to_load());
    REQUIRE(saves.submit([] { return std::string{}; }));
    REQUIRE(saves.poll(true)->empty());
    REQUIRE(saves.ready_to_load());
}

TEST_CASE("Replay save exceptions retain the snapshot for serialized retry", "[client][replay]") {
    client::SaveWorker saves;
    auto attempts = std::make_shared<unsigned>(0);
    saves.set_dirty(true);
    REQUIRE(saves.submit([attempts]() -> std::string {
        if (++*attempts == 1)
            throw std::runtime_error("disk failure");
        return {};
    }));
    REQUIRE(saves.poll(true) == std::optional<std::string>{"disk failure"});
    REQUIRE(saves.retry_needed());
    REQUIRE_FALSE(saves.ready_to_load());
    REQUIRE_FALSE(saves.submit([] { return std::string{}; }));
    saves.set_dirty(true);
    saves.retry();
    REQUIRE(saves.poll(true)->empty());
    REQUIRE(*attempts == 2);
    REQUIRE(saves.dirty());
    REQUIRE_FALSE(saves.ready_to_load());
}
