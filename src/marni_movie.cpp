#include "marni_movie.h"
#include "interop.hpp"
#include "marni.h"
#include "openre.h"
#include "re2.h"

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

        if ((flag & 8) != 0)
        {
            // Streaming mode: IAMMultiMediaStream::SetState(STREAMSTATE_RUN = 1)
            // SetState is at vtable offset 0x1C (index 7)
            auto pStream = self->pMediaStream;
            auto vtbl = *(uint32_t**)pStream;
            auto pfnSetState = (HRESULT(__stdcall*)(void*, uint32_t))vtbl[0x1C / 4];
            pfnSetState(pStream, 1);
        }
        else
        {
            // DirectShow mode
            auto pMC = self->pMediaControl;
            auto vtblMC = *(uint32_t**)pMC;

            // IMediaControl::Run at vtable offset 0x1C (index 7)
            auto pfnRun = (HRESULT(__stdcall*)(void*))vtblMC[0x1C / 4];
            if (pfnRun(pMC) == 1) // S_FALSE = 1 (graph is transitioning)
            {
                // IMediaControl::GetState at vtable offset 0x28 (index 10)
                auto pfnGetState = (HRESULT(__stdcall*)(void*, int32_t, int32_t*))vtblMC[0x28 / 4];
                int32_t state;
                do
                {
                    pfnGetState(pMC, -1, &state);
                } while (state != 2 && pfnRun(pMC) == 1); // State_Running = 2
            }

            // IMediaPosition::get_Duration at vtable offset 0x1C (index 7)
            auto pMP = self->pMediaPosition;
            auto vtblMP = *(uint32_t**)pMP;
            auto pfngetDuration = (HRESULT(__stdcall*)(void*, double*))vtblMP[0x1C / 4];
            pfngetDuration(pMP, &self->duration);

            // IVideoWindow::get_Owner at vtable offset 0x78 (index 30)
            auto pVW = self->pVideoWindow;
            auto vtblVW = *(uint32_t**)pVW;
            auto pfngetOwner = (HRESULT(__stdcall*)(void*, HWND*))vtblVW[0x78 / 4];
            HWND hWnd;
            if (pfngetOwner(pVW, &hWnd) >= 0) // S_OK or success
                UpdateWindow(hWnd);
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

        if ((flag & 8) != 0)
        {
            // Streaming mode: present the next sample via
            // IDirectDrawMediaStreamSample::Update (vtable offset 0x18, index 6).
            auto pSample = self->field_90;
            auto vtbl = *(uint32_t**)pSample;
            auto pfnUpdate = (HRESULT(__stdcall*)(void*, uint32_t, void*, void*, void*))vtbl[0x18 / 4];
            pfnUpdate(pSample, 0, nullptr, nullptr, nullptr);
            return 1;
        }

        // DirectShow mode: track the current position and loop at the end.
        auto pMP = self->pMediaPosition;
        auto vtblMP = *(uint32_t**)pMP;

        // IMediaPosition::get_CurrentPosition at vtable offset 0x24 (index 9)
        auto pfnGetPos = (HRESULT(__stdcall*)(void*, double*))vtblMP[0x24 / 4];
        if (pfnGetPos(pMP, &self->pos))
            out("error reading the position. MarniMovie::Update", "");

        if (self->duration <= self->pos)
            movie_seek(self);

        return 1;
    }

    // 0x00414C80
    int __stdcall movie_seek(MarniMovie* self)
    {
        // Restarts the movie at position 0.
        uint32_t flag = self->flag;
        if ((flag & 1) == 0)
            return 0;

        if ((flag & 8) != 0)
        {
            // Streaming mode
            auto pStream = self->pMediaStream;
            auto vtbl = *(uint32_t**)pStream;

            // IAMMultiMediaStream::SetState(STREAMSTATE_STOP) at vtable offset 0x1C
            auto pfnSetState = (HRESULT(__stdcall*)(void*, uint32_t))vtbl[0x1C / 4];
            pfnSetState(pStream, 0); // STREAMSTATE_STOP = 0

            // IAMMultiMediaStream::Seek(0) at vtable offset 0x28
            auto pfnSeek = (HRESULT(__stdcall*)(void*, int64_t, uint32_t))vtbl[0x28 / 4];
            pfnSeek(pStream, 0, 0);
        }
        else
        {
            // DirectShow mode
            auto pMC = self->pMediaControl;
            auto vtblMC = *(uint32_t**)pMC;

            // IMediaControl::Stop at vtable offset 0x24
            auto pfnStop = (HRESULT(__stdcall*)(void*))vtblMC[0x24 / 4];
            pfnStop(pMC);

            auto pMP = self->pMediaPosition;
            auto vtblMP = *(uint32_t**)pMP;

            // IMediaPosition::put_CurrentPosition(0) at vtable offset 0x20
            auto pfnPutPos = (HRESULT(__stdcall*)(void*, double))vtblMP[0x20 / 4];
            pfnPutPos(pMP, 0.0);
        }

        self->flag = (flag & ~2u) | 4u;
        return 1;
    }

    // DirectShow CLSID/IID GUIDs used by movie_open (from binary rdata)
    // CLSID_FilterGraph {E436EBB3-524F-11CE-9F53-0020AF0BA770}
    static const GUID CLSID_FilterGraph_Movie
        = { 0xE436EBB3, 0x524F, 0x11CE, { 0x9F, 0x53, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70 } };
    // IID_IGraphBuilder {56A868A9-0AD4-11CE-B03A-0020AF0BA770}
    static const GUID IID_IGraphBuilder_Movie
        = { 0x56A868A9, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70 } };
    // IID_IMediaControl {56A868B1-0AD4-11CE-B03A-0020AF0BA770}
    static const GUID IID_IMediaControl_Movie
        = { 0x56A868B1, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70 } };
    // IID_IVideoWindow {56A868B4-0AD4-11CE-B03A-0020AF0BA770}
    static const GUID IID_IVideoWindow_Movie
        = { 0x56A868B4, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70 } };
    // IID_IMediaPosition {56A868B2-0AD4-11CE-B03A-0020AF0BA770}
    static const GUID IID_IMediaPosition_Movie
        = { 0x56A868B2, 0x0AD4, 0x11CE, { 0xB0, 0x3A, 0x00, 0x20, 0xAF, 0x0B, 0xA7, 0x70 } };
    // MSPID_PrimaryVideo {A35FF56A-9FDA-11D0-8FDF-00C04FD9189D}
    static const GUID MSPID_PrimaryVideo_Movie
        = { 0xA35FF56A, 0x9FDA, 0x11D0, { 0x8F, 0xDF, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0x9D } };
    // MSPID_PrimaryAudio {A35FF56B-9FDA-11D0-8FDF-00C04FD9189D}
    static const GUID MSPID_PrimaryAudio_Movie
        = { 0xA35FF56B, 0x9FDA, 0x11D0, { 0x8F, 0xDF, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0x9D } };
    // CLSID_AMMultiMediaStream {49C47CE5-9BA4-11D0-8212-00C04FC32C45}
    static const GUID CLSID_AMMultiMediaStream_Movie
        = { 0x49C47CE5, 0x9BA4, 0x11D0, { 0x82, 0x12, 0x00, 0xC0, 0x4F, 0xC3, 0x2C, 0x45 } };
    // IID_IAMMultiMediaStream {BEBE595C-9A6F-11D0-8FDE-00C04FD9189D}
    static const GUID IID_IAMMultiMediaStream_Movie
        = { 0xBEBE595C, 0x9A6F, 0x11D0, { 0x8F, 0xDE, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0x9D } };
    // IID_IDirectDrawMediaStream {F4104FCE-9A70-11D0-8FDE-00C04FD9189D}
    static const GUID IID_IDirectDrawMediaStream_Movie
        = { 0xF4104FCE, 0x9A70, 0x11D0, { 0x8F, 0xDE, 0x00, 0xC0, 0x4F, 0xD9, 0x18, 0x9D } };

    // 0x00414CF0
    int __stdcall
    movie_open(MarniMovie* self, LPCSTR path, HWND hWnd, LPRECT pRect, LPDIRECTDRAW2 pDD2, LPDIRECTDRAWSURFACE pSurface)
    {
        movie_release(self);

        WCHAR widePath[260];
        MultiByteToWideChar(0, 0, path, -1, widePath, 260);

        void* pGraphBuilder = nullptr;
        auto hr = CoCreateInstance(CLSID_FilterGraph_Movie, nullptr, 1u, IID_IGraphBuilder_Movie, &pGraphBuilder);
        if (FAILED(hr))
        {
            out("failed to generate Filter Graph. MarniMovie::Open", "");
            return 0;
        }

        self->pGraphBuilder = pGraphBuilder;

        if ((self->flag & 8) != 0)
        {
            // Streaming mode
            void* pAMStream = nullptr;
            CoCreateInstance(CLSID_AMMultiMediaStream_Movie, nullptr, 1u, IID_IAMMultiMediaStream_Movie, &pAMStream);
            if (!pAMStream)
            {
                out("failed to generate Filter Graph. MarniMovie::Open", "");
                return 0;
            }

            auto vtblAM = *(uint32_t**)pAMStream;

            // IAMMultiMediaStream::Initialize at vtable offset 0x30 (index 12)
            auto pfnInit = (HRESULT(__stdcall*)(void*, uint32_t, uint32_t, void*))vtblAM[0x30 / 4];
            pfnInit(pAMStream, 0, 0, nullptr); // STREAMTYPE_READ = 0

            // IAMMultiMediaStream::AddMediaStream at vtable offset 0x3C (index 15)
            auto pfnAddStream = (HRESULT(__stdcall*)(void*, IUnknown*, const GUID*, uint32_t, void*))vtblAM[0x3C / 4];
            pfnAddStream(pAMStream, (IUnknown*)pDD2, &MSPID_PrimaryVideo_Movie, 0, nullptr);
            pfnAddStream(pAMStream, nullptr, &MSPID_PrimaryAudio_Movie, 1, nullptr); // AMMSF_ADDDEFAULTRENDERER = 1

            WCHAR widePath2[260];
            MultiByteToWideChar(0, 0, path, -1, widePath2, 260);

            // IAMMultiMediaStream::OpenFile at vtable offset 0x40 (index 16)
            auto pfnOpenFile = (HRESULT(__stdcall*)(void*, const WCHAR*, uint32_t))vtblAM[0x40 / 4];
            if (FAILED(pfnOpenFile(pAMStream, widePath2, 0)))
            {
                out("failed to open the movie file. MarniMovie2::Open", "");
                return 0;
            }

            self->pMediaStream = pAMStream;

            // IAMMultiMediaStream::AddRef at vtable offset 0x04
            auto pfnAddRef = (ULONG(__stdcall*)(void*))vtblAM[0x04 / 4];
            pfnAddRef(pAMStream);

            // IAMMultiMediaStream::Release at vtable offset 0x08
            auto pfnRelease = (ULONG(__stdcall*)(void*))vtblAM[0x08 / 4];
            pfnRelease(pAMStream);

            // IAMMultiMediaStream::GetMediaStream at vtable offset 0x10 (index 4)
            auto pfnGetStream = (HRESULT(__stdcall*)(void*, const GUID*, void**))vtblAM[0x10 / 4];
            void* pVideoStream = nullptr;
            pfnGetStream(pAMStream, &MSPID_PrimaryVideo_Movie, &pVideoStream);
            self->field_88 = pVideoStream;

            // IMediaStream::QueryInterface at vtable offset 0x00
            auto vtblVS = *(uint32_t**)pVideoStream;
            auto pfnQI = (HRESULT(__stdcall*)(void*, const GUID*, void**))vtblVS[0x00 / 4];
            void* pDDStream = nullptr;
            pfnQI(pVideoStream, &IID_IDirectDrawMediaStream_Movie, &pDDStream);
            self->field_8C = pDDStream;

            // Set up legacy DDSURFACEDESC (108 bytes) at start of MarniMovie struct (offset 0)
            *(uint32_t*)((uint8_t*)self + 0) = 108; // dwSize = 108

            // IDirectDrawMediaStream::GetFormat at vtable offset 0x24 (index 9)
            auto vtblDD = *(uint32_t**)pDDStream;
            auto pfnGetFormat = (HRESULT(__stdcall*)(void*, void*, void*, void*, uint32_t*))vtblDD[0x24 / 4];
            pfnGetFormat(pDDStream, self, nullptr, nullptr, nullptr);

            uint32_t dwWidth = *(uint32_t*)((uint8_t*)self + 12);
            uint32_t dwHeight = *(uint32_t*)((uint8_t*)self + 8);

            RECT rect;
            rect.left = 0;
            rect.top = 0;
            rect.right = dwWidth;
            rect.bottom = dwHeight;

            // IDirectDrawMediaStream::CreateSample at vtable offset 0x34 (index 13)
            auto pfnCreateSample = (HRESULT(__stdcall*)(void*, void*, RECT*, uint32_t, void**))vtblDD[0x34 / 4];
            void* pSample = nullptr;
            if (FAILED(pfnCreateSample(pDDStream, pSurface, &rect, 0, &pSample)))
            {
                out("failed to generate movie object. MarniMovie2::Start", "");
                return 0;
            }
            self->field_90 = pSample;
        }
        else
        {
            // DirectShow mode
            auto vtblGB = *(uint32_t**)pGraphBuilder;
            auto pfnQI = (HRESULT(__stdcall*)(void*, const GUID*, void**))vtblGB[0x00 / 4];

            void* pMC = nullptr;
            pfnQI(pGraphBuilder, &IID_IMediaControl_Movie, &pMC);
            self->pMediaControl = pMC;

            void* pVW = nullptr;
            pfnQI(pGraphBuilder, &IID_IVideoWindow_Movie, &pVW);
            self->pVideoWindow = pVW;

            void* pMP = nullptr;
            pfnQI(pGraphBuilder, &IID_IMediaPosition_Movie, &pMP);
            self->pMediaPosition = pMP;

            // IGraphBuilder::RenderFile at vtable offset 0x34 (index 13)
            auto pfnRender = (HRESULT(__stdcall*)(void*, const WCHAR*, void*))vtblGB[0x34 / 4];
            pfnRender(pGraphBuilder, widePath, nullptr);

            // IVideoWindow methods
            auto vtblVW = *(uint32_t**)pVW;

            // put_Owner at vtable offset 0x74 (index 29)
            auto pfnPutOwner = (HRESULT(__stdcall*)(void*, HWND))vtblVW[0x74 / 4];
            pfnPutOwner(pVW, hWnd);

            // put_WindowStyle at vtable offset 0x24 (index 9)
            auto pfnPutStyle = (HRESULT(__stdcall*)(void*, int32_t))vtblVW[0x24 / 4];
            pfnPutStyle(pVW, 0x46000000); // WS_CHILD | WS_CLIPSIBLINGS

            // SetWindowPosition at vtable offset 0x9C (index 39)
            auto pfnSetPos = (HRESULT(__stdcall*)(void*, int32_t, int32_t, int32_t, int32_t))vtblVW[0x9C / 4];
            pfnSetPos(pVW, pRect->left, pRect->top, pRect->right, pRect->bottom);

            // put_AutoShow at vtable offset 0x34 (index 13)
            auto pfnAutoShow = (HRESULT(__stdcall*)(void*, int32_t))vtblVW[0x34 / 4];
            pfnAutoShow(pVW, -1); // OATRUE

            // put_Visible at vtable offset 0x4C (index 19)
            auto pfnVisible = (HRESULT(__stdcall*)(void*, int32_t))vtblVW[0x4C / 4];
            pfnVisible(pVW, -1); // OATRUE
        }

        self->flag |= 1;
        return 1;
    }

    // 0x00414F50
    MarniMovie* __stdcall movie_ctor(MarniMovie* self, int mode)
    {
        self->flag = (mode != 0) ? 8 : 0;
        self->pos = 0.0;
        self->field_6C = nullptr;
        self->pGraphBuilder = nullptr;
        self->pMediaControl = nullptr;
        self->field_78 = nullptr;
        self->pVideoWindow = nullptr;
        self->pMediaPosition = nullptr;
        self->pMediaStream = nullptr;
        self->field_90 = nullptr;
        self->field_8C = nullptr;
        self->field_88 = nullptr;
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
        interop::thiscall<int, MarniMovie*>(0x00414FD0, self);
    }
}
