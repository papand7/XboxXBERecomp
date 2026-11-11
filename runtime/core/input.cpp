// controllers through SDL: Xbox, PlayStation and Switch pads read into the console controller's report
#include "core/netplay.h"
#include "core/input.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include "core/vk_codes.h"
#endif

#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "app/app.h"
#include "core/log.h"

namespace input {
namespace {
std::atomic<bool> g_tool_keys{false};
}  // namespace
void HoldBackToolKeys(bool on) { g_tool_keys = on; }
// a key the game's tool has while it is on (HoldBackToolKeys): the arrows, PageUp, PageDown, Enter, Delete, Home, End
static bool ToolKey(uint16_t k) {
    return g_tool_keys.load() && (k == 0x25 || k == 0x26 || k == 0x27 || k == 0x28 || k == 0x21 || k == 0x22 || k == 0x0D || k == 0x2E || k == 0x24 || k == 0x23);
}

namespace {

// a controller takes the lowest free port when it is plugged in and keeps it until it is unplugged
std::mutex g_lock;
bool g_sdl = false;
SDL_Gamepad* g_pads[4] = {};

void AddPad(SDL_JoystickID id) {
    // a steering wheel is core/wheel.cpp's, through DirectInput, never a pad here as well
    if (SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL) {
        const char* name = SDL_GetJoystickNameForID(id);
        LOGR("input: %s is a steering wheel, left to the wheel support", name ? name : "a controller");
        return;
    }
    for (SDL_Gamepad* pad : g_pads)
        if (pad && SDL_GetGamepadID(pad) == id) return;
    SDL_Gamepad** slot = std::find(std::begin(g_pads), std::end(g_pads), nullptr);
    if (slot == std::end(g_pads)) return;
    *slot = SDL_OpenGamepad(id);
    const char* name = *slot ? SDL_GetGamepadName(*slot) : SDL_GetError();
    LOGI("input: controller %d %s: %s", int(slot - g_pads) + 1, *slot ? "plugged in" : "failed to open", name ? name : "");
}

// the controller a port reads: the first one plays port 1 with the keyboard, or port 2 when the keyboard is alone
SDL_Gamepad* PadOf(int port, const app::Settings& s) {
    int i = s.keyboard && s.keyboard_alone ? port - 1 : port;
    return i >= 0 && i < 4 ? g_pads[i] : nullptr;
}

void RemovePad(SDL_JoystickID id) {
    for (int i = 0; i < 4; i++) {
        if (!g_pads[i] || SDL_GetGamepadID(g_pads[i]) != id) continue;
        SDL_CloseGamepad(g_pads[i]);
        g_pads[i] = nullptr;
        LOGI("input: controller %d unplugged", i + 1);
    }
}

// once a millisecond at most, however many ports and threads read. Off Windows the window thread pumps SDL's events
// (video needs it on that thread) and hands the device notices over (HandleSdlEvent).
void UpdatePads() {
#ifdef _WIN32
    static uint64_t last = 0;
    uint64_t now = SDL_GetTicks();
    if (!g_sdl || now == last) return;
    last = now;
    SDL_PumpEvents();
    SDL_Event e;
    while (SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) == 1) {
        if (e.type == SDL_EVENT_GAMEPAD_ADDED) AddPad(e.gdevice.which);
        else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) RemovePad(e.gdevice.which);
    }
#endif
}

}  // namespace

#ifndef _WIN32
bool HandleSdlEvent(const void* event) {
    const SDL_Event& e = *static_cast<const SDL_Event*>(event);
    if (e.type != SDL_EVENT_GAMEPAD_ADDED && e.type != SDL_EVENT_GAMEPAD_REMOVED) return false;
    std::lock_guard lock(g_lock);
    if (!g_sdl) return true;
    if (e.type == SDL_EVENT_GAMEPAD_ADDED) AddPad(e.gdevice.which);
    else RemovePad(e.gdevice.which);
    return true;
}
#endif

void Init() {
    // WGI reads the triggers apart and HIDAPI takes PlayStation and Switch pads. Reads come from the guest's threads,
    // so SDL's own thread takes the device notices.
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_DIRECTINPUT, "0");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        LOGE("input: SDL failed to start, no controllers: %s", SDL_GetError());
        return;
    }
    int v = SDL_GetVersion();
    LOGI("input: SDL %d.%d.%d", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v));
    WheelInit(app::Window());
    std::lock_guard lock(g_lock);
    g_sdl = true;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; i++) AddPad(ids[i]);
        SDL_free(ids);
    }
}

namespace {
constexpr const char* kNames[kControls] = {
    "dpad_up", "dpad_down", "dpad_left", "dpad_right", "start", "back", "left_thumb", "right_thumb",
    "a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
    "left_stick_left", "left_stick_right", "left_stick_up", "left_stick_down",
    "right_stick_left", "right_stick_right", "right_stick_up", "right_stick_down"};
constexpr const char* kLabels[kControls] = {
    "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Back", "Left stick click", "Right stick click",
    "A", "B", "X", "Y", "Black (RB)", "White (LB)", "Left trigger", "Right trigger",
    "Left stick left", "Left stick right", "Left stick up", "Left stick down",
    "Right stick left", "Right stick right", "Right stick up", "Right stick down"};

// a pad's inputs as 0 to 1 each, in Control order
void ReadPad(SDL_Gamepad* pad, float* v) {
    constexpr SDL_GamepadButton kButtons[] = {
        SDL_GAMEPAD_BUTTON_DPAD_UP,    SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_START,     SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        // A B X Y black white
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
    for (int i = 0; i < 14; i++) v[i] = SDL_GetGamepadButton(pad, kButtons[i]) ? 1.0f : 0.0f;
    auto axis = [&](SDL_GamepadAxis a) { return float(SDL_GetGamepadAxis(pad, a)) / 32767.0f; };
    v[kLT] = std::max(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0.0f);
    v[kRT] = std::max(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0.0f);
    // SDL's sticks have y down
    float lx = axis(SDL_GAMEPAD_AXIS_LEFTX), ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
    float rx = axis(SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(SDL_GAMEPAD_AXIS_RIGHTY);
    v[kLSLeft] = std::max(-lx, 0.0f), v[kLSRight] = std::max(lx, 0.0f);
    v[kLSUp] = std::max(-ly, 0.0f), v[kLSDown] = std::max(ly, 0.0f);
    v[kRSLeft] = std::max(-rx, 0.0f), v[kRSRight] = std::max(rx, 0.0f);
    v[kRSUp] = std::max(-ry, 0.0f), v[kRSDown] = std::max(ry, 0.0f);
}

int16_t Stick(float neg, float pos) {
    float v = std::clamp(pos - neg, -1.0f, 1.0f);
    return int16_t(std::lround(v * 32767.0f));
}
std::atomic<uint32_t> g_held[4];  // HeldControls, per port
std::atomic<uint16_t> g_rumble[2];  // controller 1's motors as the game last set them, for the wheel

// mouse look: the counts moved since the last poll, read as a rate into a stick deflection. kFullRate counts a second
// at 100% sensitivity is full deflection; a deflection begins past kDeadZone (a game's own stick dead zone would
// swallow slow moves) and follows the rate over about a frame (kSmooth seconds), so polls closer than a frame still
// read it
std::atomic<int> g_mouse_dx{0}, g_mouse_dy{0};
std::atomic<bool> g_keyboard{false};    // UsingKeyboard
std::atomic<int> g_mouse_travel{0};     // counts moved since a controller was last used
constexpr int kMouseTravel = 16;        // that many and the mouse is in use
constexpr float kFullRate = 2400.0f, kDeadZone = 0.2f, kSmooth = 1.0f / 60;

void MouseStick(const app::Settings& s, int16_t& rx, int16_t& ry) {
    static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    static float sx = 0, sy = 0;
    auto now = std::chrono::steady_clock::now();
    float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.001f, 0.1f);
    last = now;
    float k = s.mouse_sensitivity / 100.0f / kFullRate / dt;
    auto deflect = [](float v) {
        if (v == 0) return 0.0f;
        float a = std::min(std::fabs(v), 1.0f);
        return std::copysign(kDeadZone + (1 - kDeadZone) * a, v);
    };
    float tx = deflect(g_mouse_dx.exchange(0) * k), ty = deflect(-g_mouse_dy.exchange(0) * k);
    if (s.mouse_invert_y) ty = -ty;
    float f = std::min(1.0f, dt / kSmooth);
    sx += (tx - sx) * f;
    sy += (ty - sy) * f;
    if (std::fabs(sx) < 0.01f) sx = 0;
    if (std::fabs(sy) < 0.01f) sy = 0;
    rx = int16_t(std::clamp(rx + sx * 32767.0f, -32767.0f, 32767.0f));
    ry = int16_t(std::clamp(ry + sy * 32767.0f, -32767.0f, 32767.0f));
}
}  // namespace

void MouseMoved(int dx, int dy) {
    g_mouse_dx += dx;
    g_mouse_dy += dy;
    if ((g_mouse_travel += std::abs(dx) + std::abs(dy)) > kMouseTravel) g_keyboard = true;
}

// LAN play: the game's own keyboard and mouse handling is controller 1's on one copy only, so off; the mouse plays the
// right stick, which travels with the controller
bool UsingKeyboard() { return g_keyboard && !netplay::Active(); }

std::atomic<int> g_wheel{0};  // WHEEL_DELTA units

void MouseWheel(int delta) {
    g_wheel += delta;
    g_keyboard = true;
}

int TakeWheel() {
    if (!MouseLooking() && !MousePointing()) {
        g_wheel = 0;
        return 0;
    }
    int w = g_wheel.load(), notches = w / 120;
    g_wheel -= notches * 120;
    return notches;
}

std::atomic<uint32_t> g_script_mouse{0};  // XBE_PAD_CMD's CLICKL and CLICKR, held while their press is

#ifdef _WIN32
bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool GameFocused() {
    HWND hwnd = static_cast<HWND>(app::Window());
    return hwnd && GetForegroundWindow() == hwnd;
}
#else
namespace {
// a virtual key's SDL scancode, SDL_SCANCODE_UNKNOWN for the ones without one (mouse buttons and the either-side
// modifiers are KeyDown's own)
SDL_Scancode Scancode(int vk) {
    if (vk >= 'A' && vk <= 'Z') return SDL_Scancode(SDL_SCANCODE_A + (vk - 'A'));
    if (vk >= '1' && vk <= '9') return SDL_Scancode(SDL_SCANCODE_1 + (vk - '1'));
    if (vk == '0') return SDL_SCANCODE_0;
    if (vk >= VK_F1 && vk <= VK_F12) return SDL_Scancode(SDL_SCANCODE_F1 + (vk - VK_F1));
    if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) return SDL_Scancode(SDL_SCANCODE_KP_1 + (vk - VK_NUMPAD1));
    switch (vk) {
        case VK_NUMPAD0: return SDL_SCANCODE_KP_0;
        case VK_BACK: return SDL_SCANCODE_BACKSPACE;
        case VK_TAB: return SDL_SCANCODE_TAB;
        case VK_CLEAR: return SDL_SCANCODE_CLEAR;
        case VK_RETURN: return SDL_SCANCODE_RETURN;
        case VK_PAUSE: return SDL_SCANCODE_PAUSE;
        case VK_CAPITAL: return SDL_SCANCODE_CAPSLOCK;
        case VK_ESCAPE: return SDL_SCANCODE_ESCAPE;
        case VK_SPACE: return SDL_SCANCODE_SPACE;
        case VK_PRIOR: return SDL_SCANCODE_PAGEUP;
        case VK_NEXT: return SDL_SCANCODE_PAGEDOWN;
        case VK_END: return SDL_SCANCODE_END;
        case VK_HOME: return SDL_SCANCODE_HOME;
        case VK_LEFT: return SDL_SCANCODE_LEFT;
        case VK_UP: return SDL_SCANCODE_UP;
        case VK_RIGHT: return SDL_SCANCODE_RIGHT;
        case VK_DOWN: return SDL_SCANCODE_DOWN;
        case VK_SNAPSHOT: return SDL_SCANCODE_PRINTSCREEN;
        case VK_INSERT: return SDL_SCANCODE_INSERT;
        case VK_DELETE: return SDL_SCANCODE_DELETE;
        case VK_LWIN: return SDL_SCANCODE_LGUI;
        case VK_RWIN: return SDL_SCANCODE_RGUI;
        case VK_APPS: return SDL_SCANCODE_APPLICATION;
        case VK_MULTIPLY: return SDL_SCANCODE_KP_MULTIPLY;
        case VK_ADD: return SDL_SCANCODE_KP_PLUS;
        case VK_SEPARATOR: return SDL_SCANCODE_KP_COMMA;
        case VK_SUBTRACT: return SDL_SCANCODE_KP_MINUS;
        case VK_DECIMAL: return SDL_SCANCODE_KP_PERIOD;
        case VK_DIVIDE: return SDL_SCANCODE_KP_DIVIDE;
        case VK_NUMLOCK: return SDL_SCANCODE_NUMLOCKCLEAR;
        case VK_SCROLL: return SDL_SCANCODE_SCROLLLOCK;
        case VK_LSHIFT: return SDL_SCANCODE_LSHIFT;
        case VK_RSHIFT: return SDL_SCANCODE_RSHIFT;
        case VK_LCONTROL: return SDL_SCANCODE_LCTRL;
        case VK_RCONTROL: return SDL_SCANCODE_RCTRL;
        case VK_LMENU: return SDL_SCANCODE_LALT;
        case VK_RMENU: return SDL_SCANCODE_RALT;
        case VK_OEM_1: return SDL_SCANCODE_SEMICOLON;
        case VK_OEM_PLUS: return SDL_SCANCODE_EQUALS;
        case VK_OEM_COMMA: return SDL_SCANCODE_COMMA;
        case VK_OEM_MINUS: return SDL_SCANCODE_MINUS;
        case VK_OEM_PERIOD: return SDL_SCANCODE_PERIOD;
        case VK_OEM_2: return SDL_SCANCODE_SLASH;
        case VK_OEM_3: return SDL_SCANCODE_GRAVE;
        case VK_OEM_4: return SDL_SCANCODE_LEFTBRACKET;
        case VK_OEM_5: return SDL_SCANCODE_BACKSLASH;
        case VK_OEM_6: return SDL_SCANCODE_RIGHTBRACKET;
        case VK_OEM_7: return SDL_SCANCODE_APOSTROPHE;
        case VK_OEM_102: return SDL_SCANCODE_NONUSBACKSLASH;
        default: return SDL_SCANCODE_UNKNOWN;
    }
}
}  // namespace

// SDL's state, as the window thread's event pump last left it (read from any thread: a byte or a word each)
bool KeyDown(int vk) {
    if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2) {
        SDL_MouseButtonFlags b = SDL_GetMouseState(nullptr, nullptr);
        SDL_MouseButtonFlags mask = vk == VK_LBUTTON   ? SDL_BUTTON_LMASK
                                    : vk == VK_RBUTTON ? SDL_BUTTON_RMASK
                                    : vk == VK_MBUTTON ? SDL_BUTTON_MMASK
                                    : vk == VK_XBUTTON1 ? SDL_BUTTON_X1MASK
                                                        : SDL_BUTTON_X2MASK;
        return (b & mask) != 0;
    }
    int count = 0;
    const bool* keys = SDL_GetKeyboardState(&count);
    auto down = [&](SDL_Scancode s) { return keys && s > SDL_SCANCODE_UNKNOWN && int(s) < count && keys[s]; };
    switch (vk) {
        case VK_SHIFT: return down(SDL_SCANCODE_LSHIFT) || down(SDL_SCANCODE_RSHIFT);
        case VK_CONTROL: return down(SDL_SCANCODE_LCTRL) || down(SDL_SCANCODE_RCTRL);
        case VK_MENU: return down(SDL_SCANCODE_LALT) || down(SDL_SCANCODE_RALT);
        default: return down(Scancode(vk));
    }
}

int VkOfScancode(int scancode) {
    static const std::vector<uint8_t> table = [] {
        std::vector<uint8_t> t(SDL_SCANCODE_COUNT, 0);
        for (int vk = 1; vk < 256; vk++)
            if (SDL_Scancode s = Scancode(vk); s != SDL_SCANCODE_UNKNOWN && !t[s]) t[s] = uint8_t(vk);
        t[SDL_SCANCODE_LSHIFT] = t[SDL_SCANCODE_RSHIFT] = VK_SHIFT;
        t[SDL_SCANCODE_LCTRL] = t[SDL_SCANCODE_RCTRL] = VK_CONTROL;
        t[SDL_SCANCODE_LALT] = t[SDL_SCANCODE_RALT] = VK_MENU;
        return t;
    }();
    return scancode > 0 && scancode < int(table.size()) ? table[size_t(scancode)] : 0;
}

bool GameFocused() {
    SDL_Window* w = static_cast<SDL_Window*>(app::Window());
    return w && (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) != 0;
}
#endif

namespace {
// the primary and secondary buttons: GetAsyncKeyState reads the physical ones, swapped for a left handed mouse
// (SDL's are the physical ones as well; its swap setting is not read)
#ifdef _WIN32
uint16_t PrimaryButton() { return GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON; }
uint16_t SecondaryButton() { return GetSystemMetrics(SM_SWAPBUTTON) ? VK_LBUTTON : VK_RBUTTON; }
#else
uint16_t PrimaryButton() { return VK_LBUTTON; }
uint16_t SecondaryButton() { return VK_RBUTTON; }
#endif

// a key binding's mouse_left / mouse_right are the primary and secondary buttons, as the window's messages have them
uint16_t PhysicalKey(uint16_t vk) {
    if (vk == VK_LBUTTON) return PrimaryButton();
    if (vk == VK_RBUTTON) return SecondaryButton();
    return vk;
}
}  // namespace

uint32_t MouseButtons() {
    if ((!MouseLooking() && !MousePointing()) || app::KeyCaptureBusy()) return g_script_mouse;
    return g_script_mouse | uint32_t(KeyDown(PrimaryButton()) ? 1 : 0) | (KeyDown(SecondaryButton()) ? 2 : 0) |
           (KeyDown(VK_MBUTTON) ? 4 : 0);
}

std::array<uint16_t, 2> ControlKeys(int c) {
    const app::Settings& s = app::Get();
    return c >= 0 && c < int(s.keys.size()) ? s.keys[size_t(c)] : app::DefaultKeys(c);
}

bool TakeMouse(float& x, float& y) {
    x = y = 0;
    if (!MouseLooking()) return false;
    const app::Settings& s = app::Get();
    static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 0.001f, 0.1f);
    last = now;
    float k = s.mouse_sensitivity / 100.0f / kFullRate / dt;
    x = g_mouse_dx.exchange(0) * k;
    y = -g_mouse_dy.exchange(0) * k * (s.mouse_invert_y ? -1.0f : 1.0f);
    return true;
}

bool PeekMouse(float& x, float& y) {
    x = y = 0;
    if (!MouseLooking()) return false;
    const app::Settings& s = app::Get();
    float k = s.mouse_sensitivity / 100.0f;
    x = float(g_mouse_dx.load()) * k;
    y = -float(g_mouse_dy.load()) * k * (s.mouse_invert_y ? -1.0f : 1.0f);
    return true;
}

bool MousePointing() {
    return app::kGame.menu_up && app::kGame.menu_up() && GameFocused() && !app::OverlayOpen();
}

bool MouseLooking() {
    const app::Settings& s = app::Get();
    return app::kGame.mouse_look && s.mouse_look && s.keyboard && GameFocused() && !app::OverlayOpen() &&
           !(app::kGame.menu_up && app::kGame.menu_up());
}

const char* ControlName(int c) { return c >= 0 && c < kControls ? kNames[c] : ""; }
const char* ControlLabel(int c) { return c >= 0 && c < kControls ? kLabels[c] : ""; }
int ControlByName(const char* name) {
    for (int c = 0; c < kControls; c++)
        if (std::strcmp(kNames[c], name) == 0) return c;
    return -1;
}

uint32_t HeldControls() { return g_held[0] | g_held[1] | g_held[2] | g_held[3]; }

// Diagnostics, off unless set: XBE_PAD_SCRIPT, a harness pad script ("vblank buttons [vblanks held]", buttons joined
// by +; vblanks counted here as sixtieths of a second from the first poll), and XBE_PAD_CMD=1, a file named cmd in the
// exe's folder holding "buttons [vblanks held]", pressed once when read and then removed. Both press controller 1.
struct ScriptedPress {
    double at;
    std::string buttons;
    double len;
};

void PressNamed(const std::string& names, float out[kControls], uint32_t& mouse) {
    static const std::pair<const char*, int> kNames[] = {
        {"UP", kUp}, {"DOWN", kDown}, {"LEFT", kLeft}, {"RIGHT", kRight}, {"START", kStart}, {"BACK", kBack},
        {"A", kA}, {"B", kB}, {"X", kX}, {"Y", kY}, {"BLACK", kBlack}, {"WHITE", kWhite}, {"LT", kLT}, {"RT", kRT},
        {"LS", kLSUp}, {"LSD", kLSDown}, {"LSL", kLSLeft}, {"LSR", kLSRight},
        {"RS", kRSUp}, {"RSD", kRSDown}, {"RSL", kRSLeft}, {"RSR", kRSRight}};
    size_t at = 0;
    while (at <= names.size()) {
        size_t e = names.find('+', at);
        if (e == std::string::npos) e = names.size();
        std::string b = names.substr(at, e - at);
        at = e + 1;
        for (auto& [n, c] : kNames)
            if (b == n) out[c] = 1.0f;
        if (b == "CLICKL") mouse |= 1;
        if (b == "CLICKR") mouse |= 2;
    }
}

// XBE_PAD_PORTS=N: controllers 2 to N plugged in for the cmd file's presses ("2:A 8"), a split screen's test
int ScriptedPorts() {
    static const int n = [] {
        const char* e = std::getenv("XBE_PAD_PORTS");
        return e && *e ? std::clamp(std::atoi(e), 1, 4) : 1;
    }();
    return n;
}

bool Scripted(int port, float out[kControls]) {
    static const std::vector<ScriptedPress> script = [] {
        std::vector<ScriptedPress> v;
        const char* path = std::getenv("XBE_PAD_SCRIPT");
        if (FILE* f = path && *path ? std::fopen(path, "r") : nullptr) {
            char line[160];
            while (std::fgets(line, sizeof line, f)) {
                char b[64];
                double t, len = 8;
                if (line[0] != '#' && std::sscanf(line, "%lf %63s %lf", &t, b, &len) >= 2) v.push_back({t, b, len});
            }
            std::fclose(f);
            LOGR("input: pad script %s, %zu presses", path, v.size());
        }
        return v;
    }();
    static const bool cmd_on = [] {
        const char* e = std::getenv("XBE_PAD_CMD");
        return e && *e && *e != '0';
    }();
    if (script.empty() && !cmd_on) return false;
    if (port >= ScriptedPorts()) return false;
    static const auto start = std::chrono::steady_clock::now();
    // the cmd file's press, read and replaced by whichever thread polls controller 1
    static std::mutex live_lock;
    static ScriptedPress live[4] = {{-1, "", 0}, {-1, "", 0}, {-1, "", 0}, {-1, "", 0}};
    std::lock_guard lock(live_lock);
    double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() * 60.0;
    if (cmd_on) {
        std::string cmd = (app::ExeDir() / (app::Instance() > 1 ? "cmd" + std::to_string(app::Instance()) : "cmd")).string();
        if (FILE* f = std::fopen(cmd.c_str(), "r")) {
            char b[64] = {};
            double len = 8, y = 0;
            int got = std::fscanf(f, "%63s %lf %lf", b, &len, &y);
            std::fclose(f);
            if (got >= 1) std::remove(cmd.c_str());  // still being written: read again at the next poll
            if (got == 3 && std::string(b) == "PTR") {  // the pointer at x, y of the title's 640 x 480 2D
                app::SetFramePointOverride(float(len) / 640.0f, float(y) / 480.0f);
                LOGR("input: cmd pointer at %.0f, %.0f", len, y);
            } else if (got >= 1) {
                int to = 0;  // "2:A" a press of controller 2's
                std::string name = b;
                if (name.size() > 2 && name[1] == ':' && name[0] >= '1' && name[0] <= '4')
                    to = name[0] - '1', name = name.substr(2);
                live[to] = {t, name, len};
                LOGR("input: cmd %s for %.0f vblanks on controller %d", name.c_str(), len, to + 1);
            }
        }
    }
    uint32_t mouse = 0;
    if (port == 0)
        for (const ScriptedPress& p : script)
            if (t >= p.at && t <= p.at + p.len) PressNamed(p.buttons, out, mouse);
    const ScriptedPress& l = live[port];
    if (l.at >= 0 && t >= l.at && t <= l.at + l.len) PressNamed(l.buttons, out, mouse);
    if (port == 0) g_script_mouse = mouse;
    return true;
}

Pad Poll(int port) {
    Pad p;
    float in[kControls] = {}, out[kControls] = {};
    const app::Settings& s = app::Get();
    {
        std::lock_guard lock(g_lock);
        UpdatePads();
        if (SDL_Gamepad* pad = PadOf(port, s)) {
            p.connected = true;
            ReadPad(pad, in);
        }
    }
    // remapped: each console input reads the physical input the settings name for it
    for (int c = 0; c < kControls; c++) {
        int from = c < int(s.pad_map.size()) && s.pad_map[c] < kControls ? s.pad_map[c] : c;
        out[c] = in[from];
    }
    // a steering wheel plays as controller 1 too, by its own button map (the settings window's Wheel tab)
    float wheel[kControls] = {};
    if (port == 0 && WheelRead(wheel, &p.wheel)) {
        p.connected = true;
        for (int c = 0; c < kControls; c++) {
            out[c] = std::max(out[c], wheel[c]);
            in[c] = std::max(in[c], wheel[c]);  // its Back + Start opens the settings window as a pad's does
        }
        WheelFeel feel;
        if (app::kGame.wheel) app::kGame.wheel(feel);
        WheelFeedback(feel, g_rumble[0], g_rumble[1]);
    }
    // the on-screen touch controls play as controller 1 too (app/touch.cpp)
    if (float touch[kControls] = {}; port == 0 && app::TouchRead(touch)) {
        p.connected = true;
        for (int c = 0; c < kControls; c++) out[c] = std::max(out[c], touch[c]);
    }
    // the keyboard is controller 1, plugged in while keyboard play is on (alt-tabbing away must not unplug it), its
    // keys read while the game window has the focus
    if (port == 0) {
        bool pad_used = false, key_used = false;
        for (int c = 0; c < kControls; c++) pad_used |= in[c] >= 0.5f;
        if (s.keyboard && !p.connected) key_used = true;  // nothing else to play with
        if (s.keyboard) p.connected = true;
        if (s.keyboard && GameFocused() && !app::KeyCaptureBusy()) {
            for (int c = 0; c < kControls; c++) {
                std::array<uint16_t, 2> keys = c < int(s.keys.size()) ? s.keys[c] : app::DefaultKeys(c);
                for (uint16_t k : keys)
                    if (k && !ToolKey(k) && KeyDown(PhysicalKey(k))) out[c] = 1.0f, key_used = true;
            }
        }
        if (pad_used) {
            g_keyboard = false;
            g_mouse_travel = 0;
        } else if (key_used) {
            g_keyboard = true;
        }
    }
    // scripted presses count as the controller's own: the settings window (Back + Start, its navigation) sees them too
    if (float sc[kControls] = {}; port >= 0 && port < 4 && Scripted(port, sc)) {
        p.connected = true;
        for (int c = 0; c < kControls; c++) out[c] = std::max(out[c], sc[c]), in[c] = std::max(in[c], sc[c]);
    }
    // Back + Start on a controller opens or closes the settings window, Start alone closes it; what was held then
    // stays from the game until let go
    uint32_t held = 0;
    for (int c = 0; c < kControls; c++) held |= uint32_t(in[c] >= 0.5f) << c;
    if (port >= 0 && port < 4) {
        static std::atomic<uint32_t> last[4], swallow[4];
        g_held[port] = held;
        uint32_t pressed = held & ~last[port].exchange(held);
        constexpr uint32_t kStartBit = 1u << kStart, kBackBit = 1u << kBack;
        bool open = app::OverlayOpen();
        if ((pressed & kStartBit) && ((held & kBackBit) || open)) {
            std::lock_guard lock(app::UiLock());
            app::ToggleOverlay();
            for (auto& w : swallow) w = kStartBit | kBackBit;
        }
        swallow[port] &= held;
        for (int c = 0; c < kControls; c++)
            if (swallow[port] & (1u << (s.pad_map.size() > size_t(c) ? s.pad_map[c] : c))) out[c] = 0;
    }
    // the settings window takes the input while it is open
    if (app::OverlayOpen()) std::fill(std::begin(out), std::end(out), 0.0f);
    for (int i = 0; i < 8; i++) p.buttons |= uint16_t(out[i] >= 0.5f) << i;
    for (int i = 0; i < 8; i++) p.analog[i] = uint8_t(std::lround(std::clamp(out[kA + i], 0.0f, 1.0f) * 255.0f));
    p.lx = Stick(out[kLSLeft], out[kLSRight]);
    p.ly = Stick(out[kLSDown], out[kLSUp]);
    p.rx = Stick(out[kRSLeft], out[kRSRight]);
    p.ry = Stick(out[kRSDown], out[kRSUp]);
    if (port == 0 && MouseLooking()) {
        if (!app::kGame.mouse_direct || netplay::Active()) MouseStick(s, p.rx, p.ry);
    } else if (port == 0) {  // nothing kept from before (the settings window, another window in front)
        g_mouse_dx = 0;
        g_mouse_dy = 0;
    }
    if (app::kGame.pad) app::kGame.pad(port, p);
    return p;
}

// the motors run until set again
void Rumble(int port, uint16_t left, uint16_t right) {
    if (app::kGame.rumble) app::kGame.rumble(port, left, right);
    if (port == 0) g_rumble[0] = left, g_rumble[1] = right;
    std::lock_guard lock(g_lock);
    if (SDL_Gamepad* pad = PadOf(port, app::Get())) SDL_RumbleGamepad(pad, left, right, 0);
}

}  // namespace input

// a game's own controller rebinding: the physical buttons of controller 1 as the pad is read (before the settings'
// remap); what is held when the capture is armed does not count
namespace input {
namespace {
float g_arm_down[kControls] = {};
bool g_armed = false;
}  // namespace
void ArmPadCapture() {
    std::lock_guard lock(g_lock);
    const app::Settings& s = app::Get();
    float in[kControls] = {};
    UpdatePads();
    if (SDL_Gamepad* pad = PadOf(0, s)) ReadPad(pad, in);
    std::memcpy(g_arm_down, in, sizeof in);
    g_armed = true;
}
int PadPressedControl() {
    std::lock_guard lock(g_lock);
    if (!g_armed) return -1;
    const app::Settings& s = app::Get();
    float in[kControls] = {};
    UpdatePads();
    if (SDL_Gamepad* pad = PadOf(0, s)) ReadPad(pad, in);
    int found = -1;
    for (int c = 0; c < kControls && found < 0; c++)
        if (in[c] > 0.5f && g_arm_down[c] <= 0.5f) found = c;
    if (found >= 0) g_armed = false;
    return found;
}
}  // namespace input

#ifndef _WIN32
// ---- no steering wheel support off Windows yet (core/wheel.cpp is DirectInput): no devices, nothing bound
namespace input {
void WheelInit(void*) {}
void WheelSetup() {}
namespace {
bool WheelRead(float*, bool* steers) {
    if (steers) *steers = false;
    return false;
}
void WheelFeedback(const WheelFeel&, uint16_t, uint16_t) {}
WheelInfo Wheel() { return {}; }
void WheelTestForce(float) {}
}  // namespace input

namespace app {
void WheelDetectStart(WheelDetect, Settings::WheelBind&) {}
void WheelDetectCancel() {}
WheelDetect WheelDetecting(const Settings::WheelBind*) { return WheelDetect::None; }
bool WheelDetectPoll() { return false; }
const char* WheelAxisName(int axis) {
    static const char* kNames[8] = {"X", "Y", "Z", "Rx", "Ry", "Rz", "Slider 1", "Slider 2"};
    return kNames[std::clamp(axis, 0, 7)];
}
std::string WheelBindText(const Settings::WheelBind& b) {
    if (b.axis < 0 && b.button < 0) return "-";
    std::string dev = b.device.empty() ? "the wheel" : b.device;
    if (b.axis >= 0) return dev + ": " + WheelAxisName(b.axis) + " axis";
    return dev + ": button " + std::to_string(b.button);
}
}  // namespace app
#endif
