#pragma once

#include "gfx.h"
#include "sfx.h"

#include <memory>

namespace openre
{
    class Stream;
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

    struct MovieFrame
    {
        int64_t beginTime{};
        int64_t endTime{};
        std::vector<uint8_t> data;
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

        virtual MovieState getState() const = 0;
        virtual float getPosition() const = 0;
        virtual float getDuration() const = 0;
        virtual openre::audio::AudioFormat getAudioFormat() const = 0;
        virtual openre::graphics::VideoFormat getVideoFormat() const = 0;
        virtual MovieFrame dequeueAudioFrame() = 0;
        virtual MovieFrame dequeueVideoFrame() = 0;
    };

    std::unique_ptr<MoviePlayer> createMoviePlayer();
}
