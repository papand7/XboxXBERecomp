// hit feedback (Settings::hit_feedback): a game's hook calls HitMarker when a local player's shot hits a character.
// A mark of four strokes around that player's crosshair, drawn over the HUD in the ImGui frame (white a hit, amber
// the head, red a kill), and a short tick made here (no sample of the game's), mixed into the sound the mixer hands
// the output.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "audio/audio.h"

namespace app {

namespace {

using Clock = std::chrono::steady_clock;

struct Mark {
    float x, y;
    int kind;
    Clock::time_point at;
};
std::mutex g_marks_lock;
std::vector<Mark> g_marks;

struct Voice {
    std::vector<float> s;
    size_t pos = 0;
};
std::mutex g_voices_lock;
std::vector<Voice> g_voices;

constexpr float kPi = 3.14159265f;

// a tick: two partials with a fast decay over a click of noise; the head's higher and brighter, a kill's longer
// with a low knock under it
std::vector<float> Tick(int kind) {
    const float rate = float(audio::kRate);
    const float len = kind == 2 ? 0.16f : 0.07f;
    const float f1 = kind == 1 ? 3400.0f : kind == 2 ? 1900.0f : 2600.0f, f2 = f1 * 1.5f;
    std::vector<float> s(size_t(len * rate));
    uint32_t noise = 0x1234567u;
    for (size_t i = 0; i < s.size(); i++) {
        float t = float(i) / rate;
        noise = noise * 1664525u + 1013904223u;
        float n = float(int32_t(noise)) / 2147483648.0f;
        float tone = std::sin(2 * kPi * f1 * t) * 0.6f + std::sin(2 * kPi * f2 * t) * 0.25f;
        float v = tone * std::exp(-t * (kind == 2 ? 28.0f : 60.0f)) + n * 0.35f * std::exp(-t * 900.0f);
        if (kind == 2) v += std::sin(2 * kPi * 140.0f * t) * 0.7f * std::exp(-t * 35.0f);
        float attack = std::min(1.0f, t * rate / 24.0f);  // no click at its start
        s[i] = v * attack * 0.22f;
    }
    return s;
}

const std::vector<float>& Sound(int kind) {
    static const std::vector<float> s[3] = {Tick(0), Tick(1), Tick(2)};
    return s[std::clamp(kind, 0, 2)];
}

}  // namespace

void HitMarker(float x, float y, int kind) {
    uint32_t mode = Get().hit_feedback;
    if (mode == 0) return;
    {
        std::lock_guard<std::mutex> l(g_marks_lock);
        // one mark a player's crosshair: a new hit there takes its place, a kill keeps its colour for its time
        auto it = std::find_if(g_marks.begin(), g_marks.end(), [&](const Mark& m) {
            return std::abs(m.x - x) < 0.01f && std::abs(m.y - y) < 0.01f;
        });
        Mark m{x, y, kind, Clock::now()};
        if (it == g_marks.end()) g_marks.push_back(m);
        else if (kind >= it->kind || Clock::now() - it->at > std::chrono::milliseconds(150)) *it = m;
    }
    if (mode >= 2) {
        std::lock_guard<std::mutex> l(g_voices_lock);
        if (g_voices.size() < 8) g_voices.push_back({Sound(kind), 0});
    }
}

void MixHitSound(float* out, uint32_t frames) {
    std::lock_guard<std::mutex> l(g_voices_lock);
    for (Voice& v : g_voices) {
        for (uint32_t i = 0; i < frames && v.pos < v.s.size(); i++, v.pos++) {
            out[i * 2] += v.s[v.pos];
            out[i * 2 + 1] += v.s[v.pos];
        }
    }
    g_voices.erase(std::remove_if(g_voices.begin(), g_voices.end(), [](const Voice& v) { return v.pos >= v.s.size(); }),
                   g_voices.end());
}

void DrawHitMarkers() {
    std::lock_guard<std::mutex> l(g_marks_lock);
    if (g_marks.empty()) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    // the frame at its shape, centred in the window (as FramePoint takes it)
    float w = vp->Size.x, h = vp->Size.y, aspect = FrameAspect();
    float fw = std::min(w, h * aspect), fh = fw / aspect;
    float fx = vp->Pos.x + (w - fw) / 2, fy = vp->Pos.y + (h - fh) / 2;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    auto now = Clock::now();
    for (auto it = g_marks.begin(); it != g_marks.end();) {
        float age = std::chrono::duration<float>(now - it->at).count();
        float life = it->kind == 2 ? 0.45f : 0.28f;
        if (age >= life) {
            it = g_marks.erase(it);
            continue;
        }
        float fade = 1.0f - std::pow(age / life, 2.0f);
        float pop = 1.0f + 0.35f * std::max(0.0f, 1.0f - age / 0.08f);  // a little larger at first
        float cx = fx + it->x * fw, cy = fy + it->y * fh;
        float r0 = fh * 0.011f * pop, r1 = fh * (it->kind == 0 ? 0.024f : 0.030f) * pop;
        float th = std::max(1.5f, fh / 420.0f);
        ImU32 col = it->kind == 2   ? IM_COL32(235, 40, 30, int(255 * fade))
                    : it->kind == 1 ? IM_COL32(255, 190, 40, int(255 * fade))
                                    : IM_COL32(255, 255, 255, int(235 * fade));
        ImU32 edge = IM_COL32(0, 0, 0, int(140 * fade));
        for (int pass = 0; pass < 2; pass++)
            for (int q = 0; q < 4; q++) {
                float dx = (q & 1) ? 0.7071f : -0.7071f, dy = (q & 2) ? 0.7071f : -0.7071f;
                ImVec2 a(cx + dx * r0, cy + dy * r0), b(cx + dx * r1, cy + dy * r1);
                dl->AddLine(a, b, pass ? col : edge, pass ? th : th + 2.0f);
            }
        ++it;
    }
}

}  // namespace app
