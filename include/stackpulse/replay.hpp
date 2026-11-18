#pragma once
#include <stackpulse/classic.hpp>
#include <filesystem>
#include <string>
#include <vector>
namespace stackpulse::replay {
inline constexpr std::uint32_t max_frames = 1000000;
struct Replay { classic::State initial{}; std::vector<std::uint32_t> inputs; };
bool verify(const Replay&, classic::State& final_state, std::string& error);
bool encode(const Replay&, std::vector<std::uint8_t>& bytes, std::string& error);
bool decode(const std::vector<std::uint8_t>& bytes, Replay&, std::string& error);
bool save(const std::filesystem::path&, const Replay&, std::string& error);
bool load(const std::filesystem::path&, Replay&, std::string& error);
}
