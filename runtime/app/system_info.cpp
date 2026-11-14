// the log's opening lines: what the game runs on and with which settings
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>
#else
#if defined(__x86_64__)
#include <cpuid.h>
#endif
#include <sys/utsname.h>
#include <unistd.h>

#include <fstream>
#include <thread>
extern char** environ;
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <vector>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"

#ifdef _WIN32
#pragma comment(lib, "advapi32.lib")
#endif

namespace app {

namespace {

#ifdef _WIN32
std::string RegString(HKEY key, const char* name) {
    char text[256] = {};
    DWORD size = sizeof(text) - 1;
    if (RegQueryValueExA(key, name, nullptr, nullptr, reinterpret_cast<LPBYTE>(text), &size) != ERROR_SUCCESS) return {};
    return text;
}

std::string OsVersion() {
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ | KEY_WOW64_64KEY,
                      &key) != ERROR_SUCCESS)
        return "unknown";
    std::string name = RegString(key, "ProductName"), display = RegString(key, "DisplayVersion"),
                build = RegString(key, "CurrentBuildNumber");
    DWORD ubr = 0, size = sizeof(ubr);
    RegQueryValueExA(key, "UBR", nullptr, nullptr, reinterpret_cast<LPBYTE>(&ubr), &size);
    RegCloseKey(key);
    // Windows 11 still names itself Windows 10 here, its build number tells
    if (auto ten = name.find("Windows 10"); ten != std::string::npos && std::atoi(build.c_str()) >= 22000)
        name.replace(ten, 10, "Windows 11");
    if (!display.empty()) name += " " + display;
    if (!build.empty()) name += " (build " + build + "." + std::to_string(ubr) + ")";
    using WineVersion = const char*(__cdecl*)();
    if (auto wine = reinterpret_cast<WineVersion>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version"))))
        name += std::string(" on Wine ") + wine();
    return name;
}
#else
// /etc/os-release's PRETTY_NAME and the kernel's release
std::string OsVersion() {
    std::string name;
    std::ifstream f("/etc/os-release");
    for (std::string line; std::getline(f, line);)
        if (line.rfind("PRETTY_NAME=", 0) == 0) {
            name = line.substr(12);
            name.erase(std::remove(name.begin(), name.end(), '"'), name.end());
        }
    utsname u{};
    if (uname(&u) == 0) name += std::string(name.empty() ? "" : ", ") + u.sysname + " " + u.release;
    return name.empty() ? "unknown" : name;
}
#endif

std::string CpuName() {
    char brand[49] = {};
#ifdef _WIN32
    int r[4] = {};
    __cpuid(r, int(0x80000000));
    if (uint32_t(r[0]) < 0x80000004) return "unknown";
    for (int i = 0; i < 3; i++) {
        __cpuid(r, int(0x80000002 + i));
        std::memcpy(brand + i * 16, r, 16);
    }
#elif defined(__x86_64__)
    unsigned r[4] = {};
    if (__get_cpuid_max(0x80000000, nullptr) < 0x80000004) return "unknown";
    for (unsigned i = 0; i < 3; i++) {
        __get_cpuid(0x80000002 + i, &r[0], &r[1], &r[2], &r[3]);
        std::memcpy(brand + i * 16, r, 16);
    }
#else
    // Arm: /proc/cpuinfo's Hardware line where the kernel gives one, else the architecture
    std::ifstream info("/proc/cpuinfo");
    for (std::string line; std::getline(info, line);)
        if (line.rfind("Hardware", 0) == 0 && line.find(':') != std::string::npos) return line.substr(line.find(':') + 2);
    return "arm64";
#endif
    std::string s = brand;
    s.erase(0, s.find_first_not_of(' '));
    s.erase(s.find_last_not_of(' ') + 1);
    return s;
}

}  // namespace

void LogSystem() {
    std::time_t now = std::time(nullptr);
    char date[32] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    LOGR("%ls (%s), %s", kGame.title, IMAGE_XBE, date);
#ifdef _WIN32
    LOGR("Path: %ls", ExeDir().c_str());
#else
    LOGR("Path: %s", ExeDir().c_str());
#endif
    LOGR("OS: %s", OsVersion().c_str());
#ifdef _WIN32
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    LOGR("CPU: %s (%lu threads)", CpuName().c_str(), si.dwNumberOfProcessors);
    MEMORYSTATUSEX mem{sizeof(mem)};
    GlobalMemoryStatusEx(&mem);
    LOGR("RAM: %.1f GB", mem.ullTotalPhys / 1073741824.0);
    // one line per adapter, each monitor reports its own
    std::vector<std::string> seen;
    for (DWORD i = 0;; i++) {
        DISPLAY_DEVICEA dd{sizeof(dd)};
        if (!EnumDisplayDevicesA(nullptr, i, &dd, 0)) break;
        if (std::find(seen.begin(), seen.end(), dd.DeviceString) != seen.end()) continue;
        seen.emplace_back(dd.DeviceString);
        LOGR("GPU %zu: %s%s", seen.size(), dd.DeviceString, (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? " (primary)" : "");
    }
#else
    LOGR("CPU: %s (%u threads)", CpuName().c_str(), std::thread::hardware_concurrency());
    long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    LOGR("RAM: %.1f GB", pages > 0 && page > 0 ? double(pages) * double(page) / 1073741824.0 : 0.0);
    // the GPU is the Vulkan renderer's to name; the displays here
    for (const std::string& m : Monitors()) LOGR("Display %s", m.c_str());
#endif
    Mode desk = Desktop();
    LOGR("Desktop: %ux%u", desk.width, desk.height);
#ifdef _WIN32
    DEVMODEW display{};
    display.dmSize = sizeof(display);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &display)) LOGR("Display: %u Hz", unsigned(display.dmDisplayFrequency));
#endif
    LogSettings();
    // experiment and diagnostic switches stay set in a shell or the user's environment between runs: list them
    std::wstring game;
    for (const char* c = kGame.log; *c; c++) game += wchar_t(std::toupper(static_cast<unsigned char>(*c)));
    game += L"_";
#ifdef _WIN32
    // the wide block: the Windows SDK names the narrow one GetEnvironmentStrings only without UNICODE
    if (wchar_t* env = GetEnvironmentStringsW()) {
        for (const wchar_t* e = env; *e; e += std::wcslen(e) + 1)
            if (std::wcsncmp(e, L"XBE_", 4) == 0 || std::wcsncmp(e, game.c_str(), game.size()) == 0) LOGR("Environment: %ls", e);
        FreeEnvironmentStringsW(env);
    }
#else
    std::string prefix(game.begin(), game.end());
    for (char** e = environ; e && *e; e++)
        if (std::strncmp(*e, "XBE_", 4) == 0 || std::strncmp(*e, prefix.c_str(), prefix.size()) == 0) LOGR("Environment: %s", *e);
#endif
}

}  // namespace app
