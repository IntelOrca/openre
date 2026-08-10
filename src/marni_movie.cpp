#include "marni_movie.h"
#include "interop.hpp"
#include "logger.h"
#include "marni.h"
#include "openre.h"
#include "re2.h"
#include "system_gpu.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <dshow.h>
#include <dvdmedia.h>
#include <windows.h>

// The Windows 10 SDK no longer ships qedit.h (DirectShow Editing Services),
// which used to declare the sample-grabber filter and its interfaces. Declare
// them here with the historical qedit.h GUIDs and vtable layout (strmif.h
// already provides IMediaSample, so no forward declaration is needed).
struct ISampleGrabberCB;

struct ISampleGrabber : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL oneShot) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* pType) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL bufferThem) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* pBufferSize, long* pBuffer) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** ppSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB* pCallback, long whichMethodToCallback) = 0;
};

struct ISampleGrabberCB : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double sampleTime, IMediaSample* pSample) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double sampleTime, BYTE* pBuffer, long bufferLen) = 0;
};

// Movie playback is implemented with DirectShow (IGraphBuilder/RenderFile),
// which reproduces the original RE2 approach and correctly decodes the MPEG-1
// Program Stream .bin movie files.  Windows Media Foundation's source reader
// and media engine do not expose a usable decoder for the MPG1 video subtype
// on Windows 10 (the Microsoft MPEG Video Decoder MFT exists but is not
// reachable from IMFSourceReader::SetCurrentMediaType).
//
// Phase 7: cutscenes no longer play into a DirectShow child video window.
// After RenderFile builds the graph (the audio path is untouched), an
// ISampleGrabber is inserted between the video decoder and the video renderer,
// the video tail is re-routed through a Null Renderer, and the video renderer
// is removed from the graph - so the graph never creates a video window. The
// grabber's BufferCB callback (streaming thread) converts every decoded frame
// to top-down RGB24 into a shared buffer; the game loop (movie_update_window /
// movie_update) forwards each new frame to system::gpu, and the GPU backend
// composites it into the guest framebuffer during present(), after the scene
// pass and the GDI text overlay.
//
// ISampleGrabber was chosen over VMR-9 windowless (IVMRWindowlessControl9::
// GetCurrentImage or a custom allocator-presenter) because it is the least
// machinery that captures decoded frames from an existing RenderFile graph
// (no allocator/presenter COM plumbing, no per-frame DIB readback), and the
// Null Renderer keeps the audio path running so movies keep their sound. The
// decoder is asked for RGB24 first; if it cannot deliver RGB24 the grabber
// accepts the decoder's native format (YUY2/UYVY/NV12/I420/YV12/RGB32) and the
// callback converts on the CPU.
//
// MarniMovie field reuse:
//   pGraphBuilder  - IGraphBuilder*
//   pMediaControl  - IMediaControl*
//   pVideoWindow   - unused (the graph has no video renderer, so IVideoWindow
//                    is never queried; kept null for compatibility)
//   pMediaPosition - IMediaPosition*
//   field_6C       - unused (no child video window anymore)
//   flag / pos / duration  - same as before

namespace openre::marni
{
    namespace
    {
        // qedit.h GUIDs (declared manually; see the comment above).
        const GUID g_clsidSampleGrabber = { 0xC1F400A0, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
        const GUID g_clsidNullRenderer = { 0xC1F400A4, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
        const GUID g_iidSampleGrabber = { 0x6B652FFF, 0x11FE, 0x4FCE, { 0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F } };
        const GUID g_iidSampleGrabberCb = { 0x0579154A, 0x2B53, 0x4994, { 0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85 } };

        // Not declared by this SDK's uuids.h ('I420' fourcc).
        const GUID g_mediaSubtypeI420 = { 0x30323449, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

        // The SDK headers no longer ship the DirectShow base classes, which
        // used to provide FreeMediaType; replicate it here.
        void freeMediaType(AM_MEDIA_TYPE& mt)
        {
            if (mt.cbFormat != 0)
                CoTaskMemFree(mt.pbFormat);
            if (mt.pUnk != nullptr)
                mt.pUnk->Release();
            mt = {};
        }

        // The media type of the grabber's input connection, read once after
        // the graph is wired (before it runs) and used by the streaming-thread
        // callback to convert each sample to top-down RGB24.
        struct CaptureFormat
        {
            GUID subtype = GUID_NULL;
            int width = 0;
            int height = 0;
            int pitch = 0; // source row pitch in bytes (Y plane for planar formats)
            int plane = 0; // byte offset of the chroma planes for NV12/I420/YV12
            bool topDown = true;
        };

        CaptureFormat g_captureFormat;

        // Latest decoded frame (top-down RGB24) plus its size; written by the
        // streaming-thread BufferCB, consumed by the main-thread
        // movieForwardFrame.
        std::mutex g_captureMutex;
        std::vector<uint8_t> g_capturedFrame;
        int g_capturedWidth = 0;
        int g_capturedHeight = 0;
        bool g_capturedNew = false;

        uint8_t clampU8(int v)
        {
            return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
        }

        // YUV -> RGB (DirectShow BGR byte order) with the BT.601 coefficients
        // the MPEG-1 spec uses.
        void yuvToBgr(uint8_t y, uint8_t u, uint8_t v, uint8_t* bgr)
        {
            const int c = static_cast<int>(y) - 16;
            const int d = static_cast<int>(u) - 128;
            const int e = static_cast<int>(v) - 128;
            bgr[2] = clampU8((298 * c + 409 * e + 128) >> 8);           // R
            bgr[1] = clampU8((298 * c - 100 * d - 208 * e + 128) >> 8); // G
            bgr[0] = clampU8((298 * c + 516 * d + 128) >> 8);           // B
        }

        // ISampleGrabberCB sink: receives every decoded video frame on the
        // graph's streaming thread, converts it to top-down RGB24 and stores
        // it in g_capturedFrame for the game loop to forward to the GPU
        // backend. A single module-lifetime instance: the grabber holds a
        // reference while a movie is open, and the object itself outlives
        // every graph (Release never frees it).
        class MovieSampleSink final : public ISampleGrabberCB
        {
        public:
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override
            {
                if (riid == IID_IUnknown || riid == g_iidSampleGrabberCb)
                {
                    *ppvObj = static_cast<ISampleGrabberCB*>(this);
                    AddRef();
                    return S_OK;
                }
                *ppvObj = nullptr;
                return E_NOINTERFACE;
            }

            ULONG STDMETHODCALLTYPE AddRef() override
            {
                return ++m_refs;
            }

            ULONG STDMETHODCALLTYPE Release() override
            {
                // Module-lifetime instance; never actually freed.
                return --m_refs;
            }

            HRESULT STDMETHODCALLTYPE SampleCB(double /*sampleTime*/, IMediaSample* /*pSample*/) override
            {
                return S_OK;
            }

            HRESULT STDMETHODCALLTYPE BufferCB(double /*sampleTime*/, BYTE* pBuffer, long bufferLen) override
            {
                const CaptureFormat fmt = g_captureFormat;
                if (pBuffer == nullptr || fmt.width <= 0 || fmt.height <= 0 || fmt.pitch <= 0)
                    return S_OK;

                const int w = fmt.width;
                const int h = fmt.height;
                const int srcPitch = fmt.pitch;
                const size_t rowBytes = static_cast<size_t>(w) * 3;
                const bool planar
                    = fmt.subtype == MEDIASUBTYPE_NV12 || fmt.subtype == g_mediaSubtypeI420 || fmt.subtype == MEDIASUBTYPE_YV12;
                const bool yuv = planar || fmt.subtype == MEDIASUBTYPE_YUY2 || fmt.subtype == MEDIASUBTYPE_UYVY;
                const bool packed = fmt.subtype == MEDIASUBTYPE_RGB24 || fmt.subtype == MEDIASUBTYPE_RGB32
                    || fmt.subtype == MEDIASUBTYPE_ARGB32;
                if (!packed && !yuv)
                {
                    // A decoder output format we do not convert (not expected
                    // for MPEG-1 video); drop the frame.
                    static bool logged = false;
                    if (!logged)
                    {
                        logged = true;
                        char subtypeHex[16];
                        snprintf(subtypeHex, sizeof(subtypeHex), "%08X", fmt.subtype.Data1);
                        logging::logWarning("[marni:movie] unsupported grabber format (subtype={})", subtypeHex);
                    }
                    return S_OK;
                }

                size_t needBytes = static_cast<size_t>(srcPitch) * static_cast<size_t>(h);
                if (planar)
                    needBytes += needBytes / 2;
                if (static_cast<size_t>(bufferLen) < needBytes)
                    return S_OK;

                std::lock_guard lock(g_captureMutex);
                if (g_capturedFrame.size() != rowBytes * static_cast<size_t>(h))
                    g_capturedFrame.resize(rowBytes * static_cast<size_t>(h));
                g_capturedWidth = w;
                g_capturedHeight = h;

                for (int srcRow = 0; srcRow < h; srcRow++)
                {
                    const int dstRow = fmt.topDown ? srcRow : (h - 1 - srcRow);
                    const auto* src = pBuffer + static_cast<size_t>(srcRow) * srcPitch;
                    auto* dst = g_capturedFrame.data() + static_cast<size_t>(dstRow) * rowBytes;

                    if (fmt.subtype == MEDIASUBTYPE_YUY2 || fmt.subtype == MEDIASUBTYPE_UYVY)
                    {
                        const bool uyvy = fmt.subtype == MEDIASUBTYPE_UYVY;
                        int x = 0;
                        while (x < w)
                        {
                            const uint8_t y0 = uyvy ? src[1] : src[0];
                            const uint8_t u = uyvy ? src[0] : src[1];
                            const uint8_t y1 = uyvy ? src[3] : src[2];
                            const uint8_t v = uyvy ? src[2] : src[3];
                            yuvToBgr(y0, u, v, dst);
                            if (x + 1 < w)
                                yuvToBgr(y1, u, v, dst + 3);
                            src += 4;
                            dst += 3;
                            x += 2;
                        }
                    }
                    else if (fmt.subtype == MEDIASUBTYPE_NV12)
                    {
                        const auto* uv = pBuffer + fmt.plane + (static_cast<size_t>(srcRow) / 2) * srcPitch;
                        int x = 0;
                        while (x < w)
                        {
                            yuvToBgr(src[0], uv[0], uv[1], dst);
                            if (x + 1 < w)
                                yuvToBgr(src[1], uv[0], uv[1], dst + 3);
                            src += 2;
                            uv += 2;
                            dst += 3;
                            x += 2;
                        }
                    }
                    else if (fmt.subtype == g_mediaSubtypeI420 || fmt.subtype == MEDIASUBTYPE_YV12)
                    {
                        const bool yv12 = fmt.subtype == MEDIASUBTYPE_YV12;
                        const auto* up = pBuffer + fmt.plane + (static_cast<size_t>(srcRow) / 2) * (srcPitch / 2);
                        const auto* vp = up + (static_cast<size_t>(srcPitch) / 2) * ((static_cast<size_t>(h) + 1) / 2);
                        int x = 0;
                        while (x < w)
                        {
                            const uint8_t u = yv12 ? vp[x / 2] : up[x / 2];
                            const uint8_t v = yv12 ? up[x / 2] : vp[x / 2];
                            yuvToBgr(src[0], u, v, dst);
                            if (x + 1 < w)
                                yuvToBgr(src[1], u, v, dst + 3);
                            src += 2;
                            dst += 3;
                            x += 2;
                        }
                    }
                    else if (fmt.subtype == MEDIASUBTYPE_RGB32 || fmt.subtype == MEDIASUBTYPE_ARGB32)
                    {
                        for (int x = 0; x < w; x++)
                        {
                            dst[0] = src[0];
                            dst[1] = src[1];
                            dst[2] = src[2];
                            src += 4;
                            dst += 3;
                        }
                    }
                    else // MEDIASUBTYPE_RGB24
                    {
                        std::memcpy(dst, src, std::min(rowBytes, static_cast<size_t>(srcPitch)));
                    }
                }
                g_capturedNew = true;
                return S_OK;
            }

        private:
            std::atomic<ULONG> m_refs{ 1 };
        };

        MovieSampleSink g_movieSink;

        // Returns the first output pin that is connected to a peer with an
        // uncompressed video media type (the decoder output in the RenderFile
        // graph, whatever renderer it feeds) plus that peer (the renderer's
        // input pin). Caller releases both. Works for any default video
        // renderer (Video Renderer / VMR / EVR).
        bool findUncompressedVideoOutput(IGraphBuilder* pGB, IPin** outVideoOut, IPin** outRendererIn)
        {
            IEnumFilters* pEnumFilters = nullptr;
            if (FAILED(pGB->EnumFilters(&pEnumFilters)))
                return false;

            bool found = false;
            IBaseFilter* pFilter = nullptr;
            while (pEnumFilters->Next(1, &pFilter, nullptr) == S_OK)
            {
                IEnumPins* pEnumPins = nullptr;
                if (SUCCEEDED(pFilter->EnumPins(&pEnumPins)))
                {
                    IPin* pPin = nullptr;
                    while (pEnumPins->Next(1, &pPin, nullptr) == S_OK)
                    {
                        PIN_DIRECTION dir;
                        pPin->QueryDirection(&dir);
                        if (dir == PINDIR_OUTPUT)
                        {
                            IPin* pPeer = nullptr;
                            if (SUCCEEDED(pPin->ConnectedTo(&pPeer)))
                            {
                                AM_MEDIA_TYPE mt = {};
                                bool isVideo = false;
                                if (SUCCEEDED(pPin->ConnectionMediaType(&mt)))
                                {
                                    isVideo = mt.majortype == MEDIATYPE_Video
                                        && (mt.formattype == FORMAT_VideoInfo || mt.formattype == FORMAT_VideoInfo2);
                                    freeMediaType(mt);
                                }
                                if (isVideo)
                                {
                                    *outVideoOut = pPin;    // caller releases
                                    *outRendererIn = pPeer; // caller releases
                                    found = true;
                                    break;
                                }
                                pPeer->Release();
                            }
                        }
                        pPin->Release();
                        if (found)
                            break;
                    }
                    pEnumPins->Release();
                }
                pFilter->Release();
                if (found)
                    break;
            }
            pEnumFilters->Release();
            return found;
        }

        // Finds the pin of `pFilter` with the given direction; caller releases.
        IPin* getPin(IBaseFilter* pFilter, PIN_DIRECTION wanted)
        {
            IEnumPins* pEnumPins = nullptr;
            if (FAILED(pFilter->EnumPins(&pEnumPins)))
                return nullptr;
            IPin* result = nullptr;
            IPin* pPin = nullptr;
            while (pEnumPins->Next(1, &pPin, nullptr) == S_OK)
            {
                PIN_DIRECTION dir;
                pPin->QueryDirection(&dir);
                if (dir == wanted)
                {
                    result = pPin;
                    break;
                }
                pPin->Release();
            }
            pEnumPins->Release();
            return result;
        }

        int computeRowPitch(const GUID& subtype, int width, int height, LONG bitCount, DWORD sizeImage)
        {
            const bool planar = subtype == MEDIASUBTYPE_NV12 || subtype == g_mediaSubtypeI420 || subtype == MEDIASUBTYPE_YV12;
            const int bpp = planar ? 8 : static_cast<int>(bitCount);
            const int natural = width * (bpp / 8);
            if (sizeImage != 0 && height > 0)
            {
                if (planar)
                {
                    // The Y plane is 2/3 of the NV12/I420/YV12 buffer.
                    const size_t yBytes = static_cast<size_t>(sizeImage) * 2 / 3;
                    if (yBytes >= static_cast<size_t>(width) * static_cast<size_t>(height))
                        return static_cast<int>(yBytes / static_cast<size_t>(height));
                }
                else
                {
                    const size_t perRow = static_cast<size_t>(sizeImage) / static_cast<size_t>(height);
                    if (perRow >= static_cast<size_t>(natural))
                        return static_cast<int>(perRow);
                }
            }
            if (bitCount == 24)
                return (width * 3 + 3) & ~3; // DIB rows are DWORD-aligned
            return natural;
        }

        // Reads the media type of the grabber's input connection (decoder
        // output) into g_captureFormat, so the sample callback knows how to
        // convert the frames. Called once after wiring, before the graph runs.
        void readCaptureFormat(IPin* pGrabberIn)
        {
            CaptureFormat fmt = {};
            AM_MEDIA_TYPE mt = {};
            if (FAILED(pGrabberIn->ConnectionMediaType(&mt)))
            {
                g_captureFormat = fmt;
                return;
            }
            fmt.subtype = mt.subtype;
            LONG bitCount = 0;
            DWORD sizeImage = 0;
            if (mt.formattype == FORMAT_VideoInfo && mt.pbFormat != nullptr && mt.cbFormat >= sizeof(VIDEOINFOHEADER))
            {
                const auto* vih = reinterpret_cast<const VIDEOINFOHEADER*>(mt.pbFormat);
                const LONG bw = vih->bmiHeader.biWidth;
                const LONG bh = vih->bmiHeader.biHeight;
                fmt.width = bw > 0 ? static_cast<int>(bw) : static_cast<int>(-bw);
                fmt.height = bh > 0 ? static_cast<int>(bh) : static_cast<int>(-bh);
                fmt.topDown = bh < 0;
                bitCount = vih->bmiHeader.biBitCount;
                sizeImage = vih->bmiHeader.biSizeImage;
            }
            else if (mt.formattype == FORMAT_VideoInfo2 && mt.pbFormat != nullptr && mt.cbFormat >= sizeof(VIDEOINFOHEADER2))
            {
                const auto* vih2 = reinterpret_cast<const VIDEOINFOHEADER2*>(mt.pbFormat);
                const LONG bw = vih2->bmiHeader.biWidth;
                const LONG bh = vih2->bmiHeader.biHeight;
                fmt.width = bw > 0 ? static_cast<int>(bw) : static_cast<int>(-bw);
                fmt.height = bh > 0 ? static_cast<int>(bh) : static_cast<int>(-bh);
                fmt.topDown = bh < 0;
                bitCount = vih2->bmiHeader.biBitCount;
                sizeImage = vih2->bmiHeader.biSizeImage;
            }
            freeMediaType(mt);
            if (fmt.width > 0 && fmt.height > 0)
            {
                fmt.pitch = computeRowPitch(fmt.subtype, fmt.width, fmt.height, bitCount, sizeImage);
                if (fmt.subtype == MEDIASUBTYPE_NV12 || fmt.subtype == g_mediaSubtypeI420 || fmt.subtype == MEDIASUBTYPE_YV12)
                {
                    fmt.plane = fmt.pitch * fmt.height;
                }
            }
            g_captureFormat = fmt;
            char subtypeHex[16];
            snprintf(subtypeHex, sizeof(subtypeHex), "%08X", fmt.subtype.Data1);
            logging::logInfo(
                "[marni:movie] grabber connected (subtype={} {}x{} pitch={} plane={} topDown={})",
                subtypeHex,
                fmt.width,
                fmt.height,
                fmt.pitch,
                fmt.plane,
                fmt.topDown ? 1 : 0);
        }

        // Inserts an ISampleGrabber between the video decoder and the video
        // renderer RenderFile created, routes the video tail through a Null
        // Renderer and removes the video renderer so the graph never opens a
        // video window (Phase 7; frames are captured instead). Returns the
        // grabber's input pin in `outGrabberIn` (caller releases) and the
        // grabber interface (caller releases), or nullptr on failure.
        ISampleGrabber* insertSampleGrabber(IGraphBuilder* pGB, IPin** outGrabberIn)
        {
            IBaseFilter* pGrabberFilter = nullptr;
            HRESULT hr = CoCreateInstance(
                g_clsidSampleGrabber,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_IBaseFilter,
                reinterpret_cast<void**>(&pGrabberFilter));
            if (FAILED(hr))
                return nullptr;

            ISampleGrabber* pGrabber = nullptr;
            if (FAILED(pGrabberFilter->QueryInterface(g_iidSampleGrabber, reinterpret_cast<void**>(&pGrabber))))
            {
                pGrabberFilter->Release();
                return nullptr;
            }

            IBaseFilter* pNullRenderer = nullptr;
            hr = CoCreateInstance(
                g_clsidNullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, reinterpret_cast<void**>(&pNullRenderer));
            if (FAILED(hr))
            {
                pGrabber->Release();
                pGrabberFilter->Release();
                return nullptr;
            }

            if (FAILED(pGB->AddFilter(pGrabberFilter, L"OpenRE Sample Grabber"))
                || FAILED(pGB->AddFilter(pNullRenderer, L"OpenRE Null Renderer")))
            {
                pNullRenderer->Release();
                pGrabber->Release();
                pGrabberFilter->Release();
                return nullptr;
            }

            // Configure the grabber before it connects: buffer samples for
            // BufferCB and prefer RGB24 (the decoder is asked to convert).
            pGrabber->SetBufferSamples(TRUE);
            pGrabber->SetOneShot(FALSE);
            AM_MEDIA_TYPE rgb24 = {};
            rgb24.majortype = MEDIATYPE_Video;
            rgb24.subtype = MEDIASUBTYPE_RGB24;
            rgb24.formattype = FORMAT_VideoInfo;
            pGrabber->SetMediaType(&rgb24);
            pGrabber->SetCallback(&g_movieSink, 1); // 1 = BufferCB

            IPin* pGrabberIn = getPin(pGrabberFilter, PINDIR_INPUT);
            IPin* pGrabberOut = getPin(pGrabberFilter, PINDIR_OUTPUT);
            IPin* pNullIn = getPin(pNullRenderer, PINDIR_INPUT);

            IPin* pDecoderOut = nullptr;
            IPin* pRendererIn = nullptr;
            if (pGrabberIn == nullptr || pGrabberOut == nullptr || pNullIn == nullptr
                || !findUncompressedVideoOutput(pGB, &pDecoderOut, &pRendererIn))
            {
                out("no video decoder output found in the movie graph. MarniMovie::Open", "");
                if (pGrabberIn != nullptr)
                    pGrabberIn->Release();
                if (pGrabberOut != nullptr)
                    pGrabberOut->Release();
                if (pNullIn != nullptr)
                    pNullIn->Release();
                pGrabber->Release();
                pGrabberFilter->Release();
                pNullRenderer->Release();
                return nullptr;
            }

            // Unplug the decoder -> video renderer connection and insert the
            // grabber in its place.
            pDecoderOut->Disconnect();
            pRendererIn->Disconnect();

            hr = pGB->Connect(pDecoderOut, pGrabberIn);
            if (FAILED(hr))
            {
                // The decoder cannot deliver RGB24; accept its native output
                // format and convert in the sample callback instead.
                AM_MEDIA_TYPE anyType = {};
                pGrabber->SetMediaType(&anyType);
                pGrabberIn->Disconnect();
                hr = pGB->Connect(pDecoderOut, pGrabberIn);
            }
            if (FAILED(hr))
            {
                out("failed to connect the video decoder to the sample grabber. MarniMovie::Open", "");
                pDecoderOut->Release();
                pRendererIn->Release();
                pGrabberIn->Release();
                pGrabberOut->Release();
                pNullIn->Release();
                pGrabber->Release();
                pGrabberFilter->Release();
                pNullRenderer->Release();
                return nullptr;
            }

            hr = pGB->Connect(pGrabberOut, pNullIn);
            if (FAILED(hr))
            {
                out("failed to connect the null renderer. MarniMovie::Open", "");
                pDecoderOut->Release();
                pRendererIn->Release();
                pGrabberIn->Release();
                pGrabberOut->Release();
                pNullIn->Release();
                pGrabber->Release();
                pGrabberFilter->Release();
                pNullRenderer->Release();
                return nullptr;
            }

            // The video renderer is no longer needed: removing it guarantees
            // the graph never creates a video window.
            PIN_INFO pi = {};
            if (SUCCEEDED(pRendererIn->QueryPinInfo(&pi)))
            {
                if (pi.pFilter != nullptr)
                {
                    pGB->RemoveFilter(pi.pFilter);
                    pi.pFilter->Release();
                }
            }

            *outGrabberIn = pGrabberIn; // caller releases
            pDecoderOut->Release();
            pRendererIn->Release();
            pGrabberOut->Release();
            pNullIn->Release();
            pGrabberFilter->Release();
            pNullRenderer->Release();
            return pGrabber; // caller releases
        }

        // Main thread (game loop): hands the latest captured frame to the GPU
        // backend, which composites it into the guest framebuffer during the
        // next present(). Called from movie_update_window and movie_update
        // while the graph is running.
        void movieForwardFrame()
        {
            std::vector<uint8_t> frame;
            int w = 0;
            int h = 0;
            {
                std::lock_guard lock(g_captureMutex);
                if (!g_capturedNew)
                    return;
                frame.swap(g_capturedFrame);
                w = g_capturedWidth;
                h = g_capturedHeight;
                g_capturedNew = false;
            }
            if (!frame.empty() && w > 0 && h > 0)
            {
                system::gpu::set_movie_frame(frame.data(), w, h, w * 3);
            }

            // Hand the buffer back to the capture side so it is not
            // reallocated every frame (only when no newer frame was captured
            // in the meantime).
            std::lock_guard lock(g_captureMutex);
            if (g_capturedFrame.empty())
                g_capturedFrame.swap(frame);
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

        // Forward any newly captured frames to the GPU backend, which
        // composites them into the guest framebuffer on the next present().
        // (The graph has no video renderer, so there is no video window to
        // update anymore.)
        movieForwardFrame();

        self->flag |= 2;
        return 1;
    }

    // 0x00414C00
    int __stdcall movie_update(MarniMovie* self)
    {
        uint32_t flag = self->flag;
        if ((flag & 1) == 0 || (flag & 2) == 0)
            return 1;

        // Forward any newly captured frames to the GPU backend.
        movieForwardFrame();

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

        // Clear the movie overlay so a stale cutscene frame does not linger in
        // the framebuffer while the movie restarts.
        system::gpu::clear_movie_frame();
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

        IMediaPosition* pMP = nullptr;
        pGB->QueryInterface(IID_IMediaPosition, reinterpret_cast<void**>(&pMP));
        self->pMediaPosition = pMP;

        // Phase 7: capture the decoded frames instead of playing them into a
        // video window. An ISampleGrabber is inserted between the decoder and
        // the video renderer, the video tail is re-routed through a Null
        // Renderer and the video renderer is removed from the graph, so no
        // video window is ever created. The audio path RenderFile built is
        // untouched (movies keep their sound).
        IPin* pGrabberIn = nullptr;
        ISampleGrabber* pGrabber = insertSampleGrabber(pGB, &pGrabberIn);
        if (pGrabber == nullptr || pGrabberIn == nullptr)
        {
            if (pGrabber != nullptr)
                pGrabber->Release();
            if (pGrabberIn != nullptr)
                pGrabberIn->Release();
            movie_release(self);
            return 0;
        }

        // Remember the decoder's output format so the sample callback can
        // convert each frame to top-down RGB24.
        readCaptureFormat(pGrabberIn);
        pGrabberIn->Release();
        pGrabber->Release();

        // No video window: the movie renders into the guest framebuffer
        // instead (full framebuffer, letterboxed by the swapchain blit). The
        // caller-supplied rect applied to the old child video window no longer
        // applies to a window; the framebuffer composite handles geometry.
        (void)pRect;
        (void)hWnd;
        self->pVideoWindow = nullptr;
        self->field_6C = 0;

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
        self->pVideoWindow = nullptr;

        self->flag = self->flag & 8;
        self->pos = 0.0;
        self->duration = 0.0;

        // No video window exists anymore (the graph has no video renderer, so
        // there is nothing to hide), but the captured frames and the overlay
        // must be dropped so a stale cutscene frame does not linger in the
        // guest framebuffer after the movie stops.
        std::lock_guard lock(g_captureMutex);
        g_capturedFrame.clear();
        g_capturedWidth = 0;
        g_capturedHeight = 0;
        g_capturedNew = false;
        system::gpu::clear_movie_frame();
    }
}
