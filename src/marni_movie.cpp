#include "marni_movie.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "re2.h"

#include <cstring>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfmediaengine.h>
#include <mfobjects.h>
#include <oleauto.h>

// Movie playback is implemented with the Windows Media Foundation media engine
// (IMFMediaEngine), which renders the video into its own child window swapchain
// and plays the audio track, replacing the legacy DirectShow / DirectDraw
// streaming implementation. The MarniMovie COM pointer fields are reused to
// hold the media engine and its helper objects; the game never reads them.

namespace openre::marni
{
    namespace
    {
        // Window class of the child window the media engine presents into.
        constexpr char kMovieWindowClass[] = "OpenREMovieWindow";

        // Playback layout of the movie window, stored in the MarniMovie surf_pad
        // (the game does not use those bytes).
        struct MovieWindowLayout
        {
            LONG left;
            LONG top;
            LONG width;
            LONG height;
        };

        // Minimal IMFMediaEngineNotify: the engine requires a callback at
        // creation. Playback itself is driven by polling from the game loop.
        class MovieEngineNotify final : public IMFMediaEngineNotify
        {
            LONG ref_count_ = 1;

        public:
            STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override
            {
                if (riid == IID_IUnknown || riid == IID_IMFMediaEngineNotify)
                {
                    *ppvObject = static_cast<IMFMediaEngineNotify*>(this);
                    AddRef();
                    return S_OK;
                }
                *ppvObject = nullptr;
                return E_NOINTERFACE;
            }

            STDMETHODIMP_(ULONG) AddRef() override
            {
                return (ULONG)InterlockedIncrement(&ref_count_);
            }

            STDMETHODIMP_(ULONG) Release() override
            {
                LONG refs = InterlockedDecrement(&ref_count_);
                if (refs == 0)
                    delete this;
                return (ULONG)refs;
            }

            STDMETHODIMP EventNotify(DWORD event, DWORD_PTR, DWORD) override
            {
                if (event == MF_MEDIA_ENGINE_EVENT_ERROR)
                    out("media engine reported an error while playing the movie", "MarniMovie::EventNotify");
                return S_OK;
            }
        };

        LRESULT CALLBACK movie_window_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            switch (message)
            {
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                BeginPaint(hWnd, &ps);
                FillRect(ps.hdc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
                EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_ERASEBKGND: return 1; // background is painted in WM_PAINT
            default: return DefWindowProcA(hWnd, message, wParam, lParam);
            }
        }

        bool register_movie_window_class()
        {
            static bool registered = false;
            if (registered)
                return true;

            WNDCLASSEXA wc = {};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = &movie_window_proc;
            wc.hInstance = GetModuleHandleA(nullptr);
            wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
            wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
            wc.lpszClassName = kMovieWindowClass;
            registered = RegisterClassExA(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
            return registered;
        }
    }

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

        auto pEngine = (IMFMediaEngine*)self->pGraphBuilder;
        if (pEngine == nullptr)
            return 0;

        // Start playback once the engine is ready. IsPaused() also returns
        // TRUE before playback has started; Play() is a no-op while playing.
        if (pEngine->IsPaused())
            pEngine->Play();

        // Refresh the duration; GetDuration() returns NaN until metadata loads.
        self->duration = pEngine->GetDuration();

        // Re-assert the child window layout if it moved.
        HWND hMovieWnd = (HWND)self->field_6C;
        HWND hParent = (HWND)self->field_8C;
        if (hMovieWnd != nullptr && hParent != nullptr)
        {
            MovieWindowLayout layout;
            memcpy(&layout, self->surf_pad, sizeof(layout));

            RECT current = {};
            if (GetWindowRect(hMovieWnd, &current))
            {
                POINT origin = { 0, 0 };
                ClientToScreen(hParent, &origin);
                if (current.left != origin.x + layout.left || current.top != origin.y + layout.top
                    || current.right - current.left != layout.width || current.bottom - current.top != layout.height)
                {
                    MoveWindow(hMovieWnd, layout.left, layout.top, layout.width, layout.height, TRUE);
                }
            }
        }

        self->flag |= 2;
        return 1;
    }

    // 0x00414C00
    int __stdcall movie_update(MarniMovie* self)
    {
        // Advances playback. Runs only while the movie is open (flag 1) and has
        // been started (flag 2).
        uint32_t flag = self->flag;
        if ((flag & 1) == 0 || (flag & 2) == 0)
            return 1;

        auto pEngine = (IMFMediaEngine*)self->pGraphBuilder;
        if (pEngine == nullptr)
            return 1;

        // Track the current position and loop at the end.
        self->pos = pEngine->GetCurrentTime();
        if (self->duration <= self->pos)
            movie_seek(self);

        return 1;
    }

    // 0x00414C80
    int __stdcall movie_seek(MarniMovie* self)
    {
        // Pause and rewind the movie to position 0, matching the original
        // DirectShow movie_seek (IMediaControl::Stop + put_CurrentPosition(0)).
        // Playback is restarted by movie_update_window on the next frame;
        // calling Play() here would keep the movie running (with audio) after
        // kill_movie, which expects the movie to stop.
        uint32_t flag = self->flag;
        if ((flag & 1) == 0)
            return 0;

        auto pEngine = (IMFMediaEngine*)self->pGraphBuilder;
        if (pEngine != nullptr)
        {
            pEngine->Pause();
            pEngine->SetCurrentTime(0.0);
        }

        self->flag = (flag & ~2u) | 4u;
        return 1;
    }

    // 0x00414CF0
    int __stdcall
    movie_open(MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, LPDIRECTDRAW2 pDD2, LPDIRECTDRAWSURFACE pSurface)
    {
        // The DirectDraw parameters are kept only for ABI compatibility with
        // the original caller in marni.cpp; the media engine does not use them.
        (void)pDD2;
        (void)pSurface;

        movie_release(self);

        if (path == nullptr || pRect == nullptr)
        {
            out("invalid movie parameters. MarniMovie::Open", "");
            return 0;
        }

        WCHAR widePath[260];
        if (MultiByteToWideChar(CP_ACP, 0, path, -1, widePath, 260) == 0)
        {
            out("failed to convert the movie path. MarniMovie::Open", "");
            return 0;
        }

        // --- D3D11 device + DXGI device manager for the media engine ---
        UINT resetToken = 0;
        IMFDXGIDeviceManager* pDXGIManager = nullptr;
        HRESULT hr = MFCreateDXGIDeviceManager(&resetToken, &pDXGIManager);
        if (FAILED(hr))
        {
            out("failed to create the DXGI device manager. MarniMovie::Open", "");
            return 0;
        }
        self->field_78 = pDXGIManager;

        // BGRA support is required for the engine's default video output format
        // (DXGI_FORMAT_B8G8R8A8_UNORM) and the swapchain it creates.
        const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        ID3D11Device* pD3DDevice = nullptr;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
            featureLevels,
            1,
            D3D11_SDK_VERSION,
            &pD3DDevice,
            nullptr,
            nullptr);
        if (FAILED(hr))
        {
            out("failed to create the D3D11 device. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }
        self->pVideoWindow = pD3DDevice;

        hr = pDXGIManager->ResetDevice(pD3DDevice, resetToken);
        if (FAILED(hr))
        {
            out("failed to reset the DXGI device manager. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }

        // --- child window the engine presents into ---
        if (!register_movie_window_class())
        {
            out("failed to register the movie window class. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }

        // The game passes (left, top, width, height) in pRect; the right/bottom
        // fields hold the width/height, matching the original DirectShow call.
        const MovieWindowLayout layout = { pRect->left, pRect->top, pRect->right, pRect->bottom };
        memcpy(self->surf_pad, &layout, sizeof(layout));

        HWND hMovieWnd = CreateWindowExA(
            0,
            kMovieWindowClass,
            "",
            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
            layout.left,
            layout.top,
            layout.width,
            layout.height,
            hWnd,
            nullptr,
            GetModuleHandleA(nullptr),
            nullptr);
        if (hMovieWnd == nullptr)
        {
            out("failed to create the movie window. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }
        self->field_6C = hMovieWnd;
        self->field_8C = hWnd;

        // --- engine attributes ---
        IMFAttributes* pAttributes = nullptr;
        hr = MFCreateAttributes(&pAttributes, 3);
        if (FAILED(hr))
        {
            out("failed to create the media engine attributes. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }
        self->field_88 = pAttributes;

        pAttributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, pDXGIManager);
        pAttributes->SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND, (UINT64)(UINT_PTR)hMovieWnd);

        MovieEngineNotify* pNotify = new MovieEngineNotify();
        self->pMediaPosition = pNotify;
        pAttributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, pNotify);

        // --- create the engine ---
        IMFMediaEngineClassFactory* pFactory = nullptr;
        hr = CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));
        if (FAILED(hr))
        {
            out("failed to create the media engine class factory. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }

        IMFMediaEngine* pEngine = nullptr;
        hr = pFactory->CreateInstance(0, pAttributes, &pEngine);
        pFactory->Release();
        if (FAILED(hr))
        {
            out("failed to create the media engine. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }
        self->pGraphBuilder = pEngine;

        IMFMediaEngineEx* pEngineEx = nullptr;
        hr = pEngine->QueryInterface(IID_IMFMediaEngineEx, (void**)&pEngineEx);
        if (FAILED(hr))
        {
            out("failed to query the media engine ex interface. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }
        self->pMediaControl = pEngineEx;

        // --- load the movie file (kept alive for the engine's lifetime) ---
        IMFByteStream* pByteStream = nullptr;
        hr = MFCreateFile(MF_ACCESSMODE_READ, MF_OPENMODE_FAIL_IF_NOT_EXIST, MF_FILEFLAGS_NONE, widePath, &pByteStream);
        if (FAILED(hr))
        {
            out("failed to open the movie file. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }
        self->pMediaStream = pByteStream;

        BSTR url = SysAllocString(widePath);
        hr = pEngineEx->SetSourceFromByteStream(pByteStream, url);
        if (url != nullptr)
            SysFreeString(url);
        if (FAILED(hr))
        {
            out("failed to set the movie source. MarniMovie::Open", "");
            movie_release(self);
            return 0;
        }

        // --- wait (bounded) for the engine to load the source ---
        constexpr int kMaxWaitMs = 2000;
        constexpr int kPollMs = 10;
        for (int waited = 0; waited < kMaxWaitMs && pEngine->GetReadyState() < MF_MEDIA_ENGINE_READY_HAVE_CURRENT_DATA;
             waited += kPollMs)
        {
            Sleep(kPollMs);
        }
        if (pEngine->GetReadyState() < MF_MEDIA_ENGINE_READY_HAVE_CURRENT_DATA)
        {
            IMFMediaError* pError = nullptr;
            if (SUCCEEDED(pEngine->GetError(&pError)) && pError != nullptr)
                pError->Release();
            out("failed to load the movie file. MarniMovie::Open", "");
            movie_release(self);
            return 0;
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
        MFStartup(MF_VERSION);
        return self;
    }

    // 0x00414FC0
    void __stdcall movie_dtor(MarniMovie* self)
    {
        movie_release(self);
        MFShutdown();
        CoUninitialize();
    }

    // 0x00414FD0
    void __stdcall movie_release(MarniMovie* self)
    {
        movie_seek(self);

        // Stop and shut down the media engine.
        auto pEngine = (IMFMediaEngine*)self->pGraphBuilder;
        if (pEngine != nullptr)
        {
            pEngine->Pause();
            pEngine->Shutdown();
        }

        // Destroy the child window the engine presented into.
        if (self->field_6C != nullptr)
        {
            DestroyWindow((HWND)self->field_6C);
            self->field_6C = nullptr;
        }

        // Release the COM objects, engine first (the others may be referenced
        // by it).
        if (self->pMediaControl != nullptr)
        {
            ((IMFMediaEngineEx*)self->pMediaControl)->Release();
            self->pMediaControl = nullptr;
        }
        if (pEngine != nullptr)
        {
            pEngine->Release();
            self->pGraphBuilder = nullptr;
        }
        if (self->pMediaPosition != nullptr)
        {
            ((IMFMediaEngineNotify*)self->pMediaPosition)->Release();
            self->pMediaPosition = nullptr;
        }
        if (self->pMediaStream != nullptr)
        {
            ((IMFByteStream*)self->pMediaStream)->Release();
            self->pMediaStream = nullptr;
        }
        if (self->field_88 != nullptr)
        {
            ((IMFAttributes*)self->field_88)->Release();
            self->field_88 = nullptr;
        }
        if (self->field_78 != nullptr)
        {
            ((IMFDXGIDeviceManager*)self->field_78)->Release();
            self->field_78 = nullptr;
        }
        if (self->pVideoWindow != nullptr)
        {
            ((ID3D11Device*)self->pVideoWindow)->Release();
            self->pVideoWindow = nullptr;
        }

        self->field_8C = nullptr;
        self->field_90 = nullptr;

        // Keep only the legacy streaming-mode bit; the movie is no longer open.
        self->flag = self->flag & 8;
        self->pos = 0.0;
        self->duration = 0.0;
    }
}
