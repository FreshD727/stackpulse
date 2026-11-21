#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stackpulse/modern_replay.hpp>
#include <stackpulse/replay.hpp>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace stackpulse::client::replay_library {
inline constexpr std::size_t capacity = 100;
using Clock = std::chrono::system_clock;
struct Entry {
    std::filesystem::path path;
    std::string timestamp;
    int mode{}; // Classic=0, Fusion=1, Modern=2; legacy SPMR=-1 until loaded.
    bool legacy{};
    std::int64_t time{};
};
inline std::string unique_suffix() {
    static const auto nonce = static_cast<std::uint64_t>(std::random_device{}()) << 32 |
                              static_cast<std::uint64_t>(std::random_device{}());
    static std::atomic<std::uint64_t> sequence{};
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << nonce << std::setw(16)
        << sequence.fetch_add(1);
    return out.str();
}
// Allocate once when a run starts. Autosaves of that run must reuse this path.
inline std::filesystem::path new_recording(const std::filesystem::path& directory,
                                           int mode,
                                           Clock::time_point now = Clock::now()) {
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    std::ostringstream name;
    name << "replay-" << std::setfill('0') << std::setw(20) << micros << '-' << unique_suffix()
         << '-'
         << (mode == 0   ? "classic.spcr"
             : mode == 1 ? "fusion.spmr"
                         : "modern.spmr");
    return directory / "replays" / name.str();
}
inline std::string format_time(std::int64_t micros) {
    const auto seconds = static_cast<std::time_t>(micros / 1000000);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%d %H:%M:%S UTC");
    return out.str();
}
inline bool managed_entry(const std::filesystem::path& path, Entry& entry) {
    const auto name = path.filename().string();
    // Strict ownership protects unrelated recordings in this directory from pruning.
    if (name.size() < 69 || name.substr(0, 7) != "replay-" || name[27] != '-' || name[60] != '-')
        return false;
    if (!std::all_of(
            name.begin() + 7, name.begin() + 27, [](char c) { return c >= '0' && c <= '9'; }) ||
        !std::all_of(name.begin() + 28, name.begin() + 60, [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        }))
        return false;
    const auto tail = name.substr(61);
    if (tail != "classic.spcr" && tail != "fusion.spmr" && tail != "modern.spmr")
        return false;
    try {
        entry.time = std::stoll(name.substr(7, 20));
    } catch (...) {
        return false;
    }
    entry.path = path;
    entry.timestamp = format_time(entry.time);
    entry.mode = tail == "classic.spcr" ? 0 : tail == "fusion.spmr" ? 1 : 2;
    return true;
}
inline std::vector<Entry> list(const std::filesystem::path& directory, std::string& error) {
    error.clear();
    std::vector<Entry> entries;
    std::error_code ec;
    const auto history = directory / "replays";
    if (std::filesystem::exists(history, ec)) {
        std::filesystem::directory_iterator iterator(history, ec), end;
        if (ec) {
            error = "cannot list replay history: " + ec.message();
            return entries;
        }
        for (; iterator != end; iterator.increment(ec)) {
            if (ec)
                break;
            Entry entry;
            // Never follow symlinks while discovering or pruning managed history.
            if (iterator->symlink_status(ec).type() == std::filesystem::file_type::regular &&
                managed_entry(iterator->path(), entry))
                entries.push_back(std::move(entry));
            if (ec)
                break;
        }
        if (ec)
            error = "cannot list replay history: " + ec.message();
    } else if (ec)
        error = "cannot access replay history: " + ec.message();
    // Preserve pre-history recordings in place and make both formats discoverable.
    for (const auto* name : {"last.spcr", "last.spmr"}) {
        const auto path = directory / name;
        ec.clear();
        if (std::filesystem::symlink_status(path, ec).type() != std::filesystem::file_type::regular)
            continue;
        const auto written = std::filesystem::last_write_time(path, ec);
        if (ec)
            continue;
#ifdef _MSC_VER
        const auto system_written = std::chrono::clock_cast<Clock>(written);
#else
        const auto system_written = std::chrono::file_clock::to_sys(written);
#endif
        const auto time =
            std::chrono::duration_cast<std::chrono::microseconds>(system_written.time_since_epoch())
                .count();
        entries.push_back(
            {path, format_time(time), path.extension() == ".spcr" ? 0 : -1, true, time});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.time != b.time ? a.time > b.time : a.path.filename() > b.path.filename();
    });
    return entries;
}
inline bool prune(const std::filesystem::path& directory, std::string& error) {
    auto entries = list(directory, error);
    if (!error.empty())
        return false;
    auto count = entries.size();
    for (auto it = entries.rbegin(); it != entries.rend() && count > capacity; ++it) {
        if (it->legacy)
            continue;
        std::error_code ec;
        if (!std::filesystem::remove(it->path, ec) || ec) {
            error = "replay saved; cannot prune old history: " + ec.message();
            return false;
        }
        --count;
    }
    return true;
}
template <class Replay>
inline bool save(const std::filesystem::path& path, const Replay& recording, std::string& error) {
    error.clear();
    std::vector<std::uint8_t> bytes;
    // ADL dispatches to the matching ruleset and validates before touching existing files.
    if (!encode(recording, bytes, error))
        return false;
    Entry entry;
    if (!managed_entry(path, entry)) {
        error = "invalid managed replay path";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "cannot create replay history: " + ec.message();
        return false;
    }
    auto temporary = path;
    temporary += ".tmp-" + unique_suffix();
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        stream.close();
        if (!stream) {
            std::filesystem::remove(temporary, ec);
            error = "cannot write replay history";
            return false;
        }
    }
#ifdef _WIN32
    const bool replaced = MoveFileExW(temporary.c_str(),
                                      path.c_str(),
                                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!replaced)
        error = "cannot commit replay history: " + std::to_string(GetLastError());
#else
    std::filesystem::rename(temporary, path, ec);
    const bool replaced = !ec;
    if (!replaced)
        error = "cannot commit replay history: " + ec.message();
#endif
    if (!replaced) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    // Retention happens only after a complete replay has been atomically committed.
    return prune(path.parent_path().parent_path(), error);
}
} // namespace stackpulse::client::replay_library
