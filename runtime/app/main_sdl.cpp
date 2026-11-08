// the host off Windows: an SDL3 window and event loop, as app/main.cpp is on Windows; the game is described by
// app::kGame. The window thread is the main thread (SDL's video wants it): other threads ask it for things through
// SDL user events (ApplyDisplay, OpenGameWindow, Quit).
//
// Phase 2 of the Linux port (the CMake branch) builds this with SDL3, ImGui's SDL3 backend and the Vulkan headers;
// gpu/vulkan.cpp then takes the surface from CreateVulkanSurface / VulkanInstanceExtensions below and the size from
// WindowPixels, and calls ImGui_ImplSDL3_NewFrame in place of the Win32 backend's.
#ifndef _WIN32

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <dlfcn.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <jni.h>  // the relaunch through the app's Java side (AndroidRelaunch)
#else
#include <execinfo.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>

#include "app/app.h"
#include "core/diag.h"
#include "core/input.h"
#include "core/log.h"
#include "core/netplay.h"
#include "core/vk_codes.h"
#include "gpu/backend.h"
#include "image_symbols.h"
#include "kernel/kernel.h"

namespace app {

// kGame's wide strings (UTF-32 here) as UTF-8
std::string Utf8(const wchar_t* w) {
    std::string s;
    for (; w && *w; w++) {
        auto c = uint32_t(*w);
        if (c < 0x80) {
            s += char(c);
        } else if (c < 0x800) {
            s += char(0xC0 | c >> 6), s += char(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            s += char(0xE0 | c >> 12), s += char(0x80 | (c >> 6 & 0x3F)), s += char(0x80 | (c & 0x3F));
        } else {
            s += char(0xF0 | c >> 18), s += char(0x80 | (c >> 12 & 0x3F)), s += char(0x80 | (c >> 6 & 0x3F));
            s += char(0x80 | (c & 0x3F));
        }
    }
    return s;
}

bool MessageBox(const wchar_t* text, bool error, bool yes_no) {
    std::string title = Utf8(kGame.short_title), body = Utf8(text);
    if (!yes_no)
        return SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_WARNING, title.c_str(), body.c_str(),
                                        static_cast<SDL_Window*>(Window()));
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 0, "No"},
        {0, 1, "Yes"},
    };
    SDL_MessageBoxData box{};
    box.flags = error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_WARNING;
    box.window = static_cast<SDL_Window*>(Window());
    box.title = title.c_str();
    box.message = body.c_str();
    box.numbuttons = 2;
    box.buttons = buttons;
    int pressed = 0;
    return SDL_ShowMessageBox(&box, &pressed) && pressed == 1;
}

namespace {

SDL_Window* g_window = nullptr;
Uint32 g_event_base = 0;  // SDL_RegisterEvents: kApplyDisplay, kOpenWindow after it
enum : Sint32 { kApplyDisplay, kOpenWindow };
std::atomic<bool> g_active{true};  // the game window has the keyboard
bool g_apply_posted = false;       // window thread: a display apply waits in the queue

// OpenGameWindow's wait for the window thread
struct OpenRequest {
    bool apply = true;
    std::mutex lock;
    std::condition_variable cv;
    bool done = false;
};

// a crash: the faulting address, the guest thread's stack and a host backtrace in the log and on stderr
void OnCrash(int sig, siginfo_t* info, void*) {
    LOGE("crash: signal %d at %p", sig, info ? info->si_addr : nullptr);
    if (kernel::Thread* t = kernel::Current(); t && t->ctx) {
        uint64_t va = uint64_t(reinterpret_cast<uintptr_t>(info ? info->si_addr : nullptr));
        uint64_t g = uint64_t(reinterpret_cast<uintptr_t>(kernel::g_base));
        LOGE("  on guest thread %X, guest %08llX", t->id, (unsigned long long)(va - g));
        kernel::LogGuestStack(*t->ctx, 256);
    }
    logx::Flush();
#ifndef __ANDROID__  // Android's own crash report (logcat, the tombstone) has the host stack
    void* frames[32];
    backtrace_symbols_fd(frames, backtrace(frames, 32), 2);
#endif
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void DumpThreads() {
    LOGR("F3: thread stacks%s", diag::CaptureDir().empty() ? " (no capture: XBE_CAPTURE_DIR is not set)" : ", the next frame captured");
    gpu::g_capture = true;
    for (auto& t : kernel::Threads())
        if (t->ctx) {
            LOGR("F3: thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 32);
        }
}

// the monitor setting's display (the primary one when it is gone)
SDL_DisplayID Display() {
    SDL_DisplayID id = 0;
    int count = 0;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
        uint32_t n = Get().monitor;
        if (n && n <= uint32_t(count)) id = ids[n - 1];
        SDL_free(ids);
    }
    return id ? id : SDL_GetPrimaryDisplay();
}

// window thread only: the window's mode, size and place from the settings
void ApplyNow() {
    const Settings& s = Get();
    SDL_DisplayID display = Display();
    Mode out = Output();
    if (s.display_mode == DisplayMode::Windowed) {
        SDL_SetWindowFullscreen(g_window, false);
        SDL_SetWindowBordered(g_window, true);
        SDL_SetWindowSize(g_window, int(out.width), int(out.height));
        SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        return;
    }
    // borderless: the desktop's mode (no mode change); fullscreen: the closest mode to the settings' size when it is
    // not the desktop's own (switching to that anyway blanks the screen for nothing)
    SDL_DisplayMode mode{};
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
    bool exclusive = s.display_mode == DisplayMode::Fullscreen && s.width && s.height && desktop &&
                     (int(s.width) != desktop->w || int(s.height) != desktop->h);
    if (exclusive && !SDL_GetClosestFullscreenDisplayMode(display, int(s.width), int(s.height), 0.0f, false, &mode)) {
        LOGR("display: %ux%u not offered (%s), the desktop's mode kept", s.width, s.height, SDL_GetError());
        exclusive = false;
    }
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    if (!SDL_SetWindowFullscreenMode(g_window, exclusive ? &mode : nullptr))
        LOGR("display: fullscreen mode not set: %s", SDL_GetError());
    SDL_SetWindowFullscreen(g_window, true);
}

// the title's pointer for its menus (GameInfo::menu_cursor), made again when the window's height asks another size
SDL_Cursor* MenuCursor() {
    static SDL_Cursor* made = nullptr;
    static int made_size = 0;
    if (!kGame.menu_cursor) return nullptr;
    int w = 0, h = 0;
    SDL_GetWindowSize(g_window, &w, &h);
    int size = std::clamp(h / 24, 16, 96);
    if (made && size == made_size) return made;
    std::vector<uint32_t> px;
    int hx = 0, hy = 0;
    kGame.menu_cursor(size, px, hx, hy);
    if (px.size() != size_t(size) * size) return made;
    // premultiplied BGRA to SDL's straight alpha (ARGB8888 is B, G, R, A in memory here)
    for (uint32_t& p : px) {
        uint32_t a = p >> 24;
        if (!a || a == 255) continue;
        auto un = [&](int shift) { return std::min(255u, ((p >> shift & 0xFF) * 255 + a / 2) / a) << shift; };
        p = a << 24 | un(16) | un(8) | un(0);
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(size, size, SDL_PIXELFORMAT_ARGB8888, px.data(), size * 4);
    SDL_Cursor* c = surface ? SDL_CreateColorCursor(surface, hx, hy) : nullptr;
    if (surface) SDL_DestroySurface(surface);
    if (!c) return made;
    if (made) SDL_DestroyCursor(made);
    made = c;
    made_size = size;
    return made;
}

// each turn of the loop: mouse look holds the mouse (relative mode: hidden, raw movement), a title's menu shows its
// pointer
void UpdateMouse() {
    static bool relative = false;
    static SDL_Cursor* shown = nullptr;
    bool want = input::MouseLooking();
    if (want != relative && SDL_SetWindowRelativeMouseMode(g_window, want)) relative = want;
    SDL_Cursor* c = nullptr;
    if (!want && kGame.menu_up && kGame.menu_up() && !OverlayOpen()) c = MenuCursor();
    if (!c) c = SDL_GetDefaultCursor();
    if (c != shown && c) SDL_SetCursor(c), shown = c;
}

uint16_t MouseVk(Uint8 button) {
    switch (button) {
        case SDL_BUTTON_LEFT: return VK_LBUTTON;
        case SDL_BUTTON_RIGHT: return VK_RBUTTON;
        case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
        case SDL_BUTTON_X1: return VK_XBUTTON1;
        default: return VK_XBUTTON2;
    }
}

// one event, window thread; false: leave the loop
bool HandleEvent(const SDL_Event& e) {
    if (TouchEvent(&e)) return true;
    if (input::HandleSdlEvent(&e)) return true;
    if (e.type == g_event_base) {
        if (e.user.code == kApplyDisplay) {
            if (SDL_GetWindowFlags(g_window) & SDL_WINDOW_MINIMIZED) SDL_RestoreWindow(g_window);
            ApplyNow();
            g_apply_posted = false;
        } else if (e.user.code == kOpenWindow) {
            auto* r = static_cast<OpenRequest*>(e.user.data1);
            CloseSplash();
            if (r->apply) ApplyNow();
            SDL_ShowWindow(g_window);
            SDL_RaiseWindow(g_window);
            std::lock_guard lock(r->lock);
            r->done = true;
            r->cv.notify_all();
        }
        return true;
    }
    switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return false;
        case SDL_EVENT_MOUSE_MOTION:
            // the relative motion's fractions kept, as raw input's counts are whole
            if (input::MouseLooking()) {
                static float fx = 0, fy = 0;
                fx += e.motion.xrel, fy += e.motion.yrel;
                int dx = int(fx), dy = int(fy);
                fx -= float(dx), fy -= float(dy);
                if (dx || dy) input::MouseMoved(dx, dy);
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (input::MouseLooking() || input::MousePointing()) {
                float y = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
                if (int notch = int(y * 120.0f)) input::MouseWheel(notch);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            // a key binding the settings window waits for takes the next mouse button too
            uint16_t vk = MouseVk(e.button.button);
            if (CaptureKey(vk)) return true;
            std::lock_guard lock(UiLock());
            if (OverlayKey(vk)) return true;
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            uint16_t vk = uint16_t(input::VkOfScancode(int(e.key.scancode)));
            if (!vk) break;
            std::lock_guard lock(UiLock());
            if (!e.key.repeat && CaptureKey(vk)) return true;  // a title's binding: not a held key's repeat
            if (vk == VK_F1 && kGame.settings_window && !e.key.repeat) {
                ToggleOverlay();
                return true;
            }
            if (OverlayKey(vk)) return true;
            if (vk == VK_F3) DumpThreads();
            if (vk == VK_F4) {
                LOGR("F4: frame log requested");
                diag::g_framelog_request = true;
            }
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            g_active = true;
            // fullscreen at another mode than the desktop's: set again on return (SDL minimized it on the way out)
            if (Get().display_mode == DisplayMode::Fullscreen && Get().width && Get().height && !g_apply_posted &&
                !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_HIDDEN)) {
                g_apply_posted = true;
                ApplyDisplay();
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_active = false;
            break;
        default:
            break;
    }
    // GameInfo::message is Windows' messages: not called here
    std::lock_guard lock(UiLock());
    if (ImGui::GetCurrentContext()) ImGui_ImplSDL3_ProcessEvent(&e);
    return true;
}

void PushUser(Sint32 code, void* data = nullptr) {
    SDL_Event e{};
    e.type = g_event_base;
    e.user.code = code;
    e.user.data1 = data;
    if (!SDL_PushEvent(&e)) LOGW("window: event %d not queued: %s", int(code), SDL_GetError());
}

}  // namespace

std::filesystem::path ExeDir() {
    static const std::filesystem::path dir = [] {
#ifdef __ANDROID__
        // the process is the app's (app_process): the folder this library was loaded from, where the app put the game
        // package (its files, logs, saves and caches go beside it as an exe's do)
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&ExeDir), &info) && info.dli_fname)
            return std::filesystem::path(info.dli_fname).parent_path();
        return std::filesystem::current_path();
#endif
        std::error_code ec;
        std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::current_path() : self.parent_path();
    }();
    return dir;
}

// --instance N on the command line, kept as XBE_INSTANCE so a relaunch (exec, kernel::Relaunch) keeps it
int Instance() {
    static const int n = [] {
        const char* e = std::getenv("XBE_INSTANCE");
        return e ? std::clamp(std::atoi(e), 0, 8) : 0;
    }();
    return n;
}

void* Window() { return g_window; }

namespace {
std::atomic<bool> g_point_override{false};
std::atomic<float> g_point_u{0}, g_point_v{0};
}  // namespace
void SetFramePointOverride(float u, float v) {
    g_point_u = u, g_point_v = v;
    g_point_override = true;
}

bool FramePoint(float& u, float& v) {
    if (g_point_override) {
        u = g_point_u, v = g_point_v;
        return true;
    }
    if (!g_window || SDL_GetMouseFocus() != g_window) return false;
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);  // window coordinates, as the size below
    int iw = 0, ih = 0;
    SDL_GetWindowSize(g_window, &iw, &ih);
    float w = float(iw), h = float(ih);
    if (w <= 0 || h <= 0 || x < 0 || y < 0 || x >= w || y >= h) return false;
    // the frame at its shape, centred (the renderers' BlitFrame), then the 2D's own shape in its middle
    float aspect = FrameAspect();
    float vw = std::min(w, h * aspect), vh = vw / aspect;
    u = (x - (w - vw) / 2) / vw;
    v = (y - (h - vh) / 2) / vh;
    if (aspect > kGame.hud_aspect + 1e-3f) u = 0.5f + (u - 0.5f) * aspect / kGame.hud_aspect;
    return true;
}

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void ApplyDisplay() { PushUser(kApplyDisplay); }

void Quit() {
    LOGR("quit asked for");
    SDL_Event e{};
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
}

void OpenGameWindow(bool apply) {
    OpenRequest r;
    r.apply = apply;
    PushUser(kOpenWindow, &r);
    std::unique_lock lock(r.lock);
    r.cv.wait(lock, [&] { return r.done; });
}

bool Paused() {
    return !netplay::Active() && Get().pause_unfocused && !g_active && g_window &&
           !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_HIDDEN);
}

static void (*g_mid_frame)() = nullptr;
void SetMidFrame(void (*fn)()) { g_mid_frame = fn; }

// as app/main.cpp's
void Pace(uint32_t vblanks) {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    if (Paused()) {
        LOGR("paused: the window is not in front");
        while (Paused()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        LOGR("resumed");
        next = clock::now();
    }
    uint32_t cap = PaceRate(Get().fps_cap, vblanks);
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    static clock::time_point last = now, report = now + std::chrono::seconds(10);
    static uint32_t frames = 0, slow = 0;
    static double longest = 0;
    double took = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    frames++;
    if (took > 1.5 * std::chrono::duration<double, std::milli>(period).count()) slow++, longest = std::max(longest, took);
    if (now >= report) {
        uint32_t compiled = gpu::g_shaders_compiled.exchange(0);
        if (slow) LOGR("frames: %u of %u in the last 10 s over %.1f ms (%u a second paced), the longest %.1f ms; "
                       "%u shaders compiled", slow, frames, 1.5 * std::chrono::duration<double, std::milli>(period).count(),
                       cap, longest, compiled);
        if (slow) logx::Flush();
        report = now + std::chrono::seconds(10);
        frames = slow = 0;
        longest = 0;
    }
    next += period;
    if (next < now - period) next = now;
    if (g_mid_frame) g_mid_frame();
    while (clock::now() + std::chrono::milliseconds(2) < next) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (clock::now() < next) std::this_thread::yield();
    netplay::Frame();
}

// ---- the displays (settings.cpp's on Windows)

void* Monitor() { return reinterpret_cast<void*>(uintptr_t(Display())); }

std::vector<std::string> Monitors() {
    std::vector<std::string> names;
    int count = 0;
    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (SDL_DisplayID* ids = SDL_GetDisplays(&count)) {
        for (int i = 0; i < count; i++) {
            const char* name = SDL_GetDisplayName(ids[i]);
            SDL_Rect r{};
            SDL_GetDisplayBounds(ids[i], &r);
            names.push_back(std::to_string(i + 1) + ": " + (name ? name : "display") + " (" + std::to_string(r.w) +
                            " x " + std::to_string(r.h) + (ids[i] == primary ? ", primary)" : ")"));
        }
        SDL_free(ids);
    }
    return names;
}

Mode Desktop() {
    if (const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(Display()))
        return {uint32_t(m->w), uint32_t(m->h)};
    return {1920, 1080};
}

std::vector<Mode> DisplayModes() {
    std::vector<Mode> modes;
    int count = 0;
    if (SDL_DisplayMode** list = SDL_GetFullscreenDisplayModes(Display(), &count)) {
        for (int i = 0; i < count; i++) {
            Mode m{uint32_t(list[i]->w), uint32_t(list[i]->h)};
            if (m.height < 480) continue;
            if (std::none_of(modes.begin(), modes.end(), [&](const Mode& o) { return o.width == m.width && o.height == m.height; }))
                modes.push_back(m);
        }
        SDL_free(list);
    }
    std::sort(modes.begin(), modes.end(),
              [](const Mode& a, const Mode& b) { return a.width != b.width ? a.width > b.width : a.height > b.height; });
    return modes;
}

// ---- the Vulkan renderer's hooks (gpu/vulkan.cpp, phase 2)

std::vector<const char*> VulkanInstanceExtensions() {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) LOGE("vulkan: SDL has no instance extensions for the window: %s", SDL_GetError());
    return names ? std::vector<const char*>(names, names + count) : std::vector<const char*>{};
}

bool CreateVulkanSurface(void* instance, void* surface) {
    if (SDL_Vulkan_CreateSurface(g_window, static_cast<VkInstance>(instance), nullptr, static_cast<VkSurfaceKHR*>(surface)))
        return true;
    LOGE("vulkan: no surface for the window: %s", SDL_GetError());
    return false;
}

void* VulkanGetInstanceProcAddr() { return reinterpret_cast<void*>(SDL_Vulkan_GetVkGetInstanceProcAddr()); }

void WindowPixels(uint32_t& width, uint32_t& height) {
    int w = 0, h = 0;
    if (g_window) SDL_GetWindowSizeInPixels(g_window, &w, &h);
    width = uint32_t(std::max(w, 0)), height = uint32_t(std::max(h, 0));
}

#ifdef __ANDROID__
// kernel::Relaunch on Android: the app's GameActivity.relaunch(library, environment) starts the image's library in a
// new process of the app (an exec has no Android form; one process cannot load a second copy of the runtime), with
// this process's XBE_ and NF_ environment and XBE_RELAUNCHED=1 handed over as the exec keeps them on Linux. The launch
// data and the hdd are files beside the library, as on the desktop.
void AndroidRelaunch(const char* exe) {
    setenv("XBE_RELAUNCHED", "1", 1);
    std::vector<std::string> env;
    for (char** e = environ; e && *e; e++)
        if (!std::strncmp(*e, "XBE_", 4) || !std::strncmp(*e, "NF_", 3)) env.push_back(*e);
    auto* jni = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    bool asked = false;
    if (jni && activity) {
        jclass cls = jni->GetObjectClass(activity);
        jmethodID relaunch = jni->GetMethodID(cls, "relaunch", "(Ljava/lang/String;[Ljava/lang/String;)V");
        if (relaunch) {
            jclass string_class = jni->FindClass("java/lang/String");
            jobjectArray list = jni->NewObjectArray(jsize(env.size()), string_class, nullptr);
            for (size_t i = 0; i < env.size(); i++) {
                jstring s = jni->NewStringUTF(env[i].c_str());
                jni->SetObjectArrayElement(list, jsize(i), s);
                jni->DeleteLocalRef(s);
            }
            std::string lib = "lib" + std::string(exe) + ".so";
            jstring name = jni->NewStringUTF(lib.c_str());
            jni->CallVoidMethod(activity, relaunch, name, list);
            asked = !jni->ExceptionCheck();
            if (!asked) jni->ExceptionClear();
        }
        jni->DeleteLocalRef(activity);
    }
    if (!asked) {
        LOGE("relaunch: the app could not start %s", exe);
        std::string text = "The game could not start " + std::string(exe) + " (lib" + exe + ".so): import the game package again.";
        MessageBox(std::wstring(text.begin(), text.end()).c_str(), true);
    } else {
        LOGR("relaunch: lib%s.so in a new process", exe);
    }
    logx::Flush();
    std::_Exit(0);
}
#endif

}  // namespace app

#ifdef __ANDROID__
// SDL's Java side (SDLActivity.nativeRunMain) calls this in the library the app loaded; its arguments may carry the
// environment a relaunch handed over (--env=NAME=VALUE)
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    for (int i = 1; i < argc; i++)
        if (!std::strncmp(argv[i], "--env=", 6))
            if (const char* eq = std::strchr(argv[i] + 6, '=')) setenv(std::string(argv[i] + 6, eq).c_str(), eq + 1, 1);
    // landscape, the fingers not also the mouse (app/touch.cpp reads them), the back gesture the game's pause
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#else
int main(int argc, char** argv) {
    if (const char* t = std::getenv("XBE_TOUCH"); t && *t == '1') SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
#endif
    struct sigaction sa{};
    sa.sa_sigaction = app::OnCrash;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    // the command line as the environment, which an exec into the other image (kernel::Relaunch) keeps
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--instance") && i + 1 < argc) setenv("XBE_INSTANCE", argv[++i], 1);
        else if (!std::strncmp(argv[i], "--instance=", 11)) setenv("XBE_INSTANCE", argv[i] + 11, 1);
        else if (!std::strcmp(argv[i], "--relaunched")) setenv("XBE_RELAUNCHED", "1", 1);
    }
    // one log a session: a relaunch into another image carries on its predecessor's
    const char* relaunch_env = std::getenv("XBE_RELAUNCHED");
    bool relaunched = relaunch_env && *relaunch_env == '1';
    std::string log = app::kGame.log;
    if (app::Instance() > 1) log += "-" + std::to_string(app::Instance());
    logx::Init((app::ExeDir() / "logs").string(), log, relaunched);
    // video first: the settings and the system log ask for the displays
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOGE("window: SDL video failed to start: %s", SDL_GetError());
        logx::Flush();
        return 1;
    }
    app::Load();
    if (app::kGame.init) app::kGame.init();
    if (relaunched && !logx::kVerbose) LOGR("Relaunched: %s", IMAGE_XBE);
    else app::LogSystem();

    app::GameFiles game;
    if (!app::FindGame(game)) return 1;

    bool splash = app::OpenSplash(), handover = app::StartHandover();
    app::g_event_base = SDL_RegisterEvents(1);
    std::string title = app::Utf8(app::kGame.title);
    if (app::Instance()) title += " [" + std::to_string(app::Instance()) + "]";
    app::g_window = SDL_CreateWindow(title.c_str(), 1280, 720,
                                     SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app::g_window || app::g_event_base == 0) {
        LOGE("window: not made: %s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, app::Utf8(app::kGame.short_title).c_str(),
                                 "The game window could not be opened (SDL with Vulkan), see the log in the logs folder.", nullptr);
        logx::Flush();
        return 1;
    }
    // behind a splash the window stays hidden until the game's first frame; handed over, it is set up hidden
    if (handover) {
        app::ApplyNow();
    } else if (!splash) {
        app::ApplyNow();
        SDL_ShowWindow(app::g_window);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForVulkan(app::g_window);
    // the settings window reads controllers from the runtime's input (app::FeedPad), not SDL's backend
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);

    input::Init();
    if (app::kGame.window) app::kGame.window(app::g_window);
    std::string hdd = app::Instance() > 1 ? "hdd" + std::to_string(app::Instance()) : "hdd";
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), (app::ExeDir() / hdd).string())) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, app::Utf8(app::kGame.short_title).c_str(),
                                 "The game failed to start, see the log in the logs folder.", app::g_window);
        logx::Flush();
        return 1;
    }

    // the loop wakes at least every 10 ms: mouse look and the menu pointer follow the game's state, not only events
    for (bool running = true; running;) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 10)) {
            running = app::HandleEvent(e);
            while (running && SDL_PollEvent(&e)) running = app::HandleEvent(e);
        }
        app::UpdateMouse();
    }
    LOGR("the window closed");
    logx::Flush();
    kernel::Shutdown(0);
}

#endif  // !_WIN32
