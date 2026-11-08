#pragma once
// off Windows (the headless harness, the Linux build): the few Win32 names portable runtime and game files touch. This
// folder is on the include path only off Windows, so <windows.h> finds this there and Windows' own on Windows.
#include <cstddef>
#include <cstdint>

typedef long HRESULT;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HWND;
typedef void* HCURSOR;
typedef const wchar_t* PCWSTR;
typedef unsigned int UINT;
typedef int BOOL;
typedef int32_t LONG;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef float FLOAT;
#define WINAPI
#ifndef TRUE
#define TRUE 1
#endif
#define S_OK ((HRESULT)0)
#define E_FAIL ((HRESULT)0x80004005L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

struct RECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};
struct POINT {
    LONG x;
    LONG y;
};

inline HMODULE GetModuleHandleW(const wchar_t*) { return nullptr; }
inline void* GetProcAddress(HMODULE, const char*) { return nullptr; }
inline HANDLE GetCurrentThread() { return nullptr; }

// window messages and keys a game's GameInfo::message names; no window sends them here
enum : uint32_t {
    WM_ACTIVATE = 0x0006, WM_SETCURSOR = 0x0020, WM_INPUT = 0x00FF, WM_KEYDOWN = 0x0100, WM_SYSKEYDOWN = 0x0104,
    WM_LBUTTONUP = 0x0202,
};
enum { WA_INACTIVE = 0, HTCLIENT = 1 };
#include "core/vk_codes.h"  // the virtual key codes
#define LOWORD(v) ((uint16_t)((uintptr_t)(v) & 0xFFFF))
#define MAKEINTRESOURCEW(i) ((const wchar_t*)(uintptr_t)(uint16_t)(i))
inline HCURSOR LoadCursorW(void*, const wchar_t*) { return nullptr; }
inline HCURSOR SetCursor(HCURSOR) { return nullptr; }
inline short GetAsyncKeyState(int) { return 0; }
