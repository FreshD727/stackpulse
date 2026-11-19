#pragma once
#include "client_logic.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>

// Original, quiet cues synthesized locally; no external audio assets or music.
// The main thread creates voices under the stream lock; the callback advances
// them while SDL holds that same lock during mix().
class ClientAudio {
  public:
    using Cue = stackpulse::client::Cue;
    ClientAudio() = default;
    ClientAudio(const ClientAudio&) = delete;
    ClientAudio& operator=(const ClientAudio&) = delete;
    ~ClientAudio() {
        shutdown();
    }

    void initialize(bool muted) {
        if (muted) {
            std::printf("Audio: muted (--mute)\n");
            return;
        }
        make_tone(0, 960, 230.F, 150.F);
        make_tone(1, 768, 150.F, 95.F);
        make_tone(2, 2016, 540.F, 860.F);
        make_tone(3, 7200, 420.F, 1050.F);
        make_four_line_tone();
        make_tone(static_cast<std::size_t>(Cue::hard_drop), 4800, 180.F, 48.F);
        make_spin_tone(Cue::spin_single, 1);
        make_spin_tone(Cue::spin_double, 2);
        make_spin_tone(Cue::spin_triple, 3);
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            unavailable();
            return;
        }
        initialized_ = true;
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, sample_rate};
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, mix, this);
        if (!stream_) {
            unavailable();
            shutdown();
            return;
        }
        std::printf("Audio: synthesized movement / soft drop / hard drop / rotation / clear / spin "
                    "tiers\n");
    }

    void set_playing(bool playing) {
        if (!stream_ || playing == playing_)
            return;
        if (playing) {
            if (!SDL_ResumeAudioStreamDevice(stream_)) {
                unavailable();
                shutdown();
                return;
            }
        } else {
            SDL_PauseAudioStreamDevice(stream_);
            if (SDL_LockAudioStream(stream_)) {
                voices_ = {};
                SDL_ClearAudioStream(stream_);
                SDL_UnlockAudioStream(stream_);
            }
        }
        playing_ = playing;
    }

    void play(Cue cue) {
        if (!stream_ || !playing_ || !SDL_LockAudioStream(stream_))
            return;
        // Bounded polyphony: even catch-up ticks cannot grow a queue of sounds.
        voices_[next_voice_] = {static_cast<std::size_t>(cue), 0, true};
        next_voice_ = (next_voice_ + 1) % voices_.size();
        SDL_UnlockAudioStream(stream_);
    }

    void shutdown() {
        if (stream_) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        if (initialized_) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            initialized_ = false;
        }
        playing_ = false;
    }

  private:
    static constexpr int sample_rate = 48000;
    struct Tone {
        std::array<float, 12480> samples{};
        std::size_t length{};
    };
    struct Voice {
        std::size_t tone{}, position{};
        bool active{};
    };
    std::array<Tone, 9> tones_{};
    std::array<Voice, 8> voices_{};
    std::size_t next_voice_{};
    SDL_AudioStream* stream_{};
    bool initialized_{}, playing_{};

    static void unavailable() {
        std::fprintf(stderr, "Audio unavailable; continuing silently: %s\n", SDL_GetError());
    }
    void make_tone(std::size_t index, std::size_t length, float from, float to) {
        auto& tone = tones_[index];
        tone.length = length;
        float phase = 0.F;
        for (std::size_t i = 0; i < length; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(length - 1);
            const float attack = std::min(1.F, static_cast<float>(i) / 96.F);
            const float release = 1.F - progress;
            phase +=
                6.28318530718F * (from + (to - from) * progress) / static_cast<float>(sample_rate);
            tone.samples[i] = .045F * attack * release * release *
                              (std::sin(phase) + .16F * std::sin(2.F * phase));
        }
    }

    void make_four_line_tone() {
        auto& tone = tones_[static_cast<std::size_t>(Cue::four_line_clear)];
        tone.length = tone.samples.size();
        constexpr std::array<float, 4> notes{523.25F, 659.25F, 783.99F, 1046.5F};
        const auto note_length = tone.length / notes.size();
        float phase = 0.F;
        for (std::size_t i = 0; i < tone.length; ++i) {
            const auto note = i / note_length, position = i % note_length;
            const float progress =
                static_cast<float>(position) / static_cast<float>(note_length - 1);
            const float attack = std::min(1.F, static_cast<float>(position) / 96.F);
            phase += 6.28318530718F * notes[note] / static_cast<float>(sample_rate);
            tone.samples[i] = .06F * attack * (1.F - progress) * std::sin(phase);
        }
    }

    void make_spin_tone(Cue cue, unsigned tier) {
        auto& tone = tones_[static_cast<std::size_t>(cue)];
        // Each tier adds an ascending note and a brighter, stronger final chord.
        // These short original synth phrases stay bounded and callback allocation-free.
        constexpr std::array<float, 5> notes{523.25F, 659.25F, 783.99F, 1046.5F, 1318.51F};
        const auto count = static_cast<std::size_t>(tier + 2);
        tone.length = 5280 + 2400 * tier;
        const auto note_length = tone.length / count;
        float phase = 0.F;
        for (std::size_t i = 0; i < tone.length; ++i) {
            const auto note = std::min(i / note_length, count - 1);
            const auto position = i - note * note_length;
            const auto length = note + 1 == count ? tone.length - note * note_length : note_length;
            const float progress = static_cast<float>(position) / static_cast<float>(length - 1);
            const float attack = std::min(1.F, static_cast<float>(position) / 96.F);
            phase += 6.28318530718F * notes[note] / static_cast<float>(sample_rate);
            const float harmonics = std::sin(phase) + .12F * tier * std::sin(2.F * phase) +
                                    (note + 1 == count ? .22F * std::sin(1.5F * phase) : 0.F);
            tone.samples[i] = (.045F + .008F * tier) * attack * (1.F - progress) * harmonics;
        }
    }

    static void SDLCALL mix(void* context, SDL_AudioStream* stream, int additional, int) {
        auto& self = *static_cast<ClientAudio*>(context);
        // Supply only immediately requested PCM. No sleeps, application allocation,
        // file I/O, or waiting for sounds to finish on the simulation thread.
        int remaining = std::clamp(additional / static_cast<int>(sizeof(float)), 0, 4096);
        std::array<float, 512> buffer{};
        while (remaining > 0) {
            const int count = std::min(remaining, static_cast<int>(buffer.size()));
            std::fill(buffer.begin(), buffer.end(), 0.F);
            for (auto& voice : self.voices_) {
                if (!voice.active)
                    continue;
                const auto& tone = self.tones_[voice.tone];
                for (int i = 0; i < count && voice.position < tone.length; ++i)
                    buffer[static_cast<std::size_t>(i)] += tone.samples[voice.position++];
                if (voice.position == tone.length)
                    voice.active = false;
            }
            if (!SDL_PutAudioStreamData(
                    stream, buffer.data(), count * static_cast<int>(sizeof(float))))
                return;
            remaining -= count;
        }
    }
};
