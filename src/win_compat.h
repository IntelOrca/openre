#pragma once

// ============================================================================
// win_compat.h - portable mirrors of the small set of Windows types that
// survive in the codebase (RECT, HWND, HRESULT, DWORD, __stdcall, ...).
//
//  * On _WIN32 the REAL <windows.h> is always used.
//  * Everywhere else plain POD mirrors with the legacy Win32 ABI are defined
//    so the code compiles without <windows.h>.
//
// NOMINMAX is defined before windows.h (needed by std::min/std::max users).
// WIN32_LEAN_AND_MEAN is NOT defined here: several existing TUs rely on the
// full windows.h surface, and marni_movie.h defines it itself when needed.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#else // !_WIN32 - portable mirrors

using BYTE = uint8_t;
using WORD = uint16_t;
using DWORD = uint32_t;
using LONG = int32_t;
using BOOL = int;
using HRESULT = int32_t;
using HANDLE = void*;
using HWND = void*;
using HDC = void*;
using HINSTANCE = void*;
using LPVOID = void*;
using LPCVOID = const void*;
using LPCSTR = const char*;
using LPSTR = char*;
using LPCWSTR = const wchar_t*;
using LPWSTR = wchar_t*;
using LPDWORD = DWORD*;

struct RECT
{
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};
using LPRECT = RECT*;
using LPCRECT = const RECT*;

inline void SetRect(RECT* rc, int left, int top, int right, int bottom)
{
    rc->left = left;
    rc->top = top;
    rc->right = right;
    rc->bottom = bottom;
}

struct GUID
{
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

#ifndef __stdcall
#define __stdcall
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef STDMETHODCALLTYPE
#define STDMETHODCALLTYPE
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

#ifndef ZeroMemory
inline void ZeroMemory(void* dest, size_t count)
{
    std::memset(dest, 0, count);
}
#endif
#ifndef FAILED
inline bool FAILED(HRESULT hr)
{
    return hr < 0;
}
#endif
#ifndef SUCCEEDED
inline bool SUCCEEDED(HRESULT hr)
{
    return hr >= 0;
}
#endif

// ABI sanity checks (POD mirrors only - the real SDK header is authoritative
// on Windows). The game's ABI is 32-bit, so the fixed sizes only hold when
// pointers are 4 bytes.
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(RECT) == 16, "win_compat: RECT must be 16 bytes");
static_assert(sizeof(GUID) == 16, "win_compat: GUID must be 16 bytes");
#endif // UINTPTR_MAX == UINT32_MAX

#endif // !_WIN32
