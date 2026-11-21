#include "../../apps/client/replay_library.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

namespace rl = stackpulse::client::replay_library;
namespace fs = std::filesystem;
namespace {
struct TemporaryLibrary {
    fs::path path = fs::temp_directory_path() / ("stackpulse-history-tests-" + rl::unique_suffix());
    TemporaryLibrary() {
        fs::create_directories(path);
    }
    ~TemporaryLibrary() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};
stackpulse::replay::Replay classic_recording() {
    return {stackpulse::classic::initial_state(42), {0, 0}};
}
rl::Clock::time_point instant(std::int64_t seconds) {
    return rl::Clock::time_point{std::chrono::seconds(seconds)};
}
} // namespace

TEST_CASE("Replay history saves unique runs and updates a run atomically",
          "[client][replay-library]") {
    TemporaryLibrary library;
    std::string error;
    auto recording = classic_recording();
    const auto first = rl::new_recording(library.path, 0, instant(1800000000));
    const auto second = rl::new_recording(library.path, 0, instant(1800000000));
    REQUIRE(first != second);
    REQUIRE(rl::save(first, recording, error));
    REQUIRE(rl::save(second, recording, error));
    recording.inputs.push_back(0);
    REQUIRE(rl::save(first, recording, error));
    const auto entries = rl::list(library.path, error);
    REQUIRE(error.empty());
    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0].path == second);
    REQUIRE(entries[0].timestamp == "2027-01-15 08:00:00 UTC");
    stackpulse::replay::Replay loaded;
    REQUIRE(stackpulse::replay::load(first, loaded, error));
    REQUIRE(loaded.inputs.size() == 3);
    REQUIRE(stackpulse::replay::load(second, loaded, error));
    REQUIRE(loaded.inputs.size() == 2);
    // Invalid replacement must leave both the previous snapshot and other entries intact.
    recording.inputs.push_back(~stackpulse::classic::action_mask);
    REQUIRE_FALSE(rl::save(first, recording, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(stackpulse::replay::load(first, loaded, error));
    REQUIRE(loaded.inputs.size() == 3);
    REQUIRE(rl::list(library.path, error).size() == 2);
}

TEST_CASE("Replay history retains one hundred runs across all modes", "[client][replay-library]") {
    TemporaryLibrary library;
    std::string error;
    const auto classic = classic_recording();
    const stackpulse::modern_replay::Replay fusion{
        stackpulse::modern::initial_state(5, stackpulse::modern::Preset::fusion), {0}};
    const stackpulse::modern_replay::Replay modern{
        stackpulse::modern::initial_state(6, stackpulse::modern::Preset::modern), {0}};
    const auto first = rl::new_recording(library.path, 0, instant(1800000000));
    REQUIRE(rl::save(first, classic, error));
    for (int index = 1; index < 103; ++index) {
        const int mode = index % 3;
        const auto path = rl::new_recording(library.path, mode, instant(1800000000 + index));
        if (mode == 0)
            REQUIRE(rl::save(path, classic, error));
        else if (mode == 1)
            REQUIRE(rl::save(path, fusion, error));
        else
            REQUIRE(rl::save(path, modern, error));
    }
    const auto entries = rl::list(library.path, error);
    REQUIRE(error.empty());
    REQUIRE(entries.size() == 100);
    REQUIRE_FALSE(fs::exists(first));
    REQUIRE(entries.front().time == 1800000102000000LL);
    REQUIRE(entries.back().time == 1800000003000000LL);
    for (std::size_t index = 1; index < entries.size(); ++index)
        REQUIRE(entries[index - 1].time > entries[index].time);
}

TEST_CASE("Legacy recordings remain selectable and count towards history capacity",
          "[client][replay-library]") {
    TemporaryLibrary library;
    std::string error;
    const auto classic = classic_recording();
    const stackpulse::modern_replay::Replay fusion{
        stackpulse::modern::initial_state(9, stackpulse::modern::Preset::fusion), {0}};
    REQUIRE(stackpulse::replay::save(library.path / "last.spcr", classic, error));
    REQUIRE(stackpulse::modern_replay::save(library.path / "last.spmr", fusion, error));
    for (int index = 0; index < 101; ++index)
        REQUIRE(rl::save(
            rl::new_recording(library.path, 0, instant(1800000000 + index)), classic, error));
    const auto entries = rl::list(library.path, error);
    REQUIRE(entries.size() == 100);
    REQUIRE(std::count_if(entries.begin(), entries.end(), [](const auto& entry) {
                return entry.legacy;
            }) == 2);
    REQUIRE(fs::exists(library.path / "last.spcr"));
    REQUIRE(fs::exists(library.path / "last.spmr"));
    stackpulse::modern_replay::Replay loaded;
    REQUIRE(stackpulse::modern_replay::load(library.path / "last.spmr", loaded, error));
    REQUIRE(loaded.initial.preset == stackpulse::modern::Preset::fusion);
}

TEST_CASE("Replay history ignores unmanaged files and reports corrupt selected entries",
          "[client][replay-library]") {
    TemporaryLibrary library;
    fs::create_directories(library.path / "replays");
    const auto unrelated = library.path / "replays" / "personal.spcr";
    std::ofstream(unrelated) << "personal recording";
    std::ofstream(library.path / "replays" / "replay-00000000000000000000-invalid-modern.spmr")
        << "unmanaged";
    const auto corrupt = rl::new_recording(library.path, 0);
    std::ofstream(corrupt) << "damaged recording";
    const auto directory = rl::new_recording(library.path, 0);
    fs::create_directories(directory);
    std::string error;
    const auto entries = rl::list(library.path, error);
    REQUIRE(error.empty());
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.front().path == corrupt);
    stackpulse::replay::Replay loaded;
    REQUIRE_FALSE(stackpulse::replay::load(corrupt, loaded, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(rl::prune(library.path, error));
    REQUIRE(fs::exists(unrelated));
    REQUIRE(fs::is_directory(directory));
}

TEST_CASE("Failed replay commit cannot prune existing history", "[client][replay-library]") {
    TemporaryLibrary library;
    std::string error;
    const auto recording = classic_recording();
    std::vector<fs::path> existing;
    // Seed an over-capacity library to prove failure does not trigger retention.
    fs::create_directories(library.path / "replays");
    for (int index = 0; index < 101; ++index) {
        existing.push_back(rl::new_recording(library.path, 0, instant(1800000000 + index)));
        REQUIRE(stackpulse::replay::save(existing.back(), recording, error));
    }
    const auto blocked = rl::new_recording(library.path, 0);
    fs::create_directories(blocked);
    REQUIRE_FALSE(rl::save(blocked, recording, error));
    REQUIRE_FALSE(error.empty());
    REQUIRE(rl::list(library.path, error).size() == 101);
    for (const auto& path : existing)
        REQUIRE(fs::exists(path));
    for (const auto& entry : fs::directory_iterator(library.path / "replays"))
        REQUIRE(entry.path().filename().string().find(".tmp-") == std::string::npos);
}
