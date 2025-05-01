#include "marni.h"
#include "interop.hpp"
#include "openre.h"
#include "re2.h"

#define WIN32_LEAN_AND_MEAN
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>

namespace openre::marni
{
    namespace GpuFlags
    {
        constexpr uint32_t GPU_7 = 0x80;
        constexpr uint32_t GPU_9 = 0x200;
        constexpr uint32_t GPU_FULLSCREEN = 0x400;
        constexpr uint32_t GPU_13 = 0x2000;
    }

    // 0x00443620
    void mapping_tmd(int workNo, Md1* pTmd, int id)
    {
        interop::call<void, int, Md1*, int>(0x00443620, workNo, pTmd, id);
    }

    static void out_internal(const char* message, const char* location)
    {
        // std::printf("[marni] %s: %s\n", location, message);
    }

    // 0x004DBFD0
    void out(const char* message, const char* location)
    {
        out_internal(message, location);
    }

    // 0x004DBFD0
    void out()
    {
        out_internal("", "");
    }

    // 0x00401000
    int error(HRESULT hr)
    {
        return interop::call(0x00401000);
    }

    // 0x00432BB0
    void unload_door_texture()
    {
        interop::call(0x00432BB0);
    }

    // 0x00442E40
    bool sub_442E40()
    {
        using sig = bool (*)();
        auto p = (sig)0x00442E40;
        return p();
    }

    // 0x0043F550
    void unload_texture_page(int page)
    {
        auto& tp = gGameTable.texture_pages[page];
        if (tp.handle != 0)
        {
            marni::unload_texture(gGameTable.pMarni, tp.handle);
        }
        tp.handle = 0;
        tp.var_04 = 0;
        tp.var_08 = 0;
        update_timer();
    }

    // 0x00441710
    void flush_surfaces()
    {
        interop::call(0x00441710);
    }

    // 0x00432C60
    void door_disp0(int doorId, int a1, int a2, int a3)
    {
        interop::call<void, int, int, int>(0x00432C60, doorId, a1, a2, a3);
    }

    // 0x00432CD0
    void door_disp1(int doorId)
    {
        interop::call<void, int>(0x00432CD0, doorId);
    }

    // 0x00441520
    void result_unload_textures()
    {
        static constexpr uint32_t pages[26]
            = { 0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33 };
        for (const auto page : pages)
        {
            unload_texture_page(page);
        }
    }

    // 0x00406450
    static void move(Marni* marni)
    {
        HWND window = (HWND)marni->hWnd;
        RECT rect = {};
        rect.left = marni->window_rect[0];
        rect.top = marni->window_rect[1];
        rect.right = marni->window_rect[2];
        rect.bottom = marni->window_rect[3];

        POINT point0 = {};
        ClientToScreen(window, &point0);
        POINT point1 = {};
        point1.x = marni->resolutions[marni->modes].width;
        point1.y = marni->resolutions[marni->modes].height;
        ClientToScreen(window, &point1);

        SetRect(&rect, point0.x, point0.y, point1.x, point1.y);
        marni->window_rect[0] = rect.left;
        marni->window_rect[1] = rect.top;
        marni->window_rect[2] = rect.right;
        marni->window_rect[3] = rect.bottom;
    }

    // 0x004065C0
    static void resize(Marni* marni, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        interop::thiscall<int, Marni*, HWND, UINT, WPARAM, LPARAM>(0x004065C0, marni, hWnd, msg, wParam, lParam);
    }

    // 0x004064D0
    static void destroy(Marni* marni)
    {
        interop::thiscall<int, Marni*>(0x004064D0, marni);
    }

    // 0x00401F10
    static void syskeydown(Marni* marni)
    {
        interop::thiscall<int, Marni*>(0x00401F10, marni);
    }

    // 0x004063D0
    long message(Marni* self, void* hWnd, uint32_t msg, void* wParam, void* lParam)
    {
        switch (msg)
        {
        case WM_MOVE: move(self); break;
        case WM_SIZE: resize(self, (HWND)hWnd, msg, (WPARAM)wParam, (LPARAM)lParam); break;
        case WM_DESTROY: destroy(self); break;
        case WM_SYSKEYDOWN:
            if ((self->gpu_flag & GpuFlags::GPU_FULLSCREEN) != 0)
            {
                syskeydown(self);
            }
            break;
        }
        return 1;
    }

    // 0x004419A0
    void kill()
    {
        interop::call(0x004419A0);
    }

    // 0x00402500
    bool change_resolution(Marni* self)
    {
        return interop::thiscall<bool, Marni*>(0x00402500, self);
    }

    // 0x0050B220
    void config_flip_filter(MarniConfig* self)
    {
        self->bilinear ^= 1;
    }

    static void surface_release(MarniSurface* self)
    {
        interop::thiscall<int, MarniSurface*>((uintptr_t)self->vtbl[8], self);
    }

    static void surface_fill(MarniSurface* self, uint8_t r, uint8_t g, uint8_t b)
    {
        interop::thiscall<int, MarniSurface*, uint8_t, uint8_t, uint8_t>((uintptr_t)self->vtbl[0], self, r, g, b);
    }

    static void flip_blt(Marni* self, DWORD width, DWORD height)
    {
        auto src = ((LPDIRECTDRAWSURFACE2)self->surface0.pDDsurface);
        auto dst = ((LPDIRECTDRAWSURFACE2)self->surface2.pDDsurface);

        RECT srcRect;
        SetRect(&srcRect, 0, 0, width, height);

        RECT dstRect;
        CopyRect(&dstRect, (LPRECT)self->window_rect);

        DDBLTFX ddbltfx;
        ZeroMemory(&ddbltfx, sizeof(DDBLTFX));
        ddbltfx.dwSize = sizeof(DDBLTFX);
        ddbltfx.dwDDFX = DDBLTFX_NOTEARING;

        dst->Blt(&dstRect, src, &srcRect, DDBLT_DDFX | DDBLT_WAIT, &ddbltfx);
    }

    // 0x00402A80
    static void flip(Marni* self)
    {
        if (self->var_8C7EE0)
            return;
        if (self->pMovie->flag & 2)
            return;
        if (!(self->gpu_flag & GpuFlags::GPU_9))
            return;
        if ((self->gpu_flag & GpuFlags::GPU_13) && (self->gpu_flag & GpuFlags::GPU_FULLSCREEN) && self->var_8C8318 < 4)
        {
            surface_fill(&self->surface2, 0, 0, 0);
        }

        if (self->gpu_flag & GpuFlags::GPU_FULLSCREEN)
        {
            auto fullscreen = self->resolutions[self->modes].fullscreen;
            if (fullscreen == 1)
            {
                auto surface0 = (LPDIRECTDRAWSURFACE7)self->surface0.pDDsurface;
                auto surface2 = (LPDIRECTDRAWSURFACE7)self->surface2.pDDsurface;
                surface2->Flip(surface0, DDFLIP_WAIT);
            }
            else if (fullscreen == 2 || fullscreen == 3)
            {
                flip_blt(self, (int32_t)(self->render_w * self->aspect_x), (int32_t)(self->render_h * self->aspect_y));
            }
        }
        else
        {
            flip_blt(self, self->xsize, self->ysize);
        }
    }

    // 0x00407440
    static int create_d3d(Marni* self)
    {
        if (self->gpu_flag & GpuFlags::GPU_13)
            return 0;

        auto dd2 = (LPDIRECTDRAW2)self->pDirectDraw2;
        gGameTable.error = dd2->QueryInterface(IID_IDirect3D2, &self->pDirect3D2);
        if (gGameTable.error != 0)
        {
            error(gGameTable.error);
        }
        return gGameTable.error;
    }

    static BOOL CALLBACK ddrawEnumCallback(GUID* lpGUID, LPSTR lpName, LPSTR lpDesc, LPVOID lpContext)
    {
        auto lpDDresult = (LPDIRECTDRAW*)lpContext;
        LPDIRECTDRAW lpDD;
        if (lpGUID != NULL && SUCCEEDED(DirectDrawCreate(lpGUID, &lpDD, NULL)))
        {
            DDCAPS ddcapsHW;
            ZeroMemory(&ddcapsHW, sizeof(DDCAPS));
            ddcapsHW.dwSize = sizeof(DDCAPS);
            if (SUCCEEDED(lpDD->GetCaps(&ddcapsHW, NULL)) && (ddcapsHW.dwCaps & DDCAPS_3D))
            {
                *lpDDresult = lpDD;
                return FALSE;
            }
            lpDD->Release();
        }
        return TRUE;
    }

    static int __stdcall com_nop(LPUNKNOWN obj)
    {
        return 0;
    }

    static void clear_com_interface(LPUNKNOWN obj, size_t methodCount)
    {
        auto newTable = new void*[methodCount];
        for (size_t i = 0; i < methodCount; i++)
        {
            newTable[i] = com_nop;
        }

        auto vtable = (void**)obj;
        vtable[0] = newTable;
    }

    // 0x0040F1A0
    static int create_ddraw(bool bEnumDevices, LPDIRECTDRAW* lplpDD, LPDWORD lpIsDefault)
    {
        LPDIRECTDRAW lpDD = NULL;
        if (bEnumDevices)
        {
            auto hr = DirectDrawEnumerateA(ddrawEnumCallback, (LPVOID)&lpDD);
            if (FAILED(hr))
            {
                out("Direct3DEnumerate", "MarniSystem Direct3D::MDDCreateDirect3D");
                return hr;
            }
        }
        if (lpDD == NULL)
        {
            auto hr = DirectDrawCreate(NULL, &lpDD, NULL);
            if (FAILED(hr))
            {
                out("Direct3D", "MarniSystem Direct3D::MDDCreateDirect3D");
                return hr;
            }
            *lpIsDefault = 1;
        }
        else
        {
            *lpIsDefault = 0;
        }
        *lplpDD = lpDD;

        return 0;
    }

    // 0x00406860
    static int query_ddraw2(LPDIRECTDRAW pDD, LPDIRECTDRAW2* lpDD2)
    {
        return pDD->QueryInterface(IID_IDirectDraw2, (LPVOID*)lpDD2);
    }

    // 0x00406970
    static int D3DIBPPToDDBD(int bpp)
    {
        switch (bpp)
        {
        case 1: return DDBD_1;
        case 2: return DDBD_2;
        case 4: return DDBD_4;
        case 8: return DDBD_8;
        case 16: return DDBD_16;
        case 24: return DDBD_24;
        case 32: return DDBD_32;
        default: out("", "D3DIBPPToDDBD"); return 0;
        }
    }

    // 0x00407290
    static HRESULT CALLBACK enum_driver_callback(
        GUID* lpGuid, LPSTR lpDeviceDescription, LPSTR lpDeviceName, LPD3DDEVICEDESC descSw, LPD3DDEVICEDESC descHw,
        LPVOID lpContext)
    {
        auto& device = gGameTable.d3d_devices[gGameTable.d3d_device_count];
        memcpy(device.GUID, lpGuid, sizeof(GUID));
        strncpy(device.lpDeviceDescription, lpDeviceDescription, sizeof(device.lpDeviceDescription));
        strncpy(device.lpDeviceName, lpDeviceName, sizeof(device.lpDeviceName));
        if (descSw->dwFlags == 0)
        {
            device.hwAccelerated = 0;
            memcpy(device.desc, descHw, sizeof(D3DDEVICEDESC));
        }
        else
        {
            device.hwAccelerated = 1;
            memcpy(device.desc, descSw, sizeof(D3DDEVICEDESC));
        }
        gGameTable.d3d_device_count++;
        return gGameTable.d3d_device_count < 4 ? D3DENUMRET_OK : D3DENUMRET_CANCEL;
    }

    // 0x00407340
    static int enum_drivers(Marni* self)
    {
        if (self->gpu_flag & GpuFlags::GPU_13)
            return 1;

        auto pD3D2 = (LPDIRECT3D2)self->pDirect3D2;
        gGameTable.d3d_device_count = 0;
        gGameTable.error = pD3D2->EnumDevices(enum_driver_callback, NULL);
        if (FAILED(gGameTable.error))
        {
            out("failed to detect drivers that can use.", "MarniSystem Direct3D::MD3D");
            return 0;
        }

        auto bestScore = -1;
        for (auto i = 0; i < gGameTable.d3d_device_count; i++)
        {
            auto& device = gGameTable.d3d_devices[i];
            auto desc = (LPD3DDEVICEDESC)device.desc;
            device.supportsFloat = desc->dwDevCaps & D3DDEVCAPS_FLOATTLVERTEX;
            device.supportsZbuffer = desc->dwDeviceZBufferBitDepth != 0;
            device.hwAccelerated2 = device.hwAccelerated;
            auto supportsDepth
                = (D3DIBPPToDDBD(self->bpp) & desc->dwDeviceRenderBitDepth) != 0 && ((self->gpu_flag & GpuFlags::GPU_7) != 0);
            auto score = (device.supportsZbuffer != 0) + (supportsDepth ? 2 : 0) + (device.supportsFloat != 0)
                + (device.hwAccelerated != 0 ? 4 : 0);
            if (bestScore < score)
            {
                bestScore = score;
                self->device_cnt = i;
            }
        }
        return 1;
    }

    // 0x0040F2F0
    static HRESULT dd_set_coop_level(HWND hWnd, int fullscreen, LPDIRECTDRAW pDD)
    {
        if (fullscreen)
        {
            auto hr = pDD->SetCooperativeLevel(hWnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
            if (FAILED(hr))
            {
                out("SetCooperativeLevel to fullscreen failed", "MarniSystem DDSetCoopLevel");
                return hr;
            }
        }
        else
        {
            auto hr = pDD->SetCooperativeLevel(hWnd, DDSCL_NORMAL);
            if (FAILED(hr))
            {
                out("SetCooperativeLevel to normal failed", "MarniSystem DDSetCoopLevel");
                return hr;
            }

            hr = pDD->RestoreDisplayMode();
            if (FAILED(hr))
            {
                error(hr);
            }
        }
        return S_OK;
    }

    // 0x0040ECA0
    static int surfacex_create_texture_object(MarniSurfaceX* self)
    {
        if (!self->bOpen)
        {
            out("", "Direct3DSurface::CreateTextureObject");
            return 0;
        }

        auto pDDtexture = (LPDIRECT3DTEXTURE)self->pDDtexture;
        if (pDDtexture != NULL)
        {
            pDDtexture->Release();
            self->pDDtexture = NULL;
        }

        auto pDDsurface = (LPDIRECTDRAWSURFACE)self->pDDsurface;
        auto hr = pDDsurface->QueryInterface(IID_IDirect3DTexture2, (LPVOID*)&pDDtexture);
        if (FAILED(hr))
        {
            out("", "Direct3DSurface::CreateTextureObject");
            error(hr);
            surface_release(self);
            return 0;
        }

        self->pDDtexture = pDDtexture;
        return 1;
    }

    // 0x004149D0
    void surface2_ctor(MarniSurface2* self)
    {
        std::memset(self, 0, sizeof(*self));
        self->vtbl = (void**)0x005173B0;
    }

    // 0x00414A30
    void surface2_release(MarniSurface2* self)
    {
        self->vtbl = (void**)0x005173B0;
        surface2_vrelease(self);
    }

    // 0x00414A40
    void surface2_vrelease(MarniSurface2* self)
    {
        interop::thiscall<int, MarniSurface2*>(0x00414A40, self);
    }

    // 0x00405EC0
    int create_texture_handle(Marni* self, MarniSurface2* pSrcSurface, uint32_t mode)
    {
        return interop::thiscall<int, Marni*, MarniSurface2*, uint32_t>(0x00405EC0, self, pSrcSurface, mode);
    }

    // 0x004022E0
    static void request_video_memory(Marni* self)
    {
        interop::thiscall<int, Marni*>(0x004022E0, self);
    }

    // 0x00416BE0
    static void sub_416BE0(Marni* self, int handle)
    {
        interop::thiscall<int, Marni*, int>(0x00416BE0, self, handle);
    }

    // 0x00404CE0
    void unload_texture(Marni* self, int handle)
    {
        interop::thiscall<int, Marni*, int>(0x00404CE0, self, handle);
    }

    void init_hooks()
    {
        interop::hookThisCall(0x00406450, &move);
        interop::hookThisCall(0x00402A80, &flip);
        interop::hookThisCall(0x00407440, &create_d3d);
        interop::hookThisCall(0x00407340, &enum_drivers);
        interop::hookThisCall(0x0040ECA0, &surfacex_create_texture_object);
        interop::writeJmp(0x0040F1A0, &create_ddraw);
        interop::writeJmp(0x00406860, &query_ddraw2);
        interop::writeJmp(0x004DBFD0, &out_internal);
        interop::writeJmp(0x0040F2F0, &dd_set_coop_level);
    }
}
