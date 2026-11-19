#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace stackpulse::client::audio {
inline constexpr int sample_rate = 48000;
struct Tone {
    std::array<float, 12480> samples{};
    std::size_t length{};
};

inline Tone four_line_tone() {
    Tone tone;
    tone.length = tone.samples.size();
    constexpr std::array<float, 4> notes{523.25F, 659.25F, 783.99F, 1046.5F};
    const auto note_length = tone.length / notes.size();
    float phase = 0.F;
    for (std::size_t i = 0; i < tone.length; ++i) {
        const auto note = i / note_length, position = i % note_length;
        const float progress = static_cast<float>(position) / static_cast<float>(note_length - 1);
        const float attack = std::min(1.F, static_cast<float>(position) / 96.F);
        phase += 6.28318530718F * notes[note] / static_cast<float>(sample_rate);
        tone.samples[i] = .06F * attack * (1.F - progress) * std::sin(phase);
    }
    return tone;
}

inline Tone hold_tone(bool retrieving) {
    Tone tone;
    tone.length = 4320;
    // Store descends into the slot; retrieval answers with the same two notes rising.
    constexpr std::array<float, 2> notes{440.F, 659.25F};
    const auto note_length = tone.length / notes.size();
    float phase = 0.F;
    for (std::size_t i = 0; i < tone.length; ++i) {
        const auto note = i / note_length, position = i % note_length;
        const auto pitch = retrieving ? note : notes.size() - 1 - note;
        const float progress = static_cast<float>(position) / static_cast<float>(note_length - 1);
        const float attack = std::min(1.F, static_cast<float>(position) / 96.F);
        phase += 6.28318530718F * notes[pitch] / static_cast<float>(sample_rate);
        tone.samples[i] = .045F * attack * (1.F - progress) * std::sin(phase);
    }
    return tone;
}

inline Tone spin_tone(unsigned tier) {
    Tone tone;
    tier = std::clamp(tier, 1U, 3U);
    tone.length = 6720 + 1920 * tier;
    // An unpitched crack and a falling bass drum replace the clear's ascending notes.
    // Higher tiers add delayed low impacts and a longer metallic tail, not a gain step.
    std::uint32_t noise = 0x7350494eU;
    float bass_phase = 0.F, metal_phase = 0.F;
    for (std::size_t i = 0; i < tone.length; ++i) {
        const float time = static_cast<float>(i) / static_cast<float>(sample_rate);
        const float progress = static_cast<float>(i) / static_cast<float>(tone.length - 1);
        const float attack = std::min(1.F, static_cast<float>(i) / 48.F);
        const float release = (1.F - progress) * (1.F - progress);
        bass_phase += 6.28318530718F * (54.F + 116.F * std::exp(-time * 65.F)) /
                      static_cast<float>(sample_rate);
        metal_phase +=
            6.28318530718F * (740.F - 280.F * progress) / static_cast<float>(sample_rate);
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        const float crack = static_cast<float>(noise & 65535U) / 32767.5F - 1.F;
        float impact = .075F * std::sin(bass_phase) + .027F * crack * std::exp(-time * 100.F) +
                       .012F * (std::sin(metal_phase) + .4F * std::sin(1.414214F * metal_phase));
        for (unsigned accent = 1; accent < tier; ++accent) {
            const float elapsed = time - .045F * static_cast<float>(accent);
            if (elapsed >= 0.F) {
                const float accent_attack = std::min(1.F, elapsed * 1000.F);
                impact += .04F * accent_attack * std::exp(-elapsed * 28.F) *
                          std::sin(6.28318530718F * (82.F + 21.F * accent) * elapsed);
            }
        }
        tone.samples[i] = attack * release * impact;
    }
    return tone;
}
} // namespace stackpulse::client::audio
