// the host: window, message loop, crash dumps; the game is described by app::kGame
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WINVER
#define WINVER 0x0A00  // the touch screen's WM_POINTER messages (Windows 8.1 and up)
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <windowsx.h>
#include <dbghelp.h>
#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include "app/app.h"
#include "core/netplay.h"
#include "core/diag.h"
#include "core/input.h"
#include "core/log.h"
#include "core/netplay.h"
#include "gpu/backend.h"
#include "image_symbols.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace app {

namespace {

HWND g_hwnd = nullptr;
constexpr UINT kApplyDisplay = WM_APP + 1, kOpenWindow = WM_APP + 2;
bool g_mode_changed = false;
wchar_t g_mode_device[32] = {};  // the display whose mode was changed (MONITORINFOEXW::szDevice), restored by name
bool g_apply_posted = false;  // a display apply is waiting in the queue or running
std::atomic<bool> g_active{true};  // the game window is in front (WM_ACTIVATE)

// crash dump in CrashDumps beside the exe
LONG WINAPI WriteCrashDump(EXCEPTION_POINTERS* e) {
    // the dump holds no guest memory: the guest registers and return addresses go to the log
    if (kernel::Thread* t = kernel::Current(); e && t && t->ctx) {
        auto* r = e->ExceptionRecord;
        LOGE("exception %08lX at %p on guest thread %X", r->ExceptionCode, r->ExceptionAddress, t->id);
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
            uint64_t va = r->ExceptionInformation[1], g = uint64_t(reinterpret_cast<uintptr_t>(kernel::g_base));
            LOGE("  %s %llX, guest %08llX", r->ExceptionInformation[0] ? "write" : "read", (unsigned long long)va,
                 (unsigned long long)(va - g));
        }
        kernel::LogGuestStack(*t->ctx, 256);
    }
    auto dir = ExeDir() / "CrashDumps";
    CreateDirectoryW(dir.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf(name, 64, L"%ls.%04u.%02u.%02u_%02u.%02u.%02u.dmp", kGame.name, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond);
    HANDLE f = CreateFileW((dir / name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), e, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                      MINIDUMP_TYPE(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithThreadInfo |
                                    MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
                      e ? &info : nullptr, nullptr, nullptr);
    CloseHandle(f);
    logx::Flush();
    return EXCEPTION_CONTINUE_SEARCH;
}

// F3 with XBE_CAPTURE_DIR: the guest's committed memory too, for a stalled title's state to be read elsewhere
// (sdk/scripts/guestmem.py): "XBEMEM1" and a zero byte, then runs of committed pages, each its address, its length
// and its bytes
void DumpGuestMemory() {
    if (diag::CaptureDir().empty()) return;
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::weakly_canonical(ExeDir() / diag::CaptureDir(), ec);
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path file = dir / ("guest_mem_" + std::to_string(GetTickCount64()) + ".bin");
    FILE* f = _wfopen(file.wstring().c_str(), L"wb");
    if (!f) return;
    std::fwrite("XBEMEM1", 1, 8, f);
    uint64_t total = 0;
    for (uint64_t va = 0x1000; va < 0x100000000ull;) {
        if (!mem::IsCommitted(uint32_t(va))) {
            va += 0x1000;
            continue;
        }
        uint64_t end = va;
        while (end < 0x100000000ull && end - va < 0x1000000 && mem::IsCommitted(uint32_t(end))) end += 0x1000;
        uint32_t head[2] = {uint32_t(va), uint32_t(end - va)};
        std::fwrite(head, 4, 2, f);
        std::fwrite(kernel::HostPtr(uint32_t(va)), 1, size_t(end - va), f);
        total += end - va;
        va = end;
    }
    std::fclose(f);
    LOGR("F3: guest memory, %llu MB, written to %s", (unsigned long long)(total >> 20), file.string().c_str());
}

void DumpThreads() {
    LOGR("F3: thread stacks%s", diag::CaptureDir().empty() ? " (no capture: XBE_CAPTURE_DIR is not set)" : ", the next frame captured");
    gpu::g_capture = true;
    for (auto& t : kernel::Threads())
        if (t->ctx) {
            LOGR("F3: thread %X", t->id);
            kernel::LogGuestStack(*t->ctx, 32);
        }
    DumpGuestMemory();
}

// window thread only
void ApplyNow() {
    const Settings& s = Get();
    HMONITOR mon = static_cast<HMONITOR>(Monitor());
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    // the display the mode was changed on, which need not be the monitor setting's now
    if (g_mode_changed) {
        ChangeDisplaySettingsExW(g_mode_device, nullptr, nullptr, 0, nullptr);
        g_mode_changed = false;
    }
    Mode out = Output();
    LONG_PTR visible = IsWindowVisible(g_hwnd) ? WS_VISIBLE : 0;  // a window held back for a splash or handover stays so
    DEVMODEW desktop{};
    desktop.dmSize = sizeof(desktop);
    EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &desktop);
    // the desktop's own size is no mode change: switching to it anyway blanks the screen for nothing
    if (s.display_mode == DisplayMode::Fullscreen && s.width && s.height &&
        (s.width != desktop.dmPelsWidth || s.height != desktop.dmPelsHeight)) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        dm.dmPelsWidth = s.width;
        dm.dmPelsHeight = s.height;
        dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        LONG r = ChangeDisplaySettingsExW(mi.szDevice, &dm, nullptr, CDS_FULLSCREEN, nullptr);
        g_mode_changed = r == DISP_CHANGE_SUCCESSFUL;
        if (g_mode_changed) wcsncpy_s(g_mode_device, mi.szDevice, _TRUNCATE);
        else LOGR("display: %ux%u on %ls not set (%ld), the desktop's mode kept", s.width, s.height, mi.szDevice, r);
        GetMonitorInfoW(mon, &mi);
    }
    if (s.display_mode == DisplayMode::Windowed) {
        RECT r{0, 0, LONG(out.width), LONG(out.height)};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        int w = r.right - r.left, h = r.bottom - r.top;
        const RECT& a = mi.rcWork;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | visible);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, a.left + (a.right - a.left - w) / 2, a.top + (a.bottom - a.top - h) / 2, w, h,
                     SWP_FRAMECHANGED);
    } else {
        const RECT& m = mi.rcMonitor;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | visible);
        SetWindowPos(g_hwnd, HWND_NOTOPMOST, m.left, m.top, m.right - m.left, m.bottom - m.top, SWP_FRAMECHANGED);
    }
}

// the title's pointer for its menus (GameInfo::menu_cursor), made again when the window's height asks another size
HCURSOR MenuCursor(HWND hwnd) {
    static HCURSOR made = nullptr;
    static int made_size = 0;
    if (!kGame.menu_cursor) return nullptr;
    RECT r;
    GetClientRect(hwnd, &r);
    int size = std::clamp(int(r.bottom - r.top) / 24, 16, 96);
    if (made && size == made_size) return made;
    std::vector<uint32_t> px;
    int hx = 0, hy = 0;
    kGame.menu_cursor(size, px, hx, hy);
    if (px.size() != size_t(size) * size) return made;
    BITMAPV5HEADER bh{};
    bh.bV5Size = sizeof(bh);
    bh.bV5Width = size;
    bh.bV5Height = -size;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000;
    bh.bV5GreenMask = 0x0000FF00;
    bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bh), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return made;
    std::memcpy(bits, px.data(), px.size() * 4);
    // the alpha does the masking, the mask all zero (a monochrome bitmap's rows are WORD aligned)
    std::vector<uint8_t> zeros(size_t((size + 15) / 16 * 2) * size);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros.data());
    ICONINFO ii{FALSE, DWORD(hx), DWORD(hy), mask, color};
    HCURSOR c = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    if (!c) return made;
    if (made) DestroyCursor(made);
    made = c;
    made_size = size;
    return made;
}

// mouse look (Settings::mouse_look): the cursor hidden and held in the window while the mouse plays the right stick, in
// play only: never while a title's menus have the pointer free (input::MouseFree), the settings window is open or the
// window is not in front
bool g_moving = false;  // the window being dragged or sized (WM_ENTERSIZEMOVE to WM_EXITSIZEMOVE, or its title pressed)

void HoldCursor(HWND hwnd) {
    static bool held = false;
    // never while the window is moved or sized, nor with Alt or a Windows key held (to reach the title bar, the taskbar)
    const bool keys = (GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000;
    bool want = input::MouseLooking() && !input::MouseIsFree() && GetForegroundWindow() == hwnd && !IsIconic(hwnd) &&
                !g_moving && !keys;
    RECT r;
    GetClientRect(hwnd, &r);
    MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    // taken only from inside the picture: a pointer on the title bar or a border is left there
    POINT c;
    if (want && !held && (!GetCursorPos(&c) || !PtInRect(&r, c))) want = false;
    if (want) {
        ClipCursor(&r);
    } else if (held) {
        ClipCursor(nullptr);
    }
    held = want;
}

constexpr UINT_PTR kCursorTimer = 0x5243;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // mouse look: raw movement, the cursor kept hidden in the window
    if (msg == WM_INPUT && kGame.mouse_look) {
        RAWINPUT ri;
        UINT size = sizeof(ri);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
            ri.header.dwType == RIM_TYPEMOUSE && (input::MouseLooking() || input::MousePointing())) {
            const RAWMOUSE& m = ri.data.mouse;
            if (!input::MouseLooking()) {
                // the menus' pointer: the cursor's own place is read (FramePoint)
            } else if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                input::MouseMoved(m.lLastX, m.lLastY);
            } else {
                // Remote Desktop (and VMs, tablets) send where the pointer is, 0 to 65535 across the screen or the
                // virtual desktop: its moves from the last, in pixels (none from a stale one, a moment old)
                static LONG last_x = 0, last_y = 0;
                static DWORD last_t = 0;
                const bool virt = m.usFlags & MOUSE_VIRTUAL_DESKTOP;
                const int w = GetSystemMetrics(virt ? SM_CXVIRTUALSCREEN : SM_CXSCREEN),
                          h = GetSystemMetrics(virt ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
                const LONG x = LONG(int64_t(m.lLastX) * w / 65536), y = LONG(int64_t(m.lLastY) * h / 65536);
                const DWORD now = GetTickCount();
                if (last_t && now - last_t < 250) input::MouseMoved(x - last_x, y - last_y);
                last_x = x, last_y = y, last_t = now;
            }
            if (ri.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) input::MouseWheel(short(ri.data.mouse.usButtonData));
        }
        HoldCursor(hwnd);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    // the pointer moved over the window: the mouse is in use, raw input or not (a Remote Desktop session's may be none)
    if (msg == WM_MOUSEMOVE) {
        static LPARAM last = -1;
        if (last != -1 && lp != last)
            input::PointerTravel(std::abs(GET_X_LPARAM(lp) - GET_X_LPARAM(last)) + std::abs(GET_Y_LPARAM(lp) - GET_Y_LPARAM(last)));
        last = lp;
    }
    // no pointer over the game, only over the settings window: the game has none. Windows asks only when the mouse
    // moves, so a timer takes it away from one left standing there
    static bool cursor_timer = false;
    if (!cursor_timer) {
        cursor_timer = true;
        SetTimer(hwnd, kCursorTimer, 500, nullptr);
    }
    // the window dragged or sized: the cursor free until it is let go
    if (msg == WM_ENTERSIZEMOVE || (msg == WM_NCLBUTTONDOWN && wp != HTCLIENT)) {
        g_moving = true;
        ClipCursor(nullptr);
        HoldCursor(hwnd);
    }
    if (msg == WM_EXITSIZEMOVE || msg == WM_NCLBUTTONUP || msg == WM_LBUTTONDOWN) g_moving = false;
    // focus lost (alt-tab, another window clicked): the cursor let go at once
    if ((msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) || (msg == WM_ACTIVATEAPP && !wp) || msg == WM_KILLFOCUS) {
        ClipCursor(nullptr);
        HoldCursor(hwnd);
    }
    if (msg == WM_TIMER && wp == kCursorTimer) {
        if (kGame.mouse_look) HoldCursor(hwnd);  // a menu opened or the focus gone with the mouse standing still
        if (GetForegroundWindow() == hwnd && !OverlayOpen() && !input::MouseIsFree()) {
            POINT p;
            RECT r;
            if (GetCursorPos(&p) && ScreenToClient(hwnd, &p) && GetClientRect(hwnd, &r) && PtInRect(&r, p)) SetCursor(nullptr);
        }
        return 0;
    }
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT) {
        if (kGame.mouse_look) HoldCursor(hwnd);
        if (!OverlayOpen()) {
            // a title's menus with the pointer free: the arrow
            SetCursor(input::MouseIsFree() ? LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW)) : nullptr);
            return TRUE;
        }
        if (HCURSOR c = kGame.menu_up && kGame.menu_up() && !OverlayOpen() ? MenuCursor(hwnd) : nullptr) {
            SetCursor(c);
            return TRUE;
        }
    }
    // a touch screen's fingers (WM_POINTER), for the on-screen touch controls when they are on (app/touch.cpp)
    if (msg == WM_POINTERDOWN || msg == WM_POINTERUPDATE || msg == WM_POINTERUP) {
        POINTER_INFO pi{};
        const uint32_t id = GET_POINTERID_WPARAM(wp);
        if (app::Get().touch_controls && GetPointerInfo(id, &pi) && pi.pointerType == PT_TOUCH) {
            POINT pt = pi.ptPixelLocation;
            ScreenToClient(hwnd, &pt);
            app::TouchPoint(id, msg == WM_POINTERDOWN ? 0 : msg == WM_POINTERUPDATE ? 1 : 2, float(pt.x), float(pt.y));
        }
    }
    // a key binding the settings window waits for takes the next mouse button too
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN) {
        uint16_t vk = msg == WM_LBUTTONDOWN   ? VK_LBUTTON
                      : msg == WM_RBUTTONDOWN ? VK_RBUTTON
                      : msg == WM_MBUTTONDOWN ? VK_MBUTTON
                      : GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? VK_XBUTTON1
                                                           : VK_XBUTTON2;
        if (CaptureKey(vk)) return 0;  // a title's own key binding
        std::lock_guard lock(UiLock());
        if (OverlayKey(vk)) return 0;
    }
    // the settings window: F1 opens and closes it, and a key binding it waits for takes the next key
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        std::lock_guard lock(UiLock());
        if (!(lp & (1 << 30)) && CaptureKey(uint16_t(wp))) return 0;  // a title's binding: not a held key's repeat
        if (wp == VK_F1 && kGame.settings_window && !(lp & (1 << 30))) {
            ToggleOverlay();
            return 0;
        }
        if (OverlayKey(uint16_t(wp))) return 0;
    }
    {
        std::lock_guard lock(UiLock());
        if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    }
    if (intptr_t result = 0; kGame.message && kGame.message(hwnd, msg, wp, lp, &result)) return result;
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_F3) DumpThreads();
            if (wp == VK_F8 && !diag::g_f8_pressed) {
                LOGR("F8: experiments set to start now begin");
                diag::g_f8_pressed = true;
            }
            if (wp == WPARAM(Get().screenshot_key) && !(lp & (1 << 30))) RequestScreenshot();
            if (wp == VK_F4) {
                LOGR("F4: frame log requested");
                diag::g_framelog_request = true;
            }
            return 0;
        case WM_ACTIVATE:
            g_active = LOWORD(wp) != WA_INACTIVE;
            if (LOWORD(wp) == WA_INACTIVE) {
                // a changed display mode steps aside for the desktop, minimized, and comes back on return; not while
                // handing over to the next instance, which runs in the same mode
                if (g_mode_changed && !kernel::g_relaunching) {
                    ChangeDisplaySettingsExW(g_mode_device, nullptr, nullptr, 0, nullptr);
                    g_mode_changed = false;
                    ShowWindow(hwnd, SW_MINIMIZE);
                }
            } else if (Get().display_mode == DisplayMode::Fullscreen && Get().width && Get().height && !g_mode_changed &&
                       IsWindowVisible(hwnd) && !g_apply_posted) {
                // once: restoring the window activates it again from inside the apply, and a second apply switched
                // the mode back and forth, after which presents were not taken for a while (the game stood still
                // until the next alt-tab)
                g_apply_posted = true;
                PostMessageW(hwnd, kApplyDisplay, 0, 0);
            }
            break;
        case kApplyDisplay:
            if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
            ApplyNow();
            g_apply_posted = false;
            return 0;
        case kOpenWindow:
            CloseSplash();
            if (wp) ApplyNow();
            ShowWindow(hwnd, SW_SHOW);
            // to the front only on a first launch, or when the instance handing over was in front: a relaunch (between
            // the front end and a level) with the player in another window leaves that window in front
            if (const wchar_t* arg = std::wcsstr(GetCommandLineW(), L"--handover=")) {
                const DWORD previous = std::wcstoul(arg + 11, nullptr, 10);
                DWORD owner = 0;
                if (HWND fg = GetForegroundWindow()) GetWindowThreadProcessId(fg, &owner);
                if (owner == previous || owner == GetCurrentProcessId()) SetForegroundWindow(hwnd);
            } else {
                SetForegroundWindow(hwnd);
            }
            return 0;
        case WM_CLOSE:
            // who closes it (a relaunched instance once ended at once, before its handover, with no quit asked for)
            LOGR("window: WM_CLOSE (%s, %s)", InSendMessage() ? "sent from another thread or process" : "posted or sent here",
                 app::HandingOver() ? "while handing over" : "after the handover");
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            LOGR("window: WM_DESTROY");
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

std::filesystem::path ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return std::filesystem::path(p).parent_path();
}

int Instance() {
    static const int n = [] {
        const wchar_t* p = std::wcsstr(GetCommandLineW(), L"--instance");
        return p ? std::max(0, std::min(8, _wtoi(p + 10))) : 0;
    }();
    return n;
}

void* Window() { return g_hwnd; }

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
    if (!g_hwnd) return false;
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(g_hwnd, &pt);
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    float w = float(rc.right - rc.left), h = float(rc.bottom - rc.top);
    if (w <= 0 || h <= 0 || pt.x < 0 || pt.y < 0 || pt.x >= rc.right || pt.y >= rc.bottom) return false;
    // the frame at its shape, centred (the renderers' BlitFrame), then the 2D's own shape in its middle
    float aspect = FrameAspect();
    float vw = std::min(w, h * aspect), vh = vw / aspect;
    u = (pt.x - (w - vw) / 2) / vw;
    v = (pt.y - (h - vh) / 2) / vh;
    if (aspect > kGame.hud_aspect + 1e-3f) u = 0.5f + (u - 0.5f) * aspect / kGame.hud_aspect;
    return true;
}

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void ApplyDisplay() { PostMessageW(g_hwnd, kApplyDisplay, 0, 0); }
void Quit() {
    LOGR("quit asked for");
    PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
}
void OpenGameWindow(bool apply) { SendMessageW(g_hwnd, kOpenWindow, apply, 0); }

bool Paused() { return !netplay::Active() && Get().pause_unfocused && !g_active && g_hwnd && IsWindowVisible(g_hwnd); }

static void (*g_mid_frame)() = nullptr;
void SetMidFrame(void (*fn)()) { g_mid_frame = fn; }

void Pace(uint32_t vblanks) {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    // pause_unfocused: the title waits here, the sound output stops pulling the mixer (so the title's sample clock
    // stands still too)
    if (Paused()) {
        LOGR("paused: the window is not in front");
        while (Paused()) Sleep(10);
        LOGR("resumed");
        next = clock::now();
    }
    // LAN play: every copy at 60 a second whatever its cap, as the frames are the title's clocks (netplay::Frame)
    uint32_t cap = PaceRate(netplay::Active() ? 60 : Get().fps_cap, vblanks);
    auto now = clock::now();
    if (!cap) {  // NF_UNCAPPED=1 (diagnostic): the frame's work time, averaged over 10 s
        static clock::time_point last_u = now, report_u = now + std::chrono::seconds(10);
        static double work_u = 0;
        static uint32_t n_u = 0;
        work_u += std::chrono::duration<double, std::milli>(now - last_u).count(), n_u++;
        last_u = now;
        if (now >= report_u && n_u) {
            LOGR("frames uncapped: %.2f ms a frame on average (%.0f a second)", work_u / n_u, 1000.0 * n_u / work_u);
            work_u = 0, n_u = 0, report_u = now + std::chrono::seconds(10);
        }
        next = now;
        netplay::Frame(60);
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    // stutter: frames that took more than one and a half periods, a line every 10 seconds that had any (loading
    // screens count too)
    static clock::time_point last = now, report = now + std::chrono::seconds(10);
    static uint32_t frames = 0, slow = 0;
    static double longest = 0;
    double took = std::chrono::duration<double, std::milli>(now - last).count();
    last = now;
    frames++;
    if (took > 1.5 * std::chrono::duration<double, std::milli>(period).count()) slow++, longest = std::max(longest, took);
    if (now >= report) {
        // shaders compiled the first time a draw needed them: the usual cause of a stall past loading
        uint32_t compiled = gpu::g_shaders_compiled.exchange(0);
        if (slow) LOGR("frames: %u of %u in the last 10 s over %.1f ms (%u a second paced), the longest %.1f ms; "
                       "%u shaders compiled", slow, frames, 1.5 * std::chrono::duration<double, std::milli>(period).count(),
                       cap, longest, compiled);
        if (slow) logx::Flush();  // kept when the game is closed by force
        report = now + std::chrono::seconds(10);
        frames = slow = 0;
        longest = 0;
    }
    next += period;
    if (next < now - period) next = now;
    // the renderer's frame in between (smooth turning): asked for now, drawn by it half way to the next present
    if (g_mid_frame) g_mid_frame();
    // NF_PERF_WORK (the capped run): the frame's work, the period less the time this frame's pacing waits
    static double waited_last = 0, work_sum = 0;
    static uint32_t work_n = 0;
    static clock::time_point work_report = now + std::chrono::seconds(10);
    work_sum += took - waited_last, work_n++;
    if (now >= work_report && work_n) {
        LOGR("frame work: %.2f ms a frame on average (paced at %u a second)", work_sum / work_n, cap);
        work_sum = 0, work_n = 0, work_report = now + std::chrono::seconds(10);
    }
    const auto wait_from = clock::now();
    while (clock::now() + std::chrono::milliseconds(2) < next) Sleep(1);
    while (clock::now() < next) std::this_thread::yield();
    waited_last = std::chrono::duration<double, std::milli>(clock::now() - wait_from).count();
    netplay::Frame(cap);  // LAN play: the next frame's controllers of every player
}

}  // namespace app

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetUnhandledExceptionFilter(app::WriteCrashDump);
    std::signal(SIGABRT, [](int) { app::WriteCrashDump(nullptr); });
    // the C runtime's invalid parameter (a bad printf argument, a closed handle...) ends the process at once, without
    // the filter above: logged and dumped first
    _set_invalid_parameter_handler([](const wchar_t* expr, const wchar_t* fn, const wchar_t* file, unsigned line, uintptr_t) {
        LOGE("the C runtime's invalid parameter handler: %ls in %ls (%ls:%u)", expr ? expr : L"?", fn ? fn : L"?",
             file ? file : L"?", line);
        app::WriteCrashDump(nullptr);
        std::_Exit(3);
    });
    timeBeginPeriod(1);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // one log a session: a relaunch into another image carries on its predecessor's
    bool relaunched = std::wcsstr(GetCommandLineW(), L"--relaunched") != nullptr;
    std::string log = app::kGame.log;
    if (app::Instance() > 1) log += "-" + std::to_string(app::Instance());
    logx::Init((app::ExeDir() / "logs").string(), log, relaunched);
    app::Load();
    if (app::kGame.init) app::kGame.init();
    if (relaunched && !logx::kVerbose) LOGR("Relaunched: %s", IMAGE_XBE);
    else app::LogSystem();

    app::GameFiles game;
    if (!app::FindGame(game)) return 1;
    if (std::wcsstr(GetCommandLineW(), L"--prepare-code")) return app::PrepareGameCodeHeadless(game);
    if (!app::PrepareGameCode(game)) return 1;

    bool splash = app::OpenSplash(), handover = app::StartHandover();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = app::WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = app::kGame.name;
    RegisterClassExW(&wc);
    app::g_hwnd = CreateWindowExW(0, wc.lpszClassName, app::kGame.title, WS_POPUP, 0, 0, 1280, 720,
                                  nullptr, nullptr, inst, nullptr);
    // mouse look: the mouse's raw movement (WM_INPUT), not the cursor's, which stops at the screen's edge
    if (app::kGame.mouse_look) {
        RAWINPUTDEVICE rid{0x01, 0x02, 0, app::g_hwnd};  // generic desktop, mouse
        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) LOGW("input: raw mouse input not registered (%lu)", GetLastError());
    }
    // behind a splash the window stays hidden until the game's first frame; handed over, it is set up hidden
    if (handover) {
        app::ApplyNow();
    } else if (!splash) {
        app::ApplyNow();
        ShowWindow(app::g_hwnd, SW_SHOW);
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    app::OverlayInit();
    ImGui_ImplWin32_Init(app::g_hwnd);

    input::Init();
    if (app::kGame.window) app::kGame.window(app::g_hwnd);
    std::wstring hdd = app::Instance() > 1 ? L"hdd" + std::to_wstring(app::Instance()) : L"hdd";
    if (app::Instance())
        SetWindowTextW(app::g_hwnd, (std::wstring(app::kGame.title) + L" [" + std::to_wstring(app::Instance()) + L"]").c_str());
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), (app::ExeDir() / hdd).string())) {
        MessageBoxW(app::g_hwnd, L"The game failed to start, see the log in the logs folder.", app::kGame.short_title,
                    MB_ICONERROR);
        return 1;
    }

    MSG msg;
    BOOL got;
    while ((got = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (app::g_mode_changed) ChangeDisplaySettingsW(nullptr, 0);
    // why the loop ended: WM_QUIT (got 0, its code) or an error (got -1); and whether the window was destroyed
    LOGR("the window closed (GetMessage %d, quit code %llu, error %lu, window %s, %s)", int(got),
         (unsigned long long)msg.wParam, got < 0 ? GetLastError() : 0ul, IsWindow(app::g_hwnd) ? "still there" : "destroyed",
         app::HandingOver() ? "before its handover" : "after its handover");
    logx::Flush();
    kernel::Shutdown(0);
}
