#pragma once
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace stackpulse::client {
enum class Cue : std::size_t { shift, soft_drop, rotate, clear, four_line_clear };

constexpr std::optional<Cue> clear_cue(unsigned lines) {
    if (lines == 4) return Cue::four_line_clear;
    if (lines >= 1 && lines <= 3) return Cue::clear;
    return std::nullopt;
}

constexpr int level_direction(int code, bool controller) {
    if (code == (controller ? SDL_GAMEPAD_BUTTON_DPAD_LEFT : SDL_SCANCODE_LEFT)) return -1;
    if (code == (controller ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT : SDL_SCANCODE_RIGHT)) return 1;
    return 0;
}

constexpr std::uint32_t adjusted_start_level(std::uint32_t level, int direction) {
    if (direction < 0 && level > 0) return level - 1;
    if (direction > 0 && level < 29) return level + 1;
    return level;
}

inline bool configure_presentation(SDL_Renderer* renderer) {
    // Fractional scaling fills the largest proportional viewport at any window
    // size, including maximized windows that fall between integer scale steps.
    return SDL_SetRenderLogicalPresentation(renderer, 640, 360, SDL_LOGICAL_PRESENTATION_LETTERBOX);
}
}
