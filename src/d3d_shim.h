#pragma once

// ============================================================================
// d3d_shim.h - portable ABI-compatible mirrors of the legacy DirectDraw/D3D7
// types used by the GfxBackend interface (system_gpu_backend.h) and the marni
// headers (marni.h, marni_movie.h), so the non-Windows build can compile them
// without <windows.h> / <d3d.h> / <ddraw.h>.
//
// Decisions:
//  * On _WIN32 the REAL <windows.h> is always used (RECT, HWND, HRESULT,
//    DWORD, __stdcall, ...), and <d3d.h>/<ddraw.h> are additionally included
//    when the D3D-shaped front-end is compiled in (!OPENRE_NO_D3D). Windows
//    builds therefore see exactly the same types as before - the game is a
//    DX2-era title and its D3D/DDraw code depends on the legacy header
//    layouts, so the shim must never replace them on Windows.
//  * Everywhere else (non-Windows) plain POD mirrors with the legacy
//    DDK/DX-era ABI are defined: the CLASSIC DDSURFACEDESC (108 bytes, no
//    dwCaps2/3/4), the 32-byte DDPIXELFORMAT and the 24-byte D3DSTATS. All
//    values were verified against the mingw-w64 (Wine) headers, which mirror
//    the MS SDK layouts.
//  * NOMINMAX is defined before windows.h (needed by std::min/std::max users).
//    WIN32_LEAN_AND_MEAN is NOT defined here: several existing TUs rely on the
//    full windows.h surface, and marni_movie.h defines it itself when needed.
//  * On non-Windows the COM interfaces are opaque (never instantiated/called);
//    they exist only so the interface signatures compile. LPDIRECTDRAW2 etc.
//    are pointers to those opaque structs.
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#if !defined(OPENRE_NO_D3D)
#include <d3d.h>
#include <ddraw.h>
#endif

#else // !_WIN32 - portable mirrors

// ---------------------------------------------------------------------------
// Basic Windows-compatible types
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Opaque COM interfaces (never called on non-Windows; used as pointer types)
// ---------------------------------------------------------------------------
struct IUnknown
{
};
struct IDirectDraw;
struct IDirectDraw2;
struct IDirectDrawSurface;
struct IDirectDrawPalette;
struct IDirectDrawClipper;
struct IDirect3D;
struct IDirect3D2;
struct IDirect3DDevice;
struct IDirect3DDevice2;
struct IDirect3DViewport;
struct IDirect3DViewport2;
struct IDirect3DMaterial;
struct IDirect3DMaterial2;
struct IDirect3DMaterial3;
struct IDirect3DTexture;
struct IDirect3DTexture2;
struct IDirect3DLight;

using LPDIRECTDRAW = IDirectDraw*;
using LPDIRECTDRAW2 = IDirectDraw2*;
using LPDIRECTDRAWSURFACE = IDirectDrawSurface*;
using LPDIRECTDRAWPALETTE = IDirectDrawPalette*;
using LPDIRECTDRAWCLIPPER = IDirectDrawClipper*;
using LPDIRECT3D = IDirect3D*;
using LPDIRECT3D2 = IDirect3D2*;
using LPDIRECT3DDEVICE = IDirect3DDevice*;
using LPDIRECT3DDEVICE2 = IDirect3DDevice2*;
using LPDIRECT3DVIEWPORT = IDirect3DViewport*;
using LPDIRECT3DVIEWPORT2 = IDirect3DViewport2*;
using LPDIRECT3DMATERIAL = IDirect3DMaterial*;
using LPDIRECT3DMATERIAL2 = IDirect3DMaterial2*;
using LPDIRECT3DMATERIAL3 = IDirect3DMaterial3*;
using LPDIRECT3DTEXTURE = IDirect3DTexture*;
using LPDIRECT3DTEXTURE2 = IDirect3DTexture2*;
using LPDIRECT3DLIGHT = IDirect3DLight*;

// ---------------------------------------------------------------------------
// D3D enums (values match the real d3dtypes.h; verified against mingw-w64)
// ---------------------------------------------------------------------------
using D3DVALUE = float;
using D3DCOLOR = DWORD;
using D3DMATERIALHANDLE = DWORD;
using D3DTEXTUREHANDLE = DWORD;

// The render states the game actually uses (system_gpu.cpp's RenderStateId
// enum mirrors this exact set; the numbering matches real d3d.h).
enum D3DRENDERSTATETYPE
{
    D3DRENDERSTATE_TEXTUREHANDLE = 1,
    D3DRENDERSTATE_ANTIALIAS = 2,
    D3DRENDERSTATE_TEXTUREADDRESS = 3,
    D3DRENDERSTATE_TEXTUREPERSPECTIVE = 4,
    D3DRENDERSTATE_ZENABLE = 7,
    D3DRENDERSTATE_SHADEMODE = 9,
    D3DRENDERSTATE_ZWRITEENABLE = 14,
    D3DRENDERSTATE_LASTPIXEL = 16,
    D3DRENDERSTATE_TEXTUREMAG = 17,
    D3DRENDERSTATE_TEXTUREMIN = 18,
    D3DRENDERSTATE_SRCBLEND = 19,
    D3DRENDERSTATE_DESTBLEND = 20,
    D3DRENDERSTATE_TEXTUREMAPBLEND = 21,
    D3DRENDERSTATE_CULLMODE = 22,
    D3DRENDERSTATE_ZFUNC = 23,
    D3DRENDERSTATE_ALPHABLENDENABLE = 27,
    D3DRENDERSTATE_SPECULARENABLE = 29,
    D3DRENDERSTATE_SUBPIXEL = 31,
    D3DRENDERSTATE_EDGEANTIALIAS = 40,
    D3DRENDERSTATE_COLORKEYENABLE = 41,
    D3DRENDERSTATE_ANISOTROPY = 49,
};

enum D3DPRIMITIVETYPE
{
    D3DPT_POINTLIST = 1,
    D3DPT_LINELIST = 2,
    D3DPT_LINESTRIP = 3,
    D3DPT_TRIANGLELIST = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN = 6,
};

enum D3DVERTEXTYPE
{
    D3DVT_VERTEX = 1,
    D3DVT_LVERTEX = 2,
    D3DVT_TLVERTEX = 3,
};

enum D3DTRANSFORMSTATETYPE
{
    D3DTRANSFORMSTATE_WORLD = 1,
    D3DTRANSFORMSTATE_VIEW = 2,
    D3DTRANSFORMSTATE_PROJECTION = 3,
    D3DTRANSFORMSTATE_WORLD1 = 4,
    D3DTRANSFORMSTATE_WORLD2 = 5,
    D3DTRANSFORMSTATE_WORLD3 = 6,
    D3DTRANSFORMSTATE_TEXTURE0 = 16,
    D3DTRANSFORMSTATE_TEXTURE1 = 17,
    D3DTRANSFORMSTATE_TEXTURE2 = 18,
    D3DTRANSFORMSTATE_TEXTURE3 = 19,
    D3DTRANSFORMSTATE_TEXTURE4 = 20,
    D3DTRANSFORMSTATE_TEXTURE5 = 21,
    D3DTRANSFORMSTATE_TEXTURE6 = 22,
    D3DTRANSFORMSTATE_TEXTURE7 = 23,
};

enum D3DBLEND
{
    D3DBLEND_ZERO = 1,
    D3DBLEND_ONE = 2,
    D3DBLEND_SRCCOLOR = 3,
    D3DBLEND_INVSRCCOLOR = 4,
    D3DBLEND_SRCALPHA = 5,
    D3DBLEND_INVSRCALPHA = 6,
    D3DBLEND_DESTALPHA = 7,
    D3DBLEND_INVDESTALPHA = 8,
    D3DBLEND_DESTCOLOR = 9,
    D3DBLEND_INVDESTCOLOR = 10,
    D3DBLEND_SRCALPHASAT = 11,
    D3DBLEND_BOTHSRCALPHA = 12,
    D3DBLEND_BOTHINVSRCALPHA = 13,
};

enum D3DCMPFUNC
{
    D3DCMP_NEVER = 1,
    D3DCMP_LESS = 2,
    D3DCMP_EQUAL = 3,
    D3DCMP_LESSEQUAL = 4,
    D3DCMP_GREATER = 5,
    D3DCMP_NOTEQUAL = 6,
    D3DCMP_GREATEREQUAL = 7,
    D3DCMP_ALWAYS = 8,
};

enum D3DCULL
{
    D3DCULL_NONE = 1,
    D3DCULL_CW = 2,
    D3DCULL_CCW = 3,
};

enum D3DSHADEMODE
{
    D3DSHADE_FLAT = 1,
    D3DSHADE_GOURAUD = 2,
    D3DSHADE_PHONG = 3,
};

enum D3DTEXTUREBLEND
{
    D3DTBLEND_DECAL = 1,
    D3DTBLEND_MODULATE = 2,
    D3DTBLEND_DECALALPHA = 3,
    D3DTBLEND_MODULATEALPHA = 4,
    D3DTBLEND_DECALMASK = 5,
    D3DTBLEND_MODULATEMASK = 6,
    D3DTBLEND_COPY = 7,
    D3DTBLEND_ADD = 8,
};

enum D3DTEXTUREADDRESS
{
    D3DTADDRESS_WRAP = 1,
    D3DTADDRESS_MIRROR = 2,
    D3DTADDRESS_CLAMP = 3,
    D3DTADDRESS_BORDER = 4,
};

enum D3DTEXTUREFILTER
{
    D3DFILTER_NEAREST = 1,
    D3DFILTER_LINEAR = 2,
    D3DFILTER_MIPNEAREST = 3,
    D3DFILTER_MIPLINEAR = 4,
    D3DFILTER_LINEARMIPNEAREST = 5,
    D3DFILTER_LINEARMIPLINEAR = 6,
};

enum D3DZBUFFERTYPE
{
    D3DZB_FALSE = 0,
    D3DZB_TRUE = 1,
    D3DZB_USEW = 2,
};

// ---------------------------------------------------------------------------
// D3D structs (real ABI layout)
// ---------------------------------------------------------------------------
struct D3DCOLORVALUE
{
    float r;
    float g;
    float b;
    float a;
};

struct D3DVECTOR
{
    float x;
    float y;
    float z;
};

struct D3DRECT
{
    LONG x1;
    LONG y1;
    LONG x2;
    LONG y2;
};

struct D3DMATRIX
{
    float _11, _12, _13, _14;
    float _21, _22, _23, _24;
    float _31, _32, _33, _34;
    float _41, _42, _43, _44;
};

struct D3DVIEWPORT2
{
    DWORD dwSize;
    DWORD dwX;
    DWORD dwY;
    DWORD dwWidth;
    DWORD dwHeight;
    D3DVALUE dvClipX;
    D3DVALUE dvClipY;
    D3DVALUE dvClipWidth;
    D3DVALUE dvClipHeight;
    D3DVALUE dvMinZ;
    D3DVALUE dvMaxZ;
};
using LPD3DVIEWPORT2 = D3DVIEWPORT2*;

struct D3DMATERIAL
{
    DWORD dwSize;
    D3DCOLORVALUE diffuse;
    D3DCOLORVALUE ambient;
    D3DCOLORVALUE specular;
    D3DCOLORVALUE emissive;
    D3DVALUE power;
    D3DTEXTUREHANDLE hTexture;
    DWORD dwRampSize;
};
using LPD3DMATERIAL = D3DMATERIAL*;

struct D3DTLVERTEX
{
    float sx;
    float sy;
    float sz;
    float rhw;
    D3DCOLOR color;
    D3DCOLOR specular;
    float tu;
    float tv;
};
using LPD3DTLVERTEX = D3DTLVERTEX*;

// The classic D3D2-era layout the game reads back (dwSize + 5 counters).
struct D3DSTATS
{
    DWORD dwSize;
    DWORD dwTrianglesDrawn;
    DWORD dwLinesDrawn;
    DWORD dwPointsDrawn;
    DWORD dwSpansDrawn;
    DWORD dwVerticesProcessed;
};
using LPD3DSTATS = D3DSTATS*;

// ---------------------------------------------------------------------------
// DirectDraw structs (classic DDK-era layout; DDSURFACEDESC is 108 bytes)
// ---------------------------------------------------------------------------
struct DDCOLORKEY
{
    DWORD dwColorSpaceLowValue;
    DWORD dwColorSpaceHighValue;
};

struct DDSCAPS
{
    DWORD dwCaps;
};
using LPDDSCAPS = DDSCAPS*;

struct DDPIXELFORMAT
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    union
    {
        DWORD dwRGBBitCount;
        DWORD dwYUVBitCount;
        DWORD dwZBufferBitDepth;
        DWORD dwAlphaBitDepth;
        DWORD dwLuminanceBitCount;
        DWORD dwBumpBitCount;
        DWORD dwPrivateFormatBitCount;
    };
    union
    {
        DWORD dwRBitMask;
        DWORD dwYBitMask;
        DWORD dwStencilBitDepth;
        DWORD dwLuminanceBitMask;
        DWORD dwBumpDuBitMask;
        DWORD dwOperations;
    };
    union
    {
        DWORD dwGBitMask;
        DWORD dwUBitMask;
        DWORD dwZBitMask;
        DWORD dwBumpDvBitMask;
    };
    union
    {
        DWORD dwBBitMask;
        DWORD dwVBitMask;
        DWORD dwStencilBitMask;
        DWORD dwBumpLuminanceBitMask;
    };
    union
    {
        DWORD dwRGBAlphaBitMask;
        DWORD dwYUVAlphaBitMask;
        DWORD dwLuminanceAlphaBitMask;
        DWORD dwRGBZBitMask;
        DWORD dwYUVZBitMask;
    };
};
using LPDDPIXELFORMAT = DDPIXELFORMAT*;

struct DDSURFACEDESC
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwHeight;
    DWORD dwWidth;
    union
    {
        LONG lPitch;
        DWORD dwLinearSize;
    };
    DWORD dwBackBufferCount;
    union
    {
        DWORD dwMipMapCount;
        DWORD dwZBufferBitDepth;
        DWORD dwRefreshRate;
    };
    DWORD dwAlphaBitDepth;
    DWORD dwReserved;
    LPVOID lpSurface;
    DDCOLORKEY ddckCKDestOverlay;
    DDCOLORKEY ddckCKDestBlt;
    DDCOLORKEY ddckCKSrcOverlay;
    DDCOLORKEY ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS ddsCaps;
};
using LPDDSURFACEDESC = DDSURFACEDESC*;

struct DDBLTFX
{
    DWORD dwSize;
    DWORD dwDDFX;
    DWORD dwROP;
    DWORD dwDDROP;
    DWORD dwRotationAngle;
    DWORD dwZBufferOpCode;
    DWORD dwZBufferLow;
    DWORD dwZBufferHigh;
    DWORD dwZBufferBaseDest;
    DWORD dwZDestConstBitDepth;
    union
    {
        DWORD dwZDestConst;
        LPDIRECTDRAWSURFACE lpDDSZBufferDest;
    };
    DWORD dwZSrcConstBitDepth;
    union
    {
        DWORD dwZSrcConst;
        LPDIRECTDRAWSURFACE lpDDSZBufferSrc;
    };
    DWORD dwAlphaEdgeBlendBitDepth;
    DWORD dwAlphaEdgeBlend;
    DWORD dwReserved;
    DWORD dwAlphaDestConstBitDepth;
    union
    {
        DWORD dwAlphaDestConst;
        LPDIRECTDRAWSURFACE lpDDSAlphaDest;
    };
    DWORD dwAlphaSrcConstBitDepth;
    union
    {
        DWORD dwAlphaSrcConst;
        LPDIRECTDRAWSURFACE lpDDSAlphaSrc;
    };
    union
    {
        DWORD dwFillColor;
        DWORD dwFillDepth;
        DWORD dwFillPixel;
        LPDIRECTDRAWSURFACE lpDDSPattern;
    };
    DDCOLORKEY ddckDestColorkey;
    DDCOLORKEY ddckSrcColorkey;
};
using LPDDBLTFX = DDBLTFX*;

struct PALETTEENTRY
{
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
};
using LPPALETTEENTRY = PALETTEENTRY*;

// ---------------------------------------------------------------------------
// HRESULT codes and DDraw/D3D flag constants (values match the SDK headers)
// ---------------------------------------------------------------------------
constexpr HRESULT S_OK = 0;
constexpr HRESULT S_FALSE = 1;
constexpr HRESULT E_NOTIMPL = 0x80004001;
constexpr HRESULT E_NOINTERFACE = 0x80004002;
constexpr HRESULT E_POINTER = 0x80004003;
constexpr HRESULT E_FAIL = 0x80004005;
constexpr HRESULT E_OUTOFMEMORY = 0x8007000E;
constexpr HRESULT E_INVALIDARG = 0x80070057;

constexpr HRESULT DD_OK = S_OK;
// MAKE_DDHRESULT(450); facility 0x876.
constexpr HRESULT DDERR_SURFACELOST = 0x887601C2;
// MAKE_DDHRESULT(540).
constexpr HRESULT DDERR_WASSTILLDRAWING = 0x8876021C;

// DDSURFACEDESC.dwFlags
constexpr DWORD DDSD_CAPS = 0x00000001;
constexpr DWORD DDSD_HEIGHT = 0x00000002;
constexpr DWORD DDSD_WIDTH = 0x00000004;
constexpr DWORD DDSD_PITCH = 0x00000008;
constexpr DWORD DDSD_BACKBUFFERCOUNT = 0x00000020;
constexpr DWORD DDSD_ZBUFFERBITDEPTH = 0x00000040;
constexpr DWORD DDSD_ALPHABITDEPTH = 0x00000080;
constexpr DWORD DDSD_LPSURFACE = 0x00000800;
constexpr DWORD DDSD_PIXELFORMAT = 0x00001000;
constexpr DWORD DDSD_CKDESTOVERLAY = 0x00002000;
constexpr DWORD DDSD_CKDESTBLT = 0x00004000;
constexpr DWORD DDSD_CKSRCOVERLAY = 0x00008000;
constexpr DWORD DDSD_CKSRCBLT = 0x00010000;
constexpr DWORD DDSD_MIPMAPCOUNT = 0x00020000;
constexpr DWORD DDSD_REFRESHRATE = 0x00040000;
constexpr DWORD DDSD_LINEARSIZE = 0x00080000;
constexpr DWORD DDSD_ALL = 0x00fff9ee;

// DDSCAPS.dwCaps
constexpr DWORD DDSCAPS_BACKBUFFER = 0x00000004;
constexpr DWORD DDSCAPS_COMPLEX = 0x00000008;
constexpr DWORD DDSCAPS_FLIP = 0x00000010;
constexpr DWORD DDSCAPS_FRONTBUFFER = 0x00000020;
constexpr DWORD DDSCAPS_OFFSCREENPLAIN = 0x00000040;
constexpr DWORD DDSCAPS_PALETTE = 0x00000100;
constexpr DWORD DDSCAPS_PRIMARYSURFACE = 0x00000200;
constexpr DWORD DDSCAPS_SYSTEMMEMORY = 0x00000800;
constexpr DWORD DDSCAPS_TEXTURE = 0x00001000;
constexpr DWORD DDSCAPS_3DDEVICE = 0x00002000;
constexpr DWORD DDSCAPS_VIDEOMEMORY = 0x00004000;
constexpr DWORD DDSCAPS_ZBUFFER = 0x00020000;
constexpr DWORD DDSCAPS_HWCODEC = 0x00100000;
constexpr DWORD DDSCAPS_ALLOCONLOAD = 0x04000000;
constexpr DWORD DDSCAPS_LOCALVIDMEM = 0x10000000;

// DDPIXELFORMAT.dwFlags
constexpr DWORD DDPF_ALPHAPIXELS = 0x00000001;
constexpr DWORD DDPF_ALPHA = 0x00000002;
constexpr DWORD DDPF_FOURCC = 0x00000004;
constexpr DWORD DDPF_PALETTEINDEXED4 = 0x00000008;
constexpr DWORD DDPF_PALETTEINDEXED8 = 0x00000020;
constexpr DWORD DDPF_RGB = 0x00000040;
constexpr DWORD DDPF_COMPRESSED = 0x00000080;
constexpr DWORD DDPF_RGBTOYUV = 0x00000100;
constexpr DWORD DDPF_YUV = 0x00000200;
constexpr DWORD DDPF_ZBUFFER = 0x00000400;
constexpr DWORD DDPF_ZPIXELS = 0x00002000;

// DDBLT.dwFlags (blit flags)
constexpr DWORD DDBLT_ASYNC = 0x00000200;
constexpr DWORD DDBLT_COLORFILL = 0x00000400;
constexpr DWORD DDBLT_DDFX = 0x00000800;
constexpr DWORD DDBLT_KEYSRC = 0x00008000;
constexpr DWORD DDBLT_WAIT = 0x01000000;
constexpr DWORD DDBLT_DONOTWAIT = 0x08000000;

// DDBLTFX.dwDDFX
constexpr DWORD DDBLTFX_NOTEARING = 0x00000008;

// IDirectDrawSurface::SetColorKey flags
constexpr DWORD DDCKEY_COLORSPACE = 0x00000001;
constexpr DWORD DDCKEY_DESTBLT = 0x00000002;
constexpr DWORD DDCKEY_DESTOVERLAY = 0x00000004;
constexpr DWORD DDCKEY_SRCBLT = 0x00000008;
constexpr DWORD DDCKEY_SRCOVERLAY = 0x00000010;

// IDirectDraw::SetCooperativeLevel flags
constexpr DWORD DDSCL_FULLSCREEN = 0x00000001;
constexpr DWORD DDSCL_NOWINDOWCHANGES = 0x00000004;
constexpr DWORD DDSCL_NORMAL = 0x00000008;
constexpr DWORD DDSCL_EXCLUSIVE = 0x00000010;
constexpr DWORD DDSCL_ALLOWMODEX = 0x00000040;

// D3D clear / DrawPrimitive flags
constexpr DWORD D3DCLEAR_TARGET = 0x00000001;
constexpr DWORD D3DCLEAR_ZBUFFER = 0x00000002;
constexpr DWORD D3DCLEAR_STENCIL = 0x00000004;
constexpr DWORD D3DDP_WAIT = 0x00000001;
constexpr DWORD D3DDP_OUTOFORDER = 0x00000002;
constexpr DWORD D3DDP_DONOTCLIP = 0x00000004;
constexpr DWORD D3DDP_DONOTUPDATEEXTENTS = 0x00000008;
constexpr DWORD D3DDP_DONOTLIGHT = 0x00000010;

// ---------------------------------------------------------------------------
// Small helpers (guarded: windows.h already provides these on Windows)
// ---------------------------------------------------------------------------
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

// ABI sanity checks (POD mirrors only - the real SDK headers are authoritative
// on Windows, where some structs grew over time). The game's ABI is 32-bit, so
// the fixed sizes only hold when pointers are 4 bytes.
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(RECT) == 16, "d3d_shim: RECT must be 16 bytes");
static_assert(sizeof(GUID) == 16, "d3d_shim: GUID must be 16 bytes");
static_assert(sizeof(DDCOLORKEY) == 8, "d3d_shim: DDCOLORKEY must be 8 bytes");
static_assert(sizeof(DDSCAPS) == 4, "d3d_shim: DDSCAPS must be 4 bytes");
static_assert(sizeof(DDPIXELFORMAT) == 32, "d3d_shim: DDPIXELFORMAT must be 32 bytes");
static_assert(sizeof(DDSURFACEDESC) == 108, "d3d_shim: DDSURFACEDESC must be 108 bytes");
static_assert(sizeof(DDBLTFX) == 100, "d3d_shim: DDBLTFX must be 100 bytes");
static_assert(sizeof(D3DCOLORVALUE) == 16, "d3d_shim: D3DCOLORVALUE must be 16 bytes");
static_assert(sizeof(D3DVECTOR) == 12, "d3d_shim: D3DVECTOR must be 12 bytes");
static_assert(sizeof(D3DRECT) == 16, "d3d_shim: D3DRECT must be 16 bytes");
static_assert(sizeof(D3DMATRIX) == 64, "d3d_shim: D3DMATRIX must be 64 bytes");
static_assert(sizeof(D3DVIEWPORT2) == 44, "d3d_shim: D3DVIEWPORT2 must be 44 bytes");
static_assert(sizeof(D3DMATERIAL) == 80, "d3d_shim: D3DMATERIAL must be 80 bytes");
static_assert(sizeof(D3DTLVERTEX) == 32, "d3d_shim: D3DTLVERTEX must be 32 bytes");
static_assert(sizeof(D3DSTATS) == 24, "d3d_shim: D3DSTATS must be 24 bytes");
static_assert(sizeof(PALETTEENTRY) == 4, "d3d_shim: PALETTEENTRY must be 4 bytes");
static_assert(offsetof(DDPIXELFORMAT, dwRGBBitCount) == 12, "d3d_shim: DDPIXELFORMAT.dwRGBBitCount must be at 12");
static_assert(offsetof(DDSURFACEDESC, ddpfPixelFormat) == 72, "d3d_shim: DDSURFACEDESC.ddpfPixelFormat must be at 72");
static_assert(offsetof(DDSURFACEDESC, ddsCaps) == 104, "d3d_shim: DDSURFACEDESC.ddsCaps must be at 104");
static_assert(offsetof(DDBLTFX, dwFillColor) == 80, "d3d_shim: DDBLTFX.dwFillColor must be at 80");
static_assert(offsetof(D3DMATERIAL, ambient) == 20, "d3d_shim: D3DMATERIAL.ambient must be at 20");
static_assert(offsetof(D3DSTATS, dwTrianglesDrawn) == 4, "d3d_shim: D3DSTATS.dwTrianglesDrawn must be at 4");
static_assert(offsetof(D3DSTATS, dwVerticesProcessed) == 20, "d3d_shim: D3DSTATS.dwVerticesProcessed must be at 20");
#endif // UINTPTR_MAX == UINT32_MAX

#endif // !_WIN32
