#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openre
{
    // Self-contained DirectShow-based decoder for the MPEG-1 Program Stream
    // movie files used by RE2. Each decoded frame is captured as top-down
    // RGBA8 on the graph's streaming thread; the caller pulls the latest frame
    // from the game thread via acquireFrame(). Intentionally has no dependency
    // on the legacy marni movie integration or on any guest framebuffer.
    class VideoDecoder
    {
    public:
        VideoDecoder();
        ~VideoDecoder();

        VideoDecoder(const VideoDecoder&) = delete;
        VideoDecoder& operator=(const VideoDecoder&) = delete;

        // Builds the filter graph for `path` and begins playback. Returns
        // false on failure. On success videoWidth()/videoHeight() report the
        // decoded frame size.
        bool open(const std::string& path);

        // Pulls the most recently captured frame. Returns true and fills `rgba`
        // (w*h*4 bytes, top-down RGBA8) when a new frame has been decoded since
        // the previous call. `w` and `h` receive the frame dimensions.
        bool acquireFrame(std::vector<uint8_t>& rgba, int& w, int& h);

        // Playback position (seconds) and total duration (seconds).
        double position() const;
        double duration() const;

        // True once playback has reached (or passed) the end.
        bool finished() const;

        // Stops playback and releases the filter graph.
        void close();

        int videoWidth() const
        {
            return m_width;
        }
        int videoHeight() const
        {
            return m_height;
        }

    private:
        struct Impl;
        Impl* m_impl = nullptr;
        int m_width = 0;
        int m_height = 0;
    };
}
