#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <optional>
#include <stackpulse/classic.hpp>
#include <stackpulse/input.hpp>
#include <stackpulse/modern.hpp>
#include <string>
#include <utility>

namespace stackpulse::client {
// Owns the saved snapshot through its task, including failed saves awaiting retry.
// Completion never clears input recorded after a snapshot was submitted.
class SaveWorker {
    std::future<std::string> job_;
    std::function<std::string()> snapshot_;
    std::optional<std::string> launch_error_;
    bool dirty_{};
    void launch() {
        try {
            job_ = std::async(std::launch::async, snapshot_);
        } catch (const std::exception& error) {
            launch_error_ = error.what();
        } catch (...) {
            launch_error_ = "unknown save launch failure";
        }
    }

  public:
    void set_dirty(bool value) {
        dirty_ = value;
    }
    bool dirty() const {
        return dirty_;
    }
    bool pending() const {
        return job_.valid();
    }
    bool retry_needed() const {
        return !pending() && static_cast<bool>(snapshot_);
    }
    bool ready_to_load() const {
        return !pending() && !snapshot_ && !dirty_;
    }
    bool submit(std::function<std::string()> snapshot) {
        if (pending() || snapshot_)
            return false;
        snapshot_ = std::move(snapshot);
        dirty_ = false;
        launch();
        return true;
    }
    void retry() {
        if (retry_needed())
            launch();
    }
    std::optional<std::string> poll(bool wait = false) {
        if (launch_error_)
            return std::exchange(launch_error_, std::nullopt);
        if (!pending() ||
            (!wait && job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready))
            return std::nullopt;
        std::string error;
        try {
            error = job_.get();
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown replay save failure";
        }
        if (error.empty())
            snapshot_ = {};
        return error;
    }
};

// Key the palette by the actual gravity interval, so speed plateaus retain colors.
constexpr SDL_Color piece_color(classic::Piece piece, std::uint32_t gravity_interval) {
    constexpr std::array<SDL_Color, 7> palette{{{72, 196, 220, 255},
                                                {232, 196, 72, 255},
                                                {176, 104, 224, 255},
                                                {96, 204, 116, 255},
                                                {224, 88, 108, 255},
                                                {88, 120, 224, 255},
                                                {232, 148, 72, 255}}};
    return palette[(static_cast<std::size_t>(piece) + gravity_interval) % palette.size()];
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
    if (preset != modern::Preset::classic &&
        sample.held[static_cast<std::size_t>(input::Action::hold)])
        mask |= modern::hold;
    if (preset == modern::Preset::modern &&
        sample.held[static_cast<std::size_t>(input::Action::drop)])
        mask |= modern::hard_drop;
    return mask;
}

constexpr SDL_Color block_highlight(SDL_Color color) {
    return {static_cast<Uint8>(std::min(255, color.r + 35)),
            static_cast<Uint8>(std::min(255, color.g + 35)),
            static_cast<Uint8>(std::min(255, color.b + 35)),
            255};
}

enum class Cue : std::size_t {
    shift,
    soft_drop,
    rotate,
    clear,
    four_line_clear,
    hard_drop,
    spin_single,
    spin_double,
    spin_triple,
    hold_store,
    hold_retrieve,
    locked,
    garbage_warning,
    garbage_hit
};

// Only an accepted hold emits a cue; occupancy must be sampled before stepping.
constexpr std::optional<Cue> hold_cue(bool accepted, bool previously_occupied) {
    if (!accepted)
        return std::nullopt;
    return previously_occupied ? Cue::hold_retrieve : Cue::hold_store;
}

constexpr std::optional<Cue> clear_cue(unsigned lines, bool spin = false) {
    if (spin && lines >= 1 && lines <= 3)
        return lines == 1 ? Cue::spin_single : lines == 2 ? Cue::spin_double : Cue::spin_triple;
    if (lines == 4)
        return Cue::four_line_clear;
    if (lines >= 1 && lines <= 3)
        return Cue::clear;
    return std::nullopt;
}

constexpr int level_direction(int code, bool controller) {
    if (code == (controller ? static_cast<int>(SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                            : static_cast<int>(SDL_SCANCODE_LEFT)))
        return -1;
    if (code == (controller ? static_cast<int>(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                            : static_cast<int>(SDL_SCANCODE_RIGHT)))
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
