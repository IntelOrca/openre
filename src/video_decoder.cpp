#include "video_decoder.h"

#include "logger.h"

#ifdef _WIN32

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <dshow.h>
#include <dvdmedia.h>
#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>

// The Windows 10 SDK no longer ships qedit.h (DirectShow Editing Services),
// which used to declare the sample-grabber filter and its interfaces. Declare
// them here with the historical qedit.h GUIDs and vtable layout.
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

namespace
{
    const GUID g_clsidSampleGrabber = { 0xC1F400A0, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
    const GUID g_clsidNullRenderer = { 0xC1F400A4, 0x3F08, 0x11D3, { 0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37 } };
    const GUID g_iidSampleGrabber = { 0x6B652FFF, 0x11FE, 0x4FCE, { 0x92, 0xAD, 0x02, 0x66, 0xB5, 0xD7, 0xC7, 0x8F } };
    const GUID g_iidSampleGrabberCb = { 0x0579154A, 0x2B53, 0x4994, { 0xB0, 0xD0, 0xE7, 0x73, 0x14, 0x8E, 0xFF, 0x85 } };

    // Not declared by this SDK's uuids.h ('I420' fourcc).
    const GUID g_mediaSubtypeI420 = { 0x30323449, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };

    void freeMediaType(AM_MEDIA_TYPE& mt)
    {
        if (mt.cbFormat != 0)
            CoTaskMemFree(mt.pbFormat);
        if (mt.pUnk != nullptr)
            mt.pUnk->Release();
        mt = {};
    }

    uint8_t clampU8(int v)
    {
        return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    }

    // YUV -> RGB (BT.601 MPEG-1 coefficients), written as RGBA8 (A = 255).
    void yuvToRgba(uint8_t y, uint8_t u, uint8_t v, uint8_t* rgba)
    {
        const int c = static_cast<int>(y) - 16;
        const int d = static_cast<int>(u) - 128;
        const int e = static_cast<int>(v) - 128;
        rgba[0] = clampU8((298 * c + 409 * e + 128) >> 8);           // R
        rgba[1] = clampU8((298 * c - 100 * d - 208 * e + 128) >> 8); // G
        rgba[2] = clampU8((298 * c + 516 * d + 128) >> 8);           // B
        rgba[3] = 255;
    }

    struct CaptureFormat
    {
        GUID subtype = GUID_NULL;
        int width = 0;
        int height = 0;
        int pitch = 0; // source row pitch in bytes (Y plane for planar formats)
        int plane = 0; // byte offset of the chroma planes for NV12/I420/YV12
        bool topDown = true;
    };

    // Forward declaration so Impl::SampleSink::BufferCB (defined inline inside
    // Impl, above the writeRow definition) can call it.
    void writeRow(const CaptureFormat& fmt, const BYTE* src, int srcPitch, int w, int h, int srcRow, int dstRow, uint8_t* base);

    int computeRowPitch(const GUID& subtype, int width, int height, LONG bitCount, DWORD sizeImage)
    {
        const bool planar = subtype == MEDIASUBTYPE_NV12 || subtype == g_mediaSubtypeI420 || subtype == MEDIASUBTYPE_YV12;
        const int bpp = planar ? 8 : static_cast<int>(bitCount);
        const int natural = width * (bpp / 8);
        if (sizeImage != 0 && height > 0)
        {
            if (planar)
            {
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
}

struct openre::VideoDecoder::Impl
{
    IGraphBuilder* pGraphBuilder = nullptr;
    IMediaControl* pMediaControl = nullptr;
    IMediaPosition* pMediaPosition = nullptr;
    double pos = 0.0;
    double duration = 0.0;
    bool running = false;

    CaptureFormat captureFormat;

    // Latest decoded frame (top-down RGBA8) plus its size; written by the
    // streaming-thread BufferCB, consumed by the game thread's acquireFrame.
    std::mutex captureMutex;
    std::vector<uint8_t> capturedFrame;
    int capturedWidth = 0;
    int capturedHeight = 0;
    bool capturedNew = false;

    class SampleSink final : public ISampleGrabberCB
    {
    public:
        explicit SampleSink(Impl* owner)
            : m_owner(owner)
        {
        }

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
            return --m_refs;
        }

        HRESULT STDMETHODCALLTYPE SampleCB(double /*sampleTime*/, IMediaSample* /*pSample*/) override
        {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE BufferCB(double /*sampleTime*/, BYTE* pBuffer, long bufferLen) override
        {
            const CaptureFormat fmt = m_owner->captureFormat;
            if (pBuffer == nullptr || fmt.width <= 0 || fmt.height <= 0 || fmt.pitch <= 0)
                return S_OK;

            const int w = fmt.width;
            const int h = fmt.height;
            const int srcPitch = fmt.pitch;
            const size_t rowBytes = static_cast<size_t>(w) * 4;
            const bool planar
                = fmt.subtype == MEDIASUBTYPE_NV12 || fmt.subtype == g_mediaSubtypeI420 || fmt.subtype == MEDIASUBTYPE_YV12;
            const bool yuv = planar || fmt.subtype == MEDIASUBTYPE_YUY2 || fmt.subtype == MEDIASUBTYPE_UYVY;
            const bool packed
                = fmt.subtype == MEDIASUBTYPE_RGB24 || fmt.subtype == MEDIASUBTYPE_RGB32 || fmt.subtype == MEDIASUBTYPE_ARGB32;
            if (!packed && !yuv)
                return S_OK;

            size_t needBytes = static_cast<size_t>(srcPitch) * static_cast<size_t>(h);
            if (planar)
                needBytes += needBytes / 2;
            if (static_cast<size_t>(bufferLen) < needBytes)
                return S_OK;

            std::lock_guard lock(m_owner->captureMutex);
            if (m_owner->capturedFrame.size() != rowBytes * static_cast<size_t>(h))
                m_owner->capturedFrame.resize(rowBytes * static_cast<size_t>(h));
            m_owner->capturedWidth = w;
            m_owner->capturedHeight = h;

            for (int srcRow = 0; srcRow < h; srcRow++)
            {
                const int dstRow = fmt.topDown ? srcRow : (h - 1 - srcRow);
                const auto* src = pBuffer + static_cast<size_t>(srcRow) * srcPitch;
                writeRow(fmt, src, srcPitch, w, h, srcRow, dstRow, m_owner->capturedFrame.data());
            }

            m_owner->capturedNew = true;
            return S_OK;
        }

    private:
        Impl* m_owner;
        std::atomic<ULONG> m_refs{ 1 };
    };

    SampleSink* sink = nullptr;

    uint8_t* rowDst(int dstRow, size_t rowBytes)
    {
        return capturedFrame.data() + static_cast<size_t>(dstRow) * rowBytes;
    }
};

// Free function kept out of the nested class so it can see the full CaptureFormat.
namespace
{
    void
    writeRow(const CaptureFormat& fmt, const BYTE* src, int srcPitch, int w, int /*h*/, int srcRow, int dstRow, uint8_t* base)
    {
        const size_t rowBytes = static_cast<size_t>(w) * 4;
        auto* dst = base + static_cast<size_t>(dstRow) * rowBytes;

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
                yuvToRgba(y0, u, v, dst);
                if (x + 1 < w)
                    yuvToRgba(y1, u, v, dst + 4);
                src += 4;
                dst += 8;
                x += 2;
            }
        }
        else if (fmt.subtype == MEDIASUBTYPE_NV12)
        {
            const uint8_t* cuv = src + fmt.plane + (srcRow / 2) * srcPitch;
            for (int x = 0; x < w; x++)
            {
                const uint8_t y = src[x];
                const uint8_t u = cuv[(x / 2) * 2];
                const uint8_t v = cuv[(x / 2) * 2 + 1];
                yuvToRgba(y, u, v, dst);
                dst += 4;
            }
        }
        else if (fmt.subtype == g_mediaSubtypeI420 || fmt.subtype == MEDIASUBTYPE_YV12)
        {
            const uint8_t* up = src + fmt.plane + (srcRow / 2) * (srcPitch / 2);
            const uint8_t* vp = up + (srcPitch / 2) * ((fmt.height + 1) / 2);
            for (int x = 0; x < w; x++)
            {
                const uint8_t y = src[x];
                const uint8_t u = (fmt.subtype == MEDIASUBTYPE_YV12) ? vp[x / 2] : up[x / 2];
                const uint8_t v = (fmt.subtype == MEDIASUBTYPE_YV12) ? up[x / 2] : vp[x / 2];
                yuvToRgba(y, u, v, dst);
                dst += 4;
            }
        }
        else if (fmt.subtype == MEDIASUBTYPE_ARGB32)
        {
            for (int x = 0; x < w; x++)
            {
                dst[0] = src[2]; // R
                dst[1] = src[1]; // G
                dst[2] = src[0]; // B
                dst[3] = src[3]; // A
                src += 4;
                dst += 4;
            }
        }
        else // RGB24 / RGB32 (BGR byte order, low bits in src[0])
        {
            const int srcStep = (fmt.subtype == MEDIASUBTYPE_RGB32) ? 4 : 3;
            for (int x = 0; x < w; x++)
            {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = 255;
                src += srcStep;
                dst += 4;
            }
        }
    }

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
                                *outVideoOut = pPin;
                                *outRendererIn = pPeer;
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

    void readCaptureFormat(IPin* pGrabberIn, CaptureFormat& out)
    {
        CaptureFormat fmt = {};
        AM_MEDIA_TYPE mt = {};
        if (FAILED(pGrabberIn->ConnectionMediaType(&mt)))
        {
            out = fmt;
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
                fmt.plane = fmt.pitch * fmt.height;
        }
        out = fmt;
        openre::logging::logInfo(
            "[video] grabber connected ({}x{} pitch={} topDown={})", fmt.width, fmt.height, fmt.pitch, fmt.topDown ? 1 : 0);
    }

    // Inserts an ISampleGrabber between the decoder and the video renderer,
    // routes the video tail through a Null Renderer and removes the video
    // renderer so the graph never opens a video window. Returns the grabber
    // interface (caller releases) or nullptr on failure.
    ISampleGrabber* insertSampleGrabber(IGraphBuilder* pGB, IPin** outGrabberIn)
    {
        IBaseFilter* pGrabberFilter = nullptr;
        if (FAILED(CoCreateInstance(
                g_clsidSampleGrabber,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_IBaseFilter,
                reinterpret_cast<void**>(&pGrabberFilter))))
            return nullptr;

        ISampleGrabber* pGrabber = nullptr;
        if (FAILED(pGrabberFilter->QueryInterface(g_iidSampleGrabber, reinterpret_cast<void**>(&pGrabber))))
        {
            pGrabberFilter->Release();
            return nullptr;
        }

        IBaseFilter* pNullRenderer = nullptr;
        if (FAILED(CoCreateInstance(
                g_clsidNullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_IBaseFilter, reinterpret_cast<void**>(&pNullRenderer))))
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

        pGrabber->SetBufferSamples(TRUE);
        pGrabber->SetOneShot(FALSE);
        AM_MEDIA_TYPE rgb24 = {};
        rgb24.majortype = MEDIATYPE_Video;
        rgb24.subtype = MEDIASUBTYPE_RGB24;
        rgb24.formattype = FORMAT_VideoInfo;
        pGrabber->SetMediaType(&rgb24);

        IPin* pGrabberIn = getPin(pGrabberFilter, PINDIR_INPUT);
        IPin* pGrabberOut = getPin(pGrabberFilter, PINDIR_OUTPUT);
        IPin* pNullIn = getPin(pNullRenderer, PINDIR_INPUT);

        IPin* pDecoderOut = nullptr;
        IPin* pRendererIn = nullptr;
        if (pGrabberIn == nullptr || pGrabberOut == nullptr || pNullIn == nullptr
            || !findUncompressedVideoOutput(pGB, &pDecoderOut, &pRendererIn))
        {
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

        pDecoderOut->Disconnect();
        pRendererIn->Disconnect();

        HRESULT hr = pGB->Connect(pDecoderOut, pGrabberIn);
        if (FAILED(hr))
        {
            AM_MEDIA_TYPE anyType = {};
            pGrabber->SetMediaType(&anyType);
            pGrabberIn->Disconnect();
            hr = pGB->Connect(pDecoderOut, pGrabberIn);
        }
        if (FAILED(hr))
        {
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

        if (FAILED(pGB->Connect(pGrabberOut, pNullIn)))
        {
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

        PIN_INFO pi = {};
        if (SUCCEEDED(pRendererIn->QueryPinInfo(&pi)))
        {
            if (pi.pFilter != nullptr)
            {
                pGB->RemoveFilter(pi.pFilter);
                pi.pFilter->Release();
            }
        }

        *outGrabberIn = pGrabberIn;
        pDecoderOut->Release();
        pRendererIn->Release();
        pGrabberOut->Release();
        pNullIn->Release();
        pGrabberFilter->Release();
        pNullRenderer->Release();
        return pGrabber;
    }
}

openre::VideoDecoder::VideoDecoder()
{
    m_impl = new Impl();
    CoInitialize(nullptr);
}

openre::VideoDecoder::~VideoDecoder()
{
    close();
    CoUninitialize();
    delete m_impl;
}

bool openre::VideoDecoder::open(const std::string& path)
{
    Impl* im = m_impl;
    m_width = 0;
    m_height = 0;
    close();

    if (path.empty())
        return false;

    WCHAR widePath[MAX_PATH] = {};
    if (MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, widePath, MAX_PATH) == 0)
    {
        openre::logging::logWarning("[video] failed to convert the movie path");
        return false;
    }

    IGraphBuilder* pGB = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, reinterpret_cast<void**>(&pGB))))
    {
        openre::logging::logWarning("[video] failed to create filter graph");
        return false;
    }
    im->pGraphBuilder = pGB;

    if (FAILED(pGB->RenderFile(widePath, nullptr)))
    {
        openre::logging::logWarning("[video] failed to render the movie file: {}", path.c_str());
        close();
        return false;
    }

    pGB->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&im->pMediaControl));
    pGB->QueryInterface(IID_IMediaPosition, reinterpret_cast<void**>(&im->pMediaPosition));

    IPin* pGrabberIn = nullptr;
    ISampleGrabber* pGrabber = insertSampleGrabber(pGB, &pGrabberIn);
    if (pGrabber == nullptr || pGrabberIn == nullptr)
    {
        if (pGrabber != nullptr)
            pGrabber->Release();
        if (pGrabberIn != nullptr)
            pGrabberIn->Release();
        close();
        return false;
    }

    readCaptureFormat(pGrabberIn, im->captureFormat);
    im->sink = new Impl::SampleSink(im);
    pGrabber->SetCallback(im->sink, 1);

    pGrabberIn->Release();
    pGrabber->Release();

    m_width = im->captureFormat.width;
    m_height = im->captureFormat.height;

    if (im->pMediaControl != nullptr)
    {
        HRESULT hr = im->pMediaControl->Run();
        if (hr == S_FALSE)
        {
            OAFilterState state;
            im->pMediaControl->GetState(INFINITE, &state);
        }
        if (im->pMediaPosition != nullptr)
        {
            REFTIME dur = 0.0;
            im->pMediaPosition->get_Duration(&dur);
            im->duration = dur;
            REFTIME pos = 0.0;
            im->pMediaPosition->get_CurrentPosition(&pos);
            im->pos = pos;
        }
        im->running = true;
    }

    if (m_width <= 0 || m_height <= 0)
    {
        openre::logging::logWarning("[video] decoder produced no usable video format");
        close();
        return false;
    }
    return true;
}

bool openre::VideoDecoder::acquireFrame(std::vector<uint8_t>& rgba, int& w, int& h)
{
    Impl* im = m_impl;
    std::lock_guard lock(im->captureMutex);
    if (!im->capturedNew)
        return false;
    rgba.swap(im->capturedFrame);
    w = im->capturedWidth;
    h = im->capturedHeight;
    im->capturedNew = false;
    if (im->capturedFrame.empty())
        im->capturedFrame.shrink_to_fit();
    return !rgba.empty() && w > 0 && h > 0;
}

double openre::VideoDecoder::position() const
{
    Impl* im = m_impl;
    if (im->pMediaPosition != nullptr)
    {
        REFTIME pos = 0.0;
        im->pMediaPosition->get_CurrentPosition(&pos);
        im->pos = pos;
    }
    return im->pos;
}

double openre::VideoDecoder::duration() const
{
    return m_impl->duration;
}

bool openre::VideoDecoder::finished() const
{
    // m_impl->pos is refreshed by position(), which the caller invokes each
    // frame; reading it here avoids a non-const call from this const method.
    return m_impl->duration > 0.0 && m_impl->pos >= m_impl->duration;
}

void openre::VideoDecoder::close()
{
    Impl* im = m_impl;
    if (im->pMediaControl != nullptr)
    {
        im->pMediaControl->Stop();
        im->pMediaControl->Release();
        im->pMediaControl = nullptr;
    }
    if (im->pMediaPosition != nullptr)
    {
        im->pMediaPosition->Release();
        im->pMediaPosition = nullptr;
    }
    if (im->pGraphBuilder != nullptr)
    {
        im->pGraphBuilder->Release();
        im->pGraphBuilder = nullptr;
    }
    if (im->sink != nullptr)
    {
        im->sink->Release();
        im->sink = nullptr;
    }
    im->running = false;
    im->pos = 0.0;
    im->duration = 0.0;
    {
        std::lock_guard lock(im->captureMutex);
        im->capturedFrame.clear();
        im->capturedWidth = 0;
        im->capturedHeight = 0;
        im->capturedNew = false;
    }
}

#else

// Non-Windows stub: DirectShow is unavailable, so no movie can be decoded.
openre::VideoDecoder::VideoDecoder() = default;
openre::VideoDecoder::~VideoDecoder() = default;

bool openre::VideoDecoder::open(const std::string&)
{
    return false;
}
bool openre::VideoDecoder::acquireFrame(std::vector<uint8_t>&, int&, int&)
{
    return false;
}
double openre::VideoDecoder::position() const
{
    return 0.0;
}
double openre::VideoDecoder::duration() const
{
    return 0.0;
}
bool openre::VideoDecoder::finished() const
{
    return true;
}
void openre::VideoDecoder::close() {}

#endif
