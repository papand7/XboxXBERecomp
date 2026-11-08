// On-screen touch controls (Android; elsewhere XBE_TOUCH=1, the mouse then plays one finger): controller 1 played from
// the screen. A finger landing on the left of the screen is a stick there (it moves where it lands), one on a button
// holds that button, one elsewhere on the right looks by dragging (as the right stick, faster drags turn faster; the
// fire button drags to look too). Drawn over the frame in the ImGui frame; hidden while a controller is connected or
// the settings window is open (whose fingers are then ImGui's pointer). The buttons are GameInfo::touch_buttons, else
// the console's face buttons, triggers, Start and Back.
#ifndef _WIN32

#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <vector>

#include <imgui.h>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace app {

namespace {

using Clock = std::chrono::steady_clock;

const std::vector<TouchButton>& Buttons() {
    static const std::vector<TouchButton> kDefault = {
        {input::kRT, "RT", nullptr, 0.18f, 0.32f, 0.10f},  {input::kLT, "LT", nullptr, 0.18f, 0.58f, 0.065f},
        {input::kA, "A", nullptr, 0.36f, 0.10f, 0.065f},   {input::kB, "B", nullptr, 0.07f, 0.12f, 0.06f},
        {input::kX, "X", nullptr, 0.07f, 0.52f, 0.06f},    {input::kY, "Y", nullptr, 0.38f, 0.30f, 0.065f},
        {input::kStart, "Start", nullptr, 0.07f, 0.92f, 0.045f}, {input::kBack, "Back", nullptr, 0.18f, 0.92f, 0.045f},
    };
    return kGame.touch_buttons.empty() ? kDefault : kGame.touch_buttons;
}

enum class Role { Stick, Look, Button, Settings };
struct Finger {
    SDL_FingerID id;
    Role role;
    int button = -1;          // Buttons()'s index
    float x0 = 0, y0 = 0;     // where it landed, in screen heights from the top left
    float x = 0, y = 0;       // where it is now
    float lx = 0, ly = 0;     // where the look last read it
};

std::mutex g_lock;
std::vector<Finger> g_fingers;
float g_w = 1, g_h = 1;               // the window's size (its aspect: positions are in screen heights)
bool g_used = false;                  // a finger has touched: the controls show
Clock::time_point g_back_until{};     // Android's back key: Start held a moment
Clock::time_point g_last_read = Clock::now();
float g_look_x = 0, g_look_y = 0;     // the look's smoothed rate

constexpr float kStickRadius = 0.12f;  // screen heights for a full push
constexpr float kLookFull = 0.9f;      // screen heights a second for a full right stick
constexpr float kSettingsW = 0.16f, kSettingsH = 0.07f;

bool Enabled() {
    static const bool on = [] {
#ifdef __ANDROID__
        return true;
#else
        const char* e = std::getenv("XBE_TOUCH");
        return e && *e == '1';
#endif
    }();
    return on;
}

// a button's centre in screen heights from the top left: its x from the right edge (from the left when negative), its
// y from the bottom
ImVec2 Centre(const TouchButton& b) {
    float aspect = g_w / g_h;
    return {b.x >= 0 ? aspect - b.x : -b.x, 1.0f - b.y};
}

int ButtonAt(float x, float y) {
    const auto& buttons = Buttons();
    int best = -1;
    float best_d = 1e9f;
    for (size_t i = 0; i < buttons.size(); i++) {
        ImVec2 c = Centre(buttons[i]);
        float d = std::hypot(x - c.x, y - c.y);
        if (d <= buttons[i].r * 1.25f && d < best_d) best = int(i), best_d = d;  // a little slack around each
    }
    return best;
}

bool Shown() {
    return Enabled() && g_used && !SDL_HasGamepad() && !OverlayOpen();
}

}  // namespace

bool TouchEvent(const void* event) {
    if (!Enabled()) return false;
    const SDL_Event& e = *static_cast<const SDL_Event*>(event);
    if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_AC_BACK) {
        // Android's back gesture: the game's pause (Start), or the settings window shut
        if (OverlayOpen()) {
            std::lock_guard ui(UiLock());
            ToggleOverlay();
        } else {
            std::lock_guard lock(g_lock);
            g_back_until = Clock::now() + std::chrono::milliseconds(150);
        }
        return true;
    }
    if (e.type != SDL_EVENT_FINGER_DOWN && e.type != SDL_EVENT_FINGER_UP && e.type != SDL_EVENT_FINGER_MOTION &&
        e.type != SDL_EVENT_FINGER_CANCELED)
        return false;
    int iw = 0, ih = 0;
    if (SDL_Window* w = static_cast<SDL_Window*>(Window())) SDL_GetWindowSize(w, &iw, &ih);
    if (iw <= 0 || ih <= 0) return true;
    // the settings window: the finger is its pointer
    if (OverlayOpen()) {
        std::lock_guard ui(UiLock());
        if (ImGui::GetCurrentContext()) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(e.tfinger.x * float(iw), e.tfinger.y * float(ih));
            if (e.type == SDL_EVENT_FINGER_DOWN) io.AddMouseButtonEvent(0, true);
            if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) io.AddMouseButtonEvent(0, false);
        }
        std::lock_guard lock(g_lock);
        g_fingers.clear();
        return true;
    }
    bool toggle = false;  // the settings button let go on it: opened once the lock is let go (DrawTouch's order)
    {
        std::lock_guard lock(g_lock);
        g_w = float(iw), g_h = float(ih);
        float x = e.tfinger.x * g_w / g_h, y = e.tfinger.y;  // screen heights
        auto it = std::find_if(g_fingers.begin(), g_fingers.end(), [&](const Finger& f) { return f.id == e.tfinger.fingerID; });
        if (e.type == SDL_EVENT_FINGER_DOWN) {
            if (!g_used) LOGR("touch: on-screen controls shown");
            g_used = true;
            if (it != g_fingers.end()) g_fingers.erase(it);
            Finger f{e.tfinger.fingerID, Role::Look};
            f.x0 = f.x = f.lx = x, f.y0 = f.y = f.ly = y;
            if (kGame.settings_window && x < kSettingsW && y < kSettingsH) {
                f.role = Role::Settings;
            } else if (int b = ButtonAt(x, y); b >= 0) {
                f.role = Role::Button, f.button = b;
            } else if (x < 0.45f * g_w / g_h) {
                f.role = Role::Stick;
            }
            g_fingers.push_back(f);
        } else if (it != g_fingers.end()) {
            if (e.type == SDL_EVENT_FINGER_MOTION) {
                it->x = x, it->y = y;
            } else {
                toggle = it->role == Role::Settings && x < kSettingsW && y < kSettingsH;
                g_fingers.erase(it);
            }
        }
    }
    if (toggle) {
        std::lock_guard ui(UiLock());
        ToggleOverlay();
    }
    return true;
}

bool TouchRead(float* out) {
    std::lock_guard lock(g_lock);
    auto now = Clock::now();
    float dt = std::clamp(std::chrono::duration<float>(now - g_last_read).count(), 1e-3f, 0.1f);
    g_last_read = now;
    bool back = now < g_back_until;
    if (back) out[input::kStart] = 1;
    if (!Shown()) {
        g_look_x = g_look_y = 0;
        return back;
    }
    const auto& buttons = Buttons();
    float look_dx = 0, look_dy = 0;
    for (Finger& f : g_fingers) {
        switch (f.role) {
            case Role::Stick: {
                float dx = (f.x - f.x0) / kStickRadius, dy = (f.y - f.y0) / kStickRadius;
                float len = std::hypot(dx, dy);
                if (len > 1) {
                    // past the edge the stick's centre follows the finger
                    f.x0 += (f.x - f.x0) * (1 - 1 / len), f.y0 += (f.y - f.y0) * (1 - 1 / len);
                    dx /= len, dy /= len;
                }
                out[input::kLSLeft] = std::max(out[input::kLSLeft], -dx);
                out[input::kLSRight] = std::max(out[input::kLSRight], dx);
                out[input::kLSUp] = std::max(out[input::kLSUp], -dy);
                out[input::kLSDown] = std::max(out[input::kLSDown], dy);
                break;
            }
            case Role::Button:
                if (f.button >= 0 && size_t(f.button) < buttons.size()) {
                    int c = buttons[size_t(f.button)].control;
                    if (c >= 0 && c < input::kControls) out[c] = 1;
                    if (c != input::kRT) break;
                }
                [[fallthrough]];  // the fire button looks as it is dragged
            case Role::Look:
                look_dx += f.x - f.lx, look_dy += f.y - f.ly;
                f.lx = f.x, f.ly = f.y;
                break;
            case Role::Settings:
                break;
        }
    }
    // the drag's rate, smoothed a little over the polls, as the right stick
    float k = std::min(1.0f, dt * 30.0f);
    g_look_x += (look_dx / dt / kLookFull - g_look_x) * k;
    g_look_y += (look_dy / dt / kLookFull - g_look_y) * k;
    float rx = std::clamp(g_look_x, -1.0f, 1.0f), ry = std::clamp(g_look_y, -1.0f, 1.0f);
    out[input::kRSLeft] = std::max(out[input::kRSLeft], -rx);
    out[input::kRSRight] = std::max(out[input::kRSRight], rx);
    out[input::kRSUp] = std::max(out[input::kRSUp], -ry);
    out[input::kRSDown] = std::max(out[input::kRSDown], ry);
    return true;
}

bool TouchControlsShown() {
    std::lock_guard lock(g_lock);
    return Shown();
}

void DrawTouch() {
    std::lock_guard lock(g_lock);
    if (!Shown()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 size = ImGui::GetIO().DisplaySize;
    float h = size.y;
    if (h <= 0) return;
    auto px = [&](float x, float y) { return ImVec2(x * h, y * h); };
    // the modern menus' blues
    const ImU32 fill = IM_COL32(18, 52, 110, 80), held = IM_COL32(60, 140, 255, 150), edge = IM_COL32(130, 190, 255, 170),
                text = IM_COL32(225, 238, 255, 230);
    bool menu = kGame.menu_up && kGame.menu_up();
    const auto& buttons = Buttons();
    std::vector<bool> down(buttons.size(), false);
    for (const Finger& f : g_fingers)
        if (f.role == Role::Button && f.button >= 0 && size_t(f.button) < down.size()) down[size_t(f.button)] = true;
    ImFont* font = ImGui::GetFont();
    for (size_t i = 0; i < buttons.size(); i++) {
        const TouchButton& b = buttons[i];
        ImVec2 c = px(Centre(b).x, Centre(b).y);
        float r = b.r * h;
        dl->AddCircleFilled(c, r, down[i] ? held : fill, 40);
        dl->AddCircle(c, r, edge, 40, std::max(1.5f, h / 360.0f));
        const char* label = menu && b.menu_label ? b.menu_label : b.label;
        if (!label) continue;
        float fs = std::clamp(r * 0.55f, 10.0f, 64.0f);
        ImVec2 ts = font->CalcTextSizeA(fs, 1e9f, 0, label);
        if (ts.x > r * 1.8f) fs *= r * 1.8f / ts.x, ts = font->CalcTextSizeA(fs, 1e9f, 0, label);
        dl->AddText(font, fs, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), text, label);
    }
    // the stick where a finger holds it
    for (const Finger& f : g_fingers) {
        if (f.role != Role::Stick) continue;
        ImVec2 c = px(f.x0, f.y0);
        dl->AddCircleFilled(c, kStickRadius * h, fill, 48);
        dl->AddCircle(c, kStickRadius * h, edge, 48, std::max(1.5f, h / 360.0f));
        float dx = f.x - f.x0, dy = f.y - f.y0, len = std::hypot(dx, dy);
        if (len > kStickRadius) dx *= kStickRadius / len, dy *= kStickRadius / len;
        dl->AddCircleFilled(px(f.x0 + dx, f.y0 + dy), kStickRadius * 0.45f * h, held, 32);
    }
    if (kGame.settings_window) {
        ImVec2 a = px(0.015f, 0.015f), b = px(kSettingsW - 0.015f, kSettingsH - 0.01f);
        dl->AddRectFilled(a, b, fill, h * 0.01f);
        dl->AddRect(a, b, edge, h * 0.01f);
        float fs = (b.y - a.y) * 0.6f;
        ImVec2 ts = font->CalcTextSizeA(fs, 1e9f, 0, "Settings");
        dl->AddText(font, fs, ImVec2((a.x + b.x - ts.x) / 2, (a.y + b.y - ts.y) / 2), text, "Settings");
    }
}

}  // namespace app

#endif  // !_WIN32
