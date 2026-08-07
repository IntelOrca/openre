#pragma once

#include "gfx_backend.h"

namespace openre::gfx
{
    // Vtable slot indexes (offset / 4) for the legacy interfaces we wrap.
    // Verified against the Win10 SDK d3d.h/ddraw.h layouts.
    namespace slots
    {
        // IDirectDraw / IDirectDraw2
        constexpr int DD_QueryInterface = 0;
        constexpr int DD_CreatePalette = 5;
        constexpr int DD_CreateSurface = 6;

        // IDirectDrawPalette
        constexpr int PAL_GetEntries = 4;
        constexpr int PAL_SetEntries = 6;

        // IDirectDrawSurface (also IDirectDrawSurface2)
        constexpr int SURF_QueryInterface = 0;
        constexpr int SURF_AddRef = 1;
        constexpr int SURF_Release = 2;
        constexpr int SURF_AddAttachedSurface = 3;
        constexpr int SURF_Blt = 5;
        constexpr int SURF_GetDC = 17;
        constexpr int SURF_ReleaseDC = 26;
        constexpr int SURF_GetSurfaceDesc = 22;
        constexpr int SURF_IsLost = 24;
        constexpr int SURF_Lock = 25;
        constexpr int SURF_Restore = 27;
        constexpr int SURF_SetClipper = 28;
        constexpr int SURF_SetColorKey = 29;
        constexpr int SURF_SetPalette = 31;
        constexpr int SURF_Unlock = 32;

        // IDirect3D2
        constexpr int D3D2_EnumDevices = 3;
        constexpr int D3D2_CreateMaterial = 5;
        constexpr int D3D2_CreateViewport = 6;
        constexpr int D3D2_CreateDevice = 8;

        // IDirect3DMaterial2
        constexpr int MAT_SetMaterial = 3;

        // IDirect3DTexture2
        constexpr int TEX_GetHandle = 3;
        constexpr int TEX_Load = 5;

        // IDirect3DDevice2
        constexpr int DEV_GetStats = 5;
        constexpr int DEV_BeginScene = 10;
        constexpr int DEV_EndScene = 11;
        constexpr int DEV_SetCurrentViewport = 13;
        constexpr int DEV_SetRenderTarget = 15;
        constexpr int DEV_SetRenderState = 23;
        constexpr int DEV_SetTransform = 26;
        constexpr int DEV_MultiplyTransform = 28;
        constexpr int DEV_DrawPrimitive = 29;
        constexpr int DEV_DrawIndexedPrimitive = 30;

        // IDirect3DViewport / IDirect3DViewport2
        constexpr int VP_SetBackground = 8;
        constexpr int VP_Clear = 12;
        constexpr int VP_SetViewport2 = 17;
    }

    // Number of vtable slots to copy per wrapped interface. The real DDraw
    // objects implement several interfaces on one shared vtable, so the actual
    // vtable is much larger than the interface we wrap (confirmed at runtime:
    // IDirect3D2's real vtable has 64+ valid entries). We copy a generous
    // fixed size so internal DDraw code that dispatches through `this->lpVtbl`
    // at higher slots still finds a valid pointer. The over-read stays within
    // ddraw.dll's readable data segment.
    constexpr int kDDrawVtblSlots = 64;
    constexpr int kSurfaceVtblSlots = 64;
    constexpr int kPaletteVtblSlots = 64;
    constexpr int kD3D2VtblSlots = 64;
    constexpr int kDeviceVtblSlots = 64;
    constexpr int kViewportVtblSlots = 64;
    constexpr int kMaterialVtblSlots = 64;
    constexpr int kTextureVtblSlots = 64;

    // Wrap entry points (implemented in gfx_d3d2.cpp; only gfx_d3d2.cpp
    // wraps objects).
    void wrap_ddraw2(IDirectDraw2* dd2);
    void wrap_surface(IDirectDrawSurface* surface);
    void wrap_palette(IDirectDrawPalette* palette);
    void wrap_d3d2(IDirect3D2* d3d2);
    void wrap_device2(IDirect3DDevice2* device);
    void wrap_viewport2(IDirect3DViewport2* viewport);
    void wrap_material2(IDirect3DMaterial2* material);
    void wrap_texture2(IDirect3DTexture2* texture);
    // Wraps an IDirect3DTexture2 obtained from `surface` via
    // QueryInterface(IID_IDirect3DTexture2) so its GetHandle/Load reach the
    // backends, and records the texture -> base surface mapping.
    void wrap_texture_from_surface(IDirect3DTexture2* texture, IDirectDrawSurface* surface);
}
