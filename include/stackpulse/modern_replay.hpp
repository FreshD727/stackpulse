#pragma once
#include <filesystem>
#include <stackpulse/modern.hpp>
#include <string>
#include <vector>
namespace stackpulse::modern_replay {
inline constexpr std::uint32_t max_frames = 1000000;
struct Replay {
    modern::State initial{};
    std::vector<std::uint32_t> inputs;
};
bool verify(const Replay&, modern::State& final_state, std::string& error);
bool encode(const Replay&, std::vector<std::uint8_t>& bytes, std::string& error);
bool decode(const std::vector<std::uint8_t>& bytes, Replay&, std::string& error);
bool save(const std::filesystem::path&, const Replay&, std::string& error);
bool load(const std::filesystem::path&, Replay&, std::string& error);
} // namespace stackpulse::modern_replay
