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
        constexpr int DD_CreateSurface = 6;

        // IDirectDrawSurface (also IDirectDrawSurface2)
        constexpr int SURF_QueryInterface = 0;
        constexpr int SURF_AddAttachedSurface = 3;
        constexpr int SURF_Blt = 5;
        constexpr int SURF_GetSurfaceDesc = 22;
        constexpr int SURF_IsLost = 24;
        constexpr int SURF_Lock = 25;
        constexpr int SURF_Restore = 27;
        constexpr int SURF_SetClipper = 28;
        constexpr int SURF_SetColorKey = 29;
        constexpr int SURF_SetPalette = 31;
        constexpr int SURF_Unlock = 32;

        // IDirect3D2
        constexpr int D3D2_CreateViewport = 6;
        constexpr int D3D2_CreateDevice = 8;

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

    // Number of vtable slots to copy per wrapped interface. These are the
    // exact SDK interface sizes (Win10 SDK d3d.h/ddraw.h) - the maximum slot
    // count the game can legally dispatch through these objects, so the copy
    // never reads past the real vtable.
    constexpr int kDDrawVtblSlots = 24;   // IDirectDraw2 (IDirectDraw has 23)
    constexpr int kSurfaceVtblSlots = 42; // IDirectDrawSurface2
    constexpr int kD3D2VtblSlots = 9;
    constexpr int kDeviceVtblSlots = 33;   // IDirect3DDevice2
    constexpr int kViewportVtblSlots = 18; // IDirect3DViewport2

    // Wrap entry points (implemented in gfx_d3d2.cpp; only gfx_d3d2.cpp
    // wraps objects).
    void wrap_ddraw2(IDirectDraw2* dd2);
    void wrap_surface(IDirectDrawSurface* surface);
    void wrap_d3d2(IDirect3D2* d3d2);
    void wrap_device2(IDirect3DDevice2* device);
    void wrap_viewport2(IDirect3DViewport2* viewport);
}
