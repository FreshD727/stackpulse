#include "../../apps/client/client_logic.hpp"
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>

namespace client = stackpulse::client;

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
