// blood (gpu::GraphicsStyle::blood): a game's hooks call BloodSpray where a player's shot hits a character and
// BloodHurt when a player is hit. Drawn over the picture in the ImGui frame, so every style and renderer shows it:
// a spray of drops flying from the hit and falling, and red soaking in from the edges of the hit player's view with a
// few splashes, fading.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "gpu/style.h"

namespace app {

namespace {

using Clock = std::chrono::steady_clock;

struct Drop {
    float x, y, vx, vy, r;  // the frame's height a unit, a second the time
};
struct Spray {
    float x, y, strength;
    Clock::time_point at;
    std::vector<Drop> drops;
};
struct Blob {
    float dx, dy, r;  // from the splash's middle, its radius's parts
};
struct Splash {
    float x, y, r;
    std::vector<Blob> blobs;  // a blot of uneven parts, drops thrown one way
};
struct Hurt {
    float x0, y0, x1, y1, amount;
    Clock::time_point at;
    std::vector<Splash> splashes;
};
std::mutex g_lock;
std::vector<Spray> g_sprays;
std::vector<Hurt> g_hurts;
uint32_t g_seed = 0x2545F491u;

float Rand() {  // 0 to 1
    g_seed = g_seed * 1664525u + 1013904223u;
    return float(g_seed >> 8) / 16777216.0f;
}

Splash MakeSplash() {
    Splash s{Rand(), Rand(), 0.03f + 0.04f * Rand(), {}};
    for (int i = 0; i < 6; i++) {  // the blot
        float a = Rand() * 6.2832f, d = 0.45f * Rand();
        s.blobs.push_back({std::cos(a) * d, std::sin(a) * d, 0.35f + 0.45f * Rand()});
    }
    float dir = Rand() * 6.2832f;
    for (int i = 0; i < 7; i++) {  // drops thrown out to one side, smaller farther
        float a = dir + (Rand() - 0.5f) * 1.2f, d = 0.9f + 1.4f * Rand();
        s.blobs.push_back({std::cos(a) * d, std::sin(a) * d, (0.06f + 0.14f * Rand()) * (1.6f - d * 0.4f)});
    }
    return s;
}

constexpr float kSprayLife = 0.6f, kHurtLife = 1.6f;

}  // namespace

void BloodSpray(float x, float y, float strength) {
    if (!gpu::Style().blood) return;
    std::lock_guard<std::mutex> l(g_lock);
    if (g_sprays.size() >= 16) g_sprays.erase(g_sprays.begin());
    Spray s{x, y, std::clamp(strength, 0.2f, 1.0f), Clock::now(), {}};
    int n = int(10 + 16 * s.strength);
    for (int i = 0; i < n; i++) {
        // out from the hit, more to the sides and up than down
        float a = Rand() * 6.2832f, sp = (0.05f + 0.35f * Rand() * Rand()) * (0.6f + 0.6f * s.strength);
        float off = 0.006f * Rand();
        s.drops.push_back({std::cos(a) * off, std::sin(a) * off, std::cos(a) * sp * 1.3f,
                           std::sin(a) * sp - 0.08f, 0.002f + 0.006f * Rand() * Rand()});
    }
    g_sprays.push_back(std::move(s));
}

void BloodHurt(float x0, float y0, float x1, float y1, float amount) {
    if (!gpu::Style().blood) return;
    std::lock_guard<std::mutex> l(g_lock);
    amount = std::clamp(amount, 0.15f, 1.0f);
    // a hit soon after another adds to it: the view's edges redder
    for (Hurt& h : g_hurts)
        if (std::abs(h.x0 - x0) < 0.01f && std::abs(h.y0 - y0) < 0.01f) {
            float age = std::chrono::duration<float>(Clock::now() - h.at).count();
            float left = h.amount * std::max(0.0f, 1.0f - age / kHurtLife);
            amount = std::min(1.0f, left + amount);
            h.at = Clock::now();
            h.amount = amount;
            for (int i = 0; i < 2; i++) {
                if (h.splashes.size() >= 12) h.splashes.erase(h.splashes.begin());
                h.splashes.push_back(MakeSplash());
            }
            return;
        }
    Hurt h{x0, y0, x1, y1, amount, Clock::now(), {}};
    int n = 3 + int(amount * 4);
    for (int i = 0; i < n; i++) h.splashes.push_back(MakeSplash());
    g_hurts.push_back(std::move(h));
}

void DrawBlood() {
    std::lock_guard<std::mutex> l(g_lock);
    if (g_sprays.empty() && g_hurts.empty()) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = vp->Size.x, h = vp->Size.y, aspect = FrameAspect();
    float fw = std::min(w, h * aspect), fh = fw / aspect;
    float fx = vp->Pos.x + (w - fw) / 2, fy = vp->Pos.y + (h - fh) / 2;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    auto now = Clock::now();
    auto red = [](float a, int r = 150, int g = 6, int b = 8) { return IM_COL32(r, g, b, int(255 * std::clamp(a, 0.0f, 1.0f))); };

    for (auto it = g_hurts.begin(); it != g_hurts.end();) {
        float age = std::chrono::duration<float>(now - it->at).count();
        if (age >= kHurtLife) {
            it = g_hurts.erase(it);
            continue;
        }
        float fade = it->amount * (1.0f - std::pow(age / kHurtLife, 1.5f));
        float x0 = fx + it->x0 * fw, y0 = fy + it->y0 * fh, x1 = fx + it->x1 * fw, y1 = fy + it->y1 * fh;
        float vw = x1 - x0, vh = y1 - y0, band = std::min(vw, vh) * (0.08f + 0.10f * it->amount);
        ImU32 edge = red(0.55f * fade, 120, 0, 0), clear = red(0.0f, 120, 0, 0);
        dl->PushClipRect(ImVec2(x0, y0), ImVec2(x1, y1), true);
        dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y0 + band), edge, edge, clear, clear);
        dl->AddRectFilledMultiColor(ImVec2(x0, y1 - band), ImVec2(x1, y1), clear, clear, edge, edge);
        dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x0 + band, y1), edge, clear, clear, edge);
        dl->AddRectFilledMultiColor(ImVec2(x1 - band, y0), ImVec2(x1, y1), clear, edge, edge, clear);
        // splashes on the edges: a blot, drops around it, a run down from it
        for (const Splash& s : it->splashes) {
            float sx = s.x, sy = s.y;
            int side = int(s.x * 4.0f) & 3;
            float along = std::fmod(s.y * 7.13f, 1.0f);
            if (side == 0) sx = 0.03f, sy = along;
            else if (side == 1) sx = 0.97f, sy = along;
            else if (side == 2) sx = along, sy = 0.04f;
            else sx = along, sy = 0.96f;
            float cx = x0 + sx * vw, cy = y0 + sy * vh, r = s.r * vh;
            for (size_t k = 0; k < s.blobs.size(); k++) {
                const Blob& b = s.blobs[k];
                float a = (k < 6 ? 0.7f : 0.8f) * fade;
                dl->AddCircleFilled(ImVec2(cx + b.dx * r, cy + b.dy * r), std::max(b.r * r, 1.0f), red(a, 105, 0, 2), 20);
            }
            dl->AddCircleFilled(ImVec2(cx, cy), r * 0.35f, red(0.45f * fade, 60, 0, 0), 16);  // darker where it is thick
            float run = std::min(age, 1.0f) * r * 1.5f;  // a run down from it
            dl->AddRectFilled(ImVec2(cx - r * 0.1f, cy), ImVec2(cx + r * 0.1f, cy + r * 0.5f + run), red(0.6f * fade, 100, 0, 2),
                              r * 0.1f);
        }
        dl->PopClipRect();
        ++it;
    }

    for (auto it = g_sprays.begin(); it != g_sprays.end();) {
        float t = std::chrono::duration<float>(now - it->at).count();
        if (t >= kSprayLife) {
            it = g_sprays.erase(it);
            continue;
        }
        float fade = 1.0f - std::pow(t / kSprayLife, 2.0f);
        float cx = fx + it->x * fw, cy = fy + it->y * fh;
        // a puff of mist at first
        if (t < 0.15f) {
            float k = t / 0.15f;
            dl->AddCircleFilled(ImVec2(cx, cy), fh * (0.012f + 0.03f * k) * it->strength, red(0.45f * (1 - k), 170, 10, 10), 24);
        }
        for (const Drop& d : it->drops) {
            // flying out, slowed by the air, falling
            float drag = (1.0f - std::exp(-t * 4.0f)) / 4.0f;
            float px = d.x + d.vx * drag, py = d.y + d.vy * drag + 0.35f * t * t;
            float r = d.r * fh * (1.0f - 0.4f * t / kSprayLife);
            ImVec2 c(cx + px * fh, cy + py * fh);
            dl->AddCircleFilled(c, std::max(r, 1.0f), red(0.9f * fade), 10);
            // a streak behind a fast drop
            float sx = d.vx * std::exp(-t * 4.0f) * 0.02f, sy = (d.vy * std::exp(-t * 4.0f) + 0.7f * t) * 0.02f;
            dl->AddLine(c, ImVec2(c.x - sx * fh, c.y - sy * fh), red(0.6f * fade), std::max(r * 1.2f, 1.0f));
        }
        ++it;
    }
}

}  // namespace app
