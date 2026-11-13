// mixer output through XAudio2
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "app/app.h"
#include "audio/audio.h"
#include "core/log.h"

namespace audio {

namespace {

// the output device gone (a headset unplugged): XAudio2 stops taking buffers, so the mixer is pulled silent as
// without a device, or the title's sample clock (and a movie waiting on it) would stand still
struct EngineErrors final : IXAudio2EngineCallback {
    std::atomic<bool> lost{false};
    void STDMETHODCALLTYPE OnProcessingPassStart() noexcept override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT error) noexcept override {
        LOGR("audio: XAudio2 critical error %08lX (the output device is gone?), the mixer runs silent", (unsigned long)error);
        lost = true;
    }
};

}  // namespace

void StartOutput(std::function<void(float*, uint32_t)> render) {
    std::thread([render] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        IXAudio2* xa = nullptr;
        IXAudio2MasteringVoice* master = nullptr;
        IXAudio2SourceVoice* voice = nullptr;
        static EngineErrors errors;  // lives as long as the engine, which is never released
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wf.nChannels = 2;
        wf.nSamplesPerSec = kRate;
        wf.wBitsPerSample = 32;
        wf.nBlockAlign = 8;
        wf.nAvgBytesPerSec = kRate * 8;
        bool started = SUCCEEDED(XAudio2Create(&xa, 0, XAUDIO2_DEFAULT_PROCESSOR));
        if (started) xa->RegisterForCallbacks(&errors);
        started = started && SUCCEEDED(xa->CreateMasteringVoice(&master)) && SUCCEEDED(xa->CreateSourceVoice(&voice, &wf));
        constexpr uint32_t kFrames = kRate / 100, kSlots = 4;
        std::vector<float> bufs(kSlots * kFrames * 2);  // outlives the loop: a lost engine is never released
        if (started) {
            voice->Start();
            for (uint32_t n = 0; !errors.lost;) {
                XAUDIO2_VOICE_STATE st;
                voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
                // paused (app::Paused): the mixer is not pulled, so the title's sound and its sample clock stand still
                if (st.BuffersQueued >= 3 || app::Paused()) {
                    Sleep(2);
                    continue;
                }
                float* b = &bufs[(n % kSlots) * kFrames * 2];
                render(b, kFrames);
                XAUDIO2_BUFFER xb{};
                xb.AudioBytes = kFrames * 8;
                xb.pAudioData = reinterpret_cast<const BYTE*>(b);
                voice->SubmitSourceBuffer(&xb);
                n++;  // only a submitted buffer takes a slot, XAudio2 still reads the queued ones
            }
            // the device went away: no sound until the next start, the engine left as it is
        } else {
            LOGE("audio: XAudio2 unavailable, no sound (the mixer runs silent)");
        }
        // no output device (a remote session, nothing plugged in, or it went away): the mixer is still pulled in real
        // time and the samples dropped, so the title's sample clock runs and what waits on it (a movie's sound) goes on
        auto next = std::chrono::steady_clock::now();
        for (;;) {
            if (app::Paused()) {
                Sleep(10);
                next = std::chrono::steady_clock::now();
                continue;
            }
            render(bufs.data(), kFrames);
            next += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(next);
        }
    }).detach();
}

}  // namespace audio
