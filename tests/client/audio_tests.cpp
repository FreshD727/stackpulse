#include "../../apps/client/audio_tones.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cmath>

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

TEST_CASE("Synthesized clear and spin voices remain short quiet and tapered", "[client][audio]") {
    for (const auto& tone :
         {audio::four_line_tone(), audio::spin_tone(1), audio::spin_tone(2), audio::spin_tone(3)}) {
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
