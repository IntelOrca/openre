#include "marni_movie.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "re2.h"

#include <cstring>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <dshow.h>
#include <windows.h>

// Movie playback is implemented with DirectShow (IGraphBuilder/RenderFile),
// which reproduces the original RE2 approach and correctly decodes the MPEG-1
// Program Stream .bin movie files.  Windows Media Foundation's source reader
// and media engine do not expose a usable decoder for the MPG1 video subtype
// on Windows 10 (the Microsoft MPEG Video Decoder MFT exists but is not
// reachable from IMFSourceReader::SetCurrentMediaType).
//
// MarniMovie field reuse:
//   pGraphBuilder  - IGraphBuilder*
//   pMediaControl  - IMediaControl*
//   pVideoWindow   - IVideoWindow*
//   pMediaPosition - IMediaPosition*
//   field_6C       - child HWND (retrieved from IVideoWindow::get_Owner)
//   flag / pos / duration  - same as before

namespace openre::marni
{
    // 0x00414B30
    int __stdcall sub_414B30(MarniMovie* self)
    {
        if (!(self->flag & 0x01))
            return 0;
        return movie_seek(self);
    }

    // 0x00414B50
    int __stdcall movie_update_window(MarniMovie* self)
    {
        uint32_t flag = self->flag;
        if ((flag & 1) == 0)
            return 0;

        auto pMC = reinterpret_cast<IMediaControl*>(self->pMediaControl);
        if (pMC == nullptr)
            return 0;

        HRESULT hr = pMC->Run();
        if (hr == S_FALSE)
        {
            // Graph is still transitioning to running state; wait briefly.
            OAFilterState state;
            hr = pMC->GetState(INFINITE, &state);
        }

        // Update position and duration.
        auto pMP = reinterpret_cast<IMediaPosition*>(self->pMediaPosition);
        if (pMP != nullptr)
        {
            REFTIME dur = 0.0;
            pMP->get_Duration(&dur);
            self->duration = dur;

            REFTIME pos = 0.0;
            pMP->get_CurrentPosition(&pos);
            self->pos = pos;
        }

        auto pVW = reinterpret_cast<IVideoWindow*>(self->pVideoWindow);
        if (pVW != nullptr)
        {
            HWND hWnd = nullptr;
            if (SUCCEEDED(pVW->get_Owner((OAHWND*)&hWnd)))
                UpdateWindow(hWnd);
        }

        self->flag |= 2;
        return 1;
    }

    // 0x00414C00
    int __stdcall movie_update(MarniMovie* self)
    {
        uint32_t flag = self->flag;
        if ((flag & 1) == 0 || (flag & 2) == 0)
            return 1;

        auto pMP = reinterpret_cast<IMediaPosition*>(self->pMediaPosition);
        if (pMP != nullptr)
        {
            REFTIME pos = 0.0;
            pMP->get_CurrentPosition(&pos);
            self->pos = pos;
        }

        if (self->duration > 0.0 && self->pos >= self->duration)
            movie_seek(self);

        return 1;
    }

    // 0x00414C80
    int __stdcall movie_seek(MarniMovie* self)
    {
        uint32_t flag = self->flag;
        if ((flag & 1) == 0)
            return 0;

        auto pMC = reinterpret_cast<IMediaControl*>(self->pMediaControl);
        if (pMC != nullptr)
            pMC->Stop();

        auto pMP = reinterpret_cast<IMediaPosition*>(self->pMediaPosition);
        if (pMP != nullptr)
            pMP->put_CurrentPosition(0.0);

        self->pos = 0.0;
        self->flag = (flag & ~2u) | 4u;
        return 1;
    }

    // 0x00414CF0
    int __stdcall
    movie_open(MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, LPDIRECTDRAW2 pDD2, LPDIRECTDRAWSURFACE pSurface)
    {
        (void)pDD2;
        (void)pSurface;

        movie_release(self);

        if (path == nullptr || pRect == nullptr)
        {
            out("invalid movie parameters. MarniMovie::Open", "");
            return 0;
        }

        WCHAR widePath[MAX_PATH] = {};
        if (MultiByteToWideChar(CP_ACP, 0, path, -1, widePath, MAX_PATH) == 0)
        {
            out("failed to convert the movie path. MarniMovie::Open", "");
            return 0;
        }

        IGraphBuilder* pGB = nullptr;
        HRESULT hr = CoCreateInstance(
            CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, reinterpret_cast<void**>(&pGB));
        if (FAILED(hr))
        {
            out("failed to create filter graph. MarniMovie::Open", "");
            return 0;
        }
        self->pGraphBuilder = pGB;

        // RenderFile builds the full filter graph for the MPEG-1 PS file,
        // connecting the splitter, video decoder, and renderers automatically.
        hr = pGB->RenderFile(widePath, nullptr);
        if (FAILED(hr))
        {
            out("failed to render the movie file. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }

        IMediaControl* pMC = nullptr;
        pGB->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&pMC));
        self->pMediaControl = pMC;

        IVideoWindow* pVW = nullptr;
        pGB->QueryInterface(IID_IVideoWindow, reinterpret_cast<void**>(&pVW));
        self->pVideoWindow = pVW;

        IMediaPosition* pMP = nullptr;
        pGB->QueryInterface(IID_IMediaPosition, reinterpret_cast<void**>(&pMP));
        self->pMediaPosition = pMP;

        if (pVW != nullptr)
        {
            pVW->put_Owner((OAHWND)hWnd);
            pVW->put_WindowStyle(WS_CHILD | WS_CLIPSIBLINGS);
            pVW->SetWindowPosition(pRect->left, pRect->top, pRect->right, pRect->bottom);
            pVW->put_AutoShow(OATRUE);
            pVW->put_Visible(OATRUE);
        }

        self->flag |= 1;
        return 1;
    }

    // 0x00414F50
    MarniMovie* __stdcall movie_ctor(MarniMovie* self, int mode)
    {
        memset(self, 0, sizeof(MarniMovie));
        self->flag = (mode != 0) ? 8 : 0;
        CoInitialize(nullptr);
        return self;
    }

    // 0x00414FC0
    void __stdcall movie_dtor(MarniMovie* self)
    {
        movie_release(self);
        CoUninitialize();
    }

    // 0x00414FD0
    void __stdcall movie_release(MarniMovie* self)
    {
        auto pMC = reinterpret_cast<IMediaControl*>(self->pMediaControl);
        if (pMC != nullptr)
        {
            pMC->Stop();
            pMC->Release();
            self->pMediaControl = nullptr;
        }

        auto pVW = reinterpret_cast<IVideoWindow*>(self->pVideoWindow);
        if (pVW != nullptr)
        {
            pVW->put_Visible(OAFALSE);
            pVW->put_Owner(0);
            pVW->Release();
            self->pVideoWindow = nullptr;
        }

        auto pMP = reinterpret_cast<IMediaPosition*>(self->pMediaPosition);
        if (pMP != nullptr)
        {
            pMP->Release();
            self->pMediaPosition = nullptr;
        }

        auto pGB = reinterpret_cast<IGraphBuilder*>(self->pGraphBuilder);
        if (pGB != nullptr)
        {
            pGB->Release();
            self->pGraphBuilder = nullptr;
        }

        self->field_6C = nullptr;
        self->field_78 = nullptr;
        self->field_88 = nullptr;
        self->field_8C = nullptr;
        self->field_90 = nullptr;
        self->pMediaStream = nullptr;

        self->flag = self->flag & 8;
        self->pos = 0.0;
        self->duration = 0.0;
    }
}
