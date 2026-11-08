// What a title asks of the renderer's frames and what it reads back from them (gpu/style.h): screenshots of the frame
// as shown, and the last frame's timings and sizes for a performance overlay.
#include <atomic>
#include <mutex>
#include <string>

#include "gpu/style.h"

namespace gpu {
namespace {
std::mutex g_lock;
std::wstring g_asked, g_done;
bool g_pending = false, g_done_ok = false;
uint32_t g_done_count = 0;
FrameStats g_stats;
}  // namespace

void RequestScreenshot(const std::wstring& path) {
    std::lock_guard lock(g_lock);
    g_asked = path;
    g_pending = true;
}

bool ScreenshotPending() {
    std::lock_guard lock(g_lock);
    return g_pending;
}

bool TakeScreenshotRequest(std::wstring& path) {
    std::lock_guard lock(g_lock);
    if (g_asked.empty()) return false;
    path = std::move(g_asked);
    g_asked.clear();
    return true;
}

void ScreenshotWritten(const std::wstring& path, bool ok) {
    std::lock_guard lock(g_lock);
    g_done = path, g_done_ok = ok;
    g_done_count++;
    g_pending = !g_asked.empty();
}

uint32_t ScreenshotResult(std::wstring& path, bool& ok) {
    std::lock_guard lock(g_lock);
    path = g_done, ok = g_done_ok;
    return g_done_count;
}

void SetFrameStats(const FrameStats& s) {
    std::lock_guard lock(g_lock);
    g_stats = s;
}

FrameStats LastFrameStats() {
    std::lock_guard lock(g_lock);
    return g_stats;
}

}  // namespace gpu
