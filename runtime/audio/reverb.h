// A room's reverb for the mixer (xdk/dsound.cpp, Settings::audio_3d): Schroeder and Moorer's design as Freeverb lays
// it out, eight damped combs and four allpasses a side (the right's delays a little longer, so the two sides differ),
// after a pre-delay. Mono in, stereo out; a block at a time, nothing allocated while it runs.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace audio {

class Reverb {
public:
    explicit Reverb(uint32_t rate) {
        static const uint32_t kCombs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};  // at 44.1 kHz
        static const uint32_t kPasses[4] = {556, 441, 341, 225};
        const float scale = rate / 44100.0f;
        for (int s = 0; s < 2; s++) {
            for (int i = 0; i < 8; i++) comb_[s][i].buf.assign(uint32_t((kCombs[i] + 23 * s) * scale), 0.0f);
            for (int i = 0; i < 4; i++) pass_[s][i].buf.assign(uint32_t((kPasses[i] + 23 * s) * scale), 0.0f);
        }
        pre_.assign(rate / 10, 0.0f);
        rate_ = rate;
    }
    // feedback 0 to below 1 (the tail's length), damp 0 to 1 (how fast its highs fade), pre-delay in seconds
    void Set(float feedback, float damp, float pre_delay) {
        feedback_ = std::clamp(feedback, 0.0f, 0.95f);
        damp_ = std::clamp(damp, 0.0f, 0.95f);
        pre_len_ = std::clamp(uint32_t(pre_delay * rate_), 1u, uint32_t(pre_.size()));
    }
    // in: frames of mono; out: frames of interleaved stereo, the wet sound added at gain
    void Process(const float* in, float* out, uint32_t frames, float gain) {
        for (uint32_t f = 0; f < frames; f++) {
            float x = pre_[pre_at_];
            pre_[pre_at_] = in[f];
            if (++pre_at_ >= pre_len_) pre_at_ = 0;
            x *= kInput;
            for (int s = 0; s < 2; s++) {
                float y = 0;
                for (auto& c : comb_[s]) {
                    float o = c.buf[c.at];
                    c.filt = o * (1 - damp_) + c.filt * damp_;
                    c.buf[c.at] = x + c.filt * feedback_;
                    if (++c.at >= c.buf.size()) c.at = 0;
                    y += o;
                }
                for (auto& p : pass_[s]) {
                    float b = p.buf[p.at];
                    p.buf[p.at] = y + b * 0.5f;
                    if (++p.at >= p.buf.size()) p.at = 0;
                    y = b - y;
                }
                out[f * 2 + s] += y * gain;
            }
        }
    }
    void Clear() {
        for (auto& side : comb_)
            for (auto& c : side) std::fill(c.buf.begin(), c.buf.end(), 0.0f), c.filt = 0;
        for (auto& side : pass_)
            for (auto& p : side) std::fill(p.buf.begin(), p.buf.end(), 0.0f);
        std::fill(pre_.begin(), pre_.end(), 0.0f);
    }

private:
    static constexpr float kInput = 0.015f;  // Freeverb's: eight combs summed stay in range
    struct Line {
        std::vector<float> buf;
        uint32_t at = 0;
        float filt = 0;
    };
    Line comb_[2][8], pass_[2][4];
    std::vector<float> pre_;
    uint32_t pre_at_ = 0, pre_len_ = 1, rate_ = 48000;
    float feedback_ = 0.7f, damp_ = 0.3f;
};

}  // namespace audio
