#pragma once
#include "audio_tones.hpp"
#include "client_logic.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

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
        tones_[static_cast<std::size_t>(Cue::four_line_clear)] =
            stackpulse::client::audio::four_line_tone();
        make_tone(static_cast<std::size_t>(Cue::hard_drop), 4800, 180.F, 48.F);
        tones_[static_cast<std::size_t>(Cue::spin_single)] =
            stackpulse::client::audio::spin_tone(1);
        tones_[static_cast<std::size_t>(Cue::spin_double)] =
            stackpulse::client::audio::spin_tone(2);
        tones_[static_cast<std::size_t>(Cue::spin_triple)] =
            stackpulse::client::audio::spin_tone(3);
        tones_[static_cast<std::size_t>(Cue::hold_store)] =
            stackpulse::client::audio::hold_tone(false);
        tones_[static_cast<std::size_t>(Cue::hold_retrieve)] =
            stackpulse::client::audio::hold_tone(true);
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
                    "tiers / hold store / hold retrieval\n");
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
    static constexpr int sample_rate = stackpulse::client::audio::sample_rate;
    using Tone = stackpulse::client::audio::Tone;
    struct Voice {
        std::size_t tone{}, position{};
        bool active{};
    };
    // Keep the immutable PCM bank off the small Windows main-thread stack.
    // It is allocated once before the audio callback starts and never resized.
    std::vector<Tone> tones_ = std::vector<Tone>(static_cast<std::size_t>(Cue::hold_retrieve) + 1);
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
