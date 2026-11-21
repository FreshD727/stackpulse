#include "../../apps/client/audio_tones.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>

namespace audio = stackpulse::client::audio;
namespace {
double energy(const audio::Tone& tone) {
    double result = 0.;
    for (std::size_t i = 0; i < tone.length; ++i)
        result += static_cast<double>(tone.samples[i]) * tone.samples[i];
    return result;
}
double bass_fraction(const audio::Tone& tone) {
    // A 250 Hz low-pass measures the intended bass impact without depending on a device.
    double low = 0., result = 0.;
    for (std::size_t i = 0; i < tone.length; ++i) {
        low += .0322 * (tone.samples[i] - low);
        result += low * low;
    }
    return result / energy(tone);
}
} // namespace

TEST_CASE("Spin cues use bass percussion distinct from the four-clear melody", "[client][audio]") {
    const auto clear = audio::four_line_tone();
    REQUIRE(bass_fraction(clear) < .3);
    double previous_energy = 0.;
    std::size_t previous_length = 0;
    for (unsigned tier = 1; tier <= 3; ++tier) {
        CAPTURE(tier);
        const auto spin = audio::spin_tone(tier);
        REQUIRE(bass_fraction(spin) > .6);
        REQUIRE(energy(spin) > previous_energy);
        REQUIRE(spin.length > previous_length);
        previous_energy = energy(spin);
        previous_length = spin.length;
        REQUIRE(spin.samples == audio::spin_tone(tier).samples);
    }
}

TEST_CASE("Synthesized clear spin and hold voices remain short quiet and tapered",
          "[client][audio]") {
    // Construct one heap-backed PCM buffer at a time. An initializer list of all
    // nine buffers plus its temporaries exhausts the Windows test thread stack.
    using Factory = audio::Tone (*)();
    const std::array<Factory, 9> factories{audio::four_line_tone,
                                           [] { return audio::spin_tone(1); },
                                           [] { return audio::spin_tone(2); },
                                           [] { return audio::spin_tone(3); },
                                           [] { return audio::hold_tone(false); },
                                           [] { return audio::hold_tone(true); },
                                           audio::lock_tone,
                                           [] { return audio::garbage_tone(false); },
                                           [] { return audio::garbage_tone(true); }};
    for (const auto factory : factories) {
        const auto buffer = std::make_unique<audio::Tone>(factory());
        const auto& tone = *buffer;
        REQUIRE(tone.length <= tone.samples.size());
        REQUIRE(tone.length <= static_cast<std::size_t>(audio::sample_rate * .26));
        REQUIRE(tone.samples.front() == 0.F);
        REQUIRE(tone.samples[tone.length - 1] == 0.F);
        for (float sample : tone.samples) {
            REQUIRE(std::isfinite(sample));
            // Eight simultaneous voices remain below full scale even at this bound.
            REQUIRE(std::abs(sample) <= .12F);
        }
        for (std::size_t i = tone.length; i < tone.samples.size(); ++i)
            REQUIRE(tone.samples[i] == 0.F);
    }
}

TEST_CASE("Lock incoming warning and garbage impact have distinct audible voices",
          "[client][audio]") {
    const auto lock = audio::lock_tone();
    const auto warning = audio::garbage_tone(false);
    const auto hit = audio::garbage_tone(true);
    REQUIRE(energy(lock) > 0.);
    REQUIRE(energy(warning) > 0.);
    REQUIRE(energy(hit) > 0.);
    REQUIRE(bass_fraction(lock) > .6);
    REQUIRE(bass_fraction(hit) > .6);
    REQUIRE(bass_fraction(warning) < .3);
    REQUIRE(lock.length < hit.length);
    REQUIRE(lock.samples != hit.samples);
}

TEST_CASE("Hold store and retrieval use distinct opposing note orders", "[client][audio][hold]") {
    const auto store = audio::hold_tone(false);
    const auto retrieve = audio::hold_tone(true);
    REQUIRE(store.length == retrieve.length);
    REQUIRE(energy(store) > 0.);
    REQUIRE(energy(retrieve) > 0.);
    REQUIRE(store.samples != retrieve.samples);
    const auto half = store.length / 2;
    auto crossings = [half](const audio::Tone& tone, std::size_t start) {
        unsigned count = 0;
        for (std::size_t i = start + 1; i < start + half; ++i)
            if (tone.samples[i - 1] < 0.F && tone.samples[i] >= 0.F)
                ++count;
        return count;
    };
    REQUIRE(crossings(store, 0) > crossings(store, half));
    REQUIRE(crossings(retrieve, 0) < crossings(retrieve, half));
}
