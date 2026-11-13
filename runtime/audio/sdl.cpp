// mixer output through SDL3's audio stream, off Windows (audio/xaudio2.cpp there)
#ifndef _WIN32

#include <SDL3/SDL.h>

#include <chrono>
#include <thread>
#include <vector>

#include "app/app.h"
#include "audio/audio.h"
#include "core/log.h"

namespace audio {

namespace {

constexpr uint32_t kFrames = kRate / 100;  // the mixer is pulled 10 ms at a time, as XAudio2's buffers are

struct Output {
    std::function<void(float*, uint32_t)> render;
    std::vector<float> chunk = std::vector<float>(kFrames * 2);
};

// SDL's audio thread, when the device wants more: whole 10 ms chunks until it has what it asked for (bytes), the
// rest of the last one queued for the next call. Paused (app::Paused): silence, the mixer not pulled, so the title's
// sound and its sample clock stand still.
void SDLCALL Pull(void* user, SDL_AudioStream* stream, int additional, int) {
    auto* out = static_cast<Output*>(user);
    if (additional <= 0) return;
    if (app::Paused()) {
        std::vector<float> silence(size_t(additional + 7) / 8 * 2);
        SDL_PutAudioStreamData(stream, silence.data(), int(silence.size() * sizeof(float)));
        return;
    }
    for (int given = 0; given < additional; given += int(kFrames * 8)) {
        out->render(out->chunk.data(), kFrames);
        SDL_PutAudioStreamData(stream, out->chunk.data(), int(kFrames * 8));
    }
}

}  // namespace

void StartOutput(std::function<void(float*, uint32_t)> render) {
    static Output out;  // lives as long as the stream, which is never closed
    out.render = std::move(render);
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "480");
    SDL_AudioStream* stream = nullptr;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, int(kRate)};
        // the default device: SDL follows it to another when it changes or goes away (a headset unplugged)
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Pull, &out);
    }
    if (stream && SDL_ResumeAudioStreamDevice(stream)) {
        LOGI("audio: SDL %s output, 48 kHz stereo", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "");
        return;
    }
    // no output device (nothing plugged in, no sound server): the mixer is still pulled in real time and the samples
    // dropped, so the title's sample clock runs and what waits on it (a movie's sound) goes on
    LOGE("audio: SDL audio unavailable, no sound (the mixer runs silent): %s", SDL_GetError());
    std::thread([] {
        auto next = std::chrono::steady_clock::now();
        for (;;) {
            if (app::Paused()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                next = std::chrono::steady_clock::now();
                continue;
            }
            out.render(out.chunk.data(), kFrames);
            next += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(next);
        }
    }).detach();
}

}  // namespace audio

#endif  // !_WIN32
