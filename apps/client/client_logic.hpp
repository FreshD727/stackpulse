#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stackpulse/classic.hpp>
#include <stackpulse/input.hpp>
#include <stackpulse/modern.hpp>
#include <utility>

namespace stackpulse::client {
// Original retro palettes: light pieces plus two accents, cycling every ten levels.
constexpr SDL_Color piece_color(classic::Piece piece, std::uint32_t level) {
    constexpr std::array<std::array<SDL_Color, 3>, 10> palettes{{
        {{{76, 120, 224, 255}, {88, 204, 224, 255}, {208, 232, 252, 255}}},
        {{{64, 160, 104, 255}, {160, 216, 88, 255}, {224, 244, 200, 255}}},
        {{{148, 96, 204, 255}, {220, 132, 224, 255}, {244, 216, 252, 255}}},
        {{{64, 156, 216, 255}, {112, 216, 152, 255}, {204, 244, 232, 255}}},
        {{{200, 88, 148, 255}, {112, 132, 232, 255}, {252, 212, 232, 255}}},
        {{{72, 176, 160, 255}, {156, 116, 224, 255}, {220, 228, 252, 255}}},
        {{{208, 104, 72, 255}, {232, 188, 88, 255}, {252, 228, 200, 255}}},
        {{{132, 104, 216, 255}, {104, 192, 224, 255}, {216, 220, 252, 255}}},
        {{{184, 152, 64, 255}, {184, 216, 112, 255}, {244, 244, 200, 255}}},
        {{{208, 88, 104, 255}, {232, 152, 88, 255}, {252, 216, 208, 255}}},
    }};
    const auto& palette = palettes[level % palettes.size()];
    switch (piece) {
    case classic::Piece::J:
    case classic::Piece::L:
        return palette[0];
    case classic::Piece::S:
    case classic::Piece::Z:
        return palette[1];
    default:
        return palette[2];
    }
}

// Piece enum orders differ between rulesets; never cast between them.
constexpr classic::Piece classic_palette_piece(modern::Piece piece) {
    switch (piece) {
    case modern::Piece::I:
        return classic::Piece::I;
    case modern::Piece::J:
        return classic::Piece::J;
    case modern::Piece::L:
        return classic::Piece::L;
    case modern::Piece::O:
        return classic::Piece::O;
    case modern::Piece::S:
        return classic::Piece::S;
    case modern::Piece::T:
        return classic::Piece::T;
    case modern::Piece::Z:
        return classic::Piece::Z;
    }
    return classic::Piece::I;
}
constexpr std::uint32_t modern_input(const input::TickInput& sample, modern::Preset preset) {
    std::uint32_t mask = 0;
    constexpr std::array mapping{
        std::pair{input::Action::left, modern::left},
        std::pair{input::Action::right, modern::right},
        std::pair{input::Action::down, modern::down},
        std::pair{input::Action::clockwise, modern::clockwise},
        std::pair{input::Action::counterclockwise, modern::counterclockwise}};
    for (const auto& [action, bit] : mapping)
        if (sample.held[static_cast<std::size_t>(action)])
            mask |= bit;
    if (preset == modern::Preset::modern) {
        if (sample.held[static_cast<std::size_t>(input::Action::hold)])
            mask |= modern::hold;
        if (sample.held[static_cast<std::size_t>(input::Action::drop)])
            mask |= modern::hard_drop;
    }
    return mask;
}

constexpr SDL_Color block_highlight(SDL_Color color) {
    return {static_cast<Uint8>(std::min(255, color.r + 35)),
            static_cast<Uint8>(std::min(255, color.g + 35)),
            static_cast<Uint8>(std::min(255, color.b + 35)),
            255};
}

enum class Cue : std::size_t { shift, soft_drop, rotate, clear, four_line_clear };

constexpr std::optional<Cue> clear_cue(unsigned lines) {
    if (lines == 4)
        return Cue::four_line_clear;
    if (lines >= 1 && lines <= 3)
        return Cue::clear;
    return std::nullopt;
}

constexpr int level_direction(int code, bool controller) {
    if (code == (controller ? SDL_GAMEPAD_BUTTON_DPAD_LEFT : SDL_SCANCODE_LEFT))
        return -1;
    if (code == (controller ? SDL_GAMEPAD_BUTTON_DPAD_RIGHT : SDL_SCANCODE_RIGHT))
        return 1;
    return 0;
}

constexpr std::uint32_t adjusted_start_level(std::uint32_t level, int direction) {
    if (direction < 0 && level > 0)
        return level - 1;
    if (direction > 0 && level < 29)
        return level + 1;
    return level;
}

inline bool configure_presentation(SDL_Renderer* renderer) {
    // Fractional scaling fills the largest proportional viewport at any window
    // size, including maximized windows that fall between integer scale steps.
    return SDL_SetRenderLogicalPresentation(renderer, 640, 360, SDL_LOGICAL_PRESENTATION_LETTERBOX);
}
} // namespace stackpulse::client
