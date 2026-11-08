#pragma once
// off Windows: the MSVC names FSR 2's sources (and its headers' users) take for granted, forced in ahead of them by
// cmake/XbeRecomp.cmake; FFX_GCC is defined beside it
#include <stddef.h>
#include <string.h>
#include <wchar.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifdef __cplusplus
#include <locale>  // std::wstring_convert, which MSVC's <codecvt> brings along

template <size_t N>
inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t* src) {
    wcsncpy(dst, src, N - 1);
    dst[N - 1] = 0;
    return 0;
}
#endif
