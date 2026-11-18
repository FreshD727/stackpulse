#include "../../apps/client/client_logic.hpp"
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace client = stackpulse::client;

namespace {
bool same_color(SDL_Color a, SDL_Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}
} // namespace

TEST_CASE("Classic palettes cycle every ten levels with three piece groups", "[client][palette]") {
    using Piece = stackpulse::classic::Piece;
    constexpr std::array pieces{
        Piece::I, Piece::O, Piece::T, Piece::S, Piece::Z, Piece::J, Piece::L};
    for (std::uint32_t level = 0; level < 30; ++level) {
        CAPTURE(level);
        const auto light = client::piece_color(Piece::I, level);
        const auto first = client::piece_color(Piece::J, level);
        const auto second = client::piece_color(Piece::S, level);
        REQUIRE(same_color(light, client::piece_color(Piece::O, level)));
        REQUIRE(same_color(light, client::piece_color(Piece::T, level)));
        REQUIRE(same_color(first, client::piece_color(Piece::L, level)));
        REQUIRE(same_color(second, client::piece_color(Piece::Z, level)));
        REQUIRE_FALSE(same_color(light, first));
        REQUIRE_FALSE(same_color(light, second));
        REQUIRE_FALSE(same_color(first, second));
        for (auto piece : pieces) {
            const auto color = client::piece_color(piece, level);
            REQUIRE(color.a == 255);
            REQUIRE(same_color(color, client::piece_color(piece, level + 10)));
            const auto highlight = client::block_highlight(color);
            REQUIRE(highlight.r >= color.r);
            REQUIRE(highlight.g >= color.g);
            REQUIRE(highlight.b >= color.b);
            REQUIRE(highlight.a == 255);
        }
    }
    for (auto piece : pieces)
        REQUIRE_FALSE(same_color(client::piece_color(piece, 0), client::piece_color(piece, 1)));
    // All ten palettes differ, including the wrap from level nine to ten.
    for (std::uint32_t a = 0; a < 10; ++a)
        for (std::uint32_t b = a + 1; b < 10; ++b)
            REQUIRE_FALSE(
                same_color(client::piece_color(Piece::J, a), client::piece_color(Piece::J, b)));
    REQUIRE_FALSE(same_color(client::piece_color(Piece::J, 9), client::piece_color(Piece::J, 10)));
    const auto saturated = client::block_highlight({250, 225, 42, 255});
    REQUIRE(same_color(saturated, SDL_Color{255, 255, 77, 255}));
}

TEST_CASE("line-clear events select a distinct four-line cue", "[client][audio]") {
    REQUIRE_FALSE(client::clear_cue(0));
    for (unsigned lines = 1; lines <= 3; ++lines)
        REQUIRE(client::clear_cue(lines) == client::Cue::clear);
    REQUIRE(client::clear_cue(4) == client::Cue::four_line_clear);
    REQUIRE(client::clear_cue(4) != client::clear_cue(3));
    REQUIRE_FALSE(client::clear_cue(5));
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
    REQUIRE(client::modern_input(sample, modern::Preset::fusion) == 0);
    sample.held[static_cast<std::size_t>(in::Action::left)] = true;
    sample.held[static_cast<std::size_t>(in::Action::clockwise)] = true;
    sample.held[static_cast<std::size_t>(in::Action::pause)] = true;
    REQUIRE(client::modern_input(sample, modern::Preset::fusion) ==
            (modern::left | modern::clockwise));
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
