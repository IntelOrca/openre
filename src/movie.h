#pragma once

#include "gfx.h"

#include <memory>

namespace openre
{
    class Stream;
}

namespace openre::audio
{
    struct AudioBuffer
    {
        std::vector<uint8_t> data;
        uint32_t sampleRate{};
        uint8_t channels{};
        uint8_t bitsPerSample{};
    };
}

namespace openre::movie
{
    enum class MovieState
    {
        blank,
        unsupported,
        error,
        stopped,
        playing,
        finished
    };

    class MoviePlayer
    {
    public:
        virtual ~MoviePlayer() = default;

        virtual void open(std::unique_ptr<Stream> stream) = 0;
        virtual void close() = 0;
        virtual void play() = 0;
        virtual void stop() = 0;
        virtual void setPosition(float position) = 0;
        virtual void queueFrames(uint32_t count) = 0;
        virtual void dequeueNextFrame() = 0;

        virtual MovieState getState() const = 0;
        virtual float getPosition() const = 0;
        virtual float getDuration() const = 0;
        virtual openre::graphics::Size getResolution() const = 0;
        virtual uint32_t getSampleRate() const = 0;
        virtual uint32_t getFrameRate() const = 0;
        virtual const openre::audio::AudioBuffer* getAudioFrame() = 0;
        virtual const openre::graphics::TextureBuffer* getVideoFrame() = 0;
    };

    std::unique_ptr<MoviePlayer> createMoviePlayer();
}
