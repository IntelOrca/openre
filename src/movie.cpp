#include "movie.h"
#include "shell.h"

#include <chrono>
#include <cstring>
#include <queue>
#include <stdexcept>

#ifdef ENABLE_MOVIE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

using namespace openre::audio;
using namespace openre::graphics;
using namespace openre::movie;

namespace openre::movie
{
    class AVFrameQueue
    {
    private:
        std::vector<AVFrame*> q;
        size_t read{};
        size_t write{};
        size_t size{};

        void clear()
        {
            for (size_t i = 0; i < this->q.size(); i++)
            {
                av_frame_free(&this->q[i]);
            }
            q.clear();
        }

    public:
        AVFrameQueue() = default;
        AVFrameQueue(const AVFrameQueue&) = delete;
        AVFrameQueue& operator=(const AVFrameQueue&) = delete;
        AVFrameQueue(AVFrameQueue&& rhs) noexcept = delete;

        AVFrameQueue& operator=(AVFrameQueue&& rhs) noexcept
        {
            clear();
            this->q = std::move(rhs.q);
            return *this;
        }

        AVFrameQueue(size_t capacity)
        {
            this->q.resize(capacity);
            for (size_t i = 0; i < this->q.size(); i++)
            {
                this->q[i] = av_frame_alloc();
            }
        }

        ~AVFrameQueue()
        {
            clear();
        }

        size_t getCapacity() const
        {
            return this->q.capacity();
        }

        size_t getSize() const
        {
            return this->size;
        }

        AVFrame* queue()
        {
            if (this->q.size() == 0)
                return nullptr;

            auto nextWrite = (this->write + 1) % this->q.size();
            if (nextWrite == this->read)
                return nullptr;
            auto result = this->q[this->write];
            this->write = nextWrite;
            this->size++;
            return result;
        }

        AVFrame* dequeue()
        {
            if (this->q.size() == 0)
                return nullptr;

            if (this->read == this->write)
                return nullptr;
            auto result = this->q[this->read];
            this->read = (this->read + 1) % this->q.size();
            this->size--;
            return result;
        }
    };

    class FFMpegMovie : public MoviePlayer
    {
    private:
        MovieState state{};
        std::unique_ptr<Stream> inputStream;
        std::queue<MovieFrame> audioFrames;
        std::queue<MovieFrame> videoFrames;
        std::chrono::time_point<std::chrono::high_resolution_clock> playTimePoint;
        float position{};
        int64_t audioFrameIndex{};
        int64_t videoFrameIndex{};

        AVFormatContext* ctx{};

        AVStream* audioStream{};
        const AVCodec* audioCodec{};
        AVCodecContext* audioCodecCtx{};
        AVFrame* rawAudioFrame{};

        AVStream* videoStream{};
        const AVCodec* videoCodec{};
        AVCodecContext* videoCodecCtx{};
        AVFrame* rawVideoFrame{};
        AVFrame* convertedVideoFrame{};
        SwsContext* swsCtx{};

    public:
        FFMpegMovie() = default;
        FFMpegMovie(const FFMpegMovie&) = delete;

        ~FFMpegMovie() override
        {
            close();
        }

        void open(std::unique_ptr<Stream> stream) override
        {
            inputStream = std::move(stream);

            ctx = avformat_alloc_context();
            if (ctx == nullptr)
                return closeWithError();

            auto streamBufferSize = 4 * 1024; // 4 KiB
            auto streamBuffer = av_malloc(streamBufferSize);
            ctx->pb = avio_alloc_context(
                static_cast<unsigned char*>(streamBuffer),
                streamBufferSize,
                0,
                this,
                [](void* opaque, uint8_t* buf, int buf_size) -> int {
                    auto result = static_cast<int>(((FFMpegMovie*)opaque)->inputStream->read(buf, buf_size));
                    if (result == 0)
                    {
                        return AVERROR_EOF;
                    }
                    return result;
                },
                nullptr,
                [](void* opaque, int64_t offset, int whence) -> int64_t {
                    return ((FFMpegMovie*)opaque)->inputStream->seek(offset, whence);
                });
            if (ctx->pb == nullptr)
                return closeWithError();

            if (avformat_open_input(&this->ctx, nullptr, nullptr, nullptr) != 0)
                return closeWithError();

            if (avformat_find_stream_info(this->ctx, nullptr) != 0)
                return closeWithError();

            for (unsigned int i = 0; i < this->ctx->nb_streams; i++)
            {
                auto stream = this->ctx->streams[i];
                if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                {
                    if (this->audioStream == nullptr)
                    {
                        this->audioStream = stream;
                    }
                }
                else if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                {
                    if (this->videoStream == nullptr)
                    {
                        this->videoStream = stream;
                    }
                }
            }

            if (this->audioStream != nullptr)
            {
                this->audioCodec = avcodec_find_decoder(this->audioStream->codecpar->codec_id);
                if (this->audioCodec != nullptr)
                {
                    this->audioCodecCtx = avcodec_alloc_context3(this->audioCodec);
                    if (this->audioCodecCtx != nullptr)
                    {
                        if (avcodec_parameters_to_context(this->audioCodecCtx, this->audioStream->codecpar) == 0
                            && avcodec_open2(this->audioCodecCtx, this->audioCodec, nullptr) == 0)
                        {
                            this->rawAudioFrame = av_frame_alloc();
                        }
                        else
                        {
                            avcodec_free_context(&this->audioCodecCtx);
                        }
                    }
                }
            }

            if (this->videoStream != nullptr)
            {
                this->videoCodec = avcodec_find_decoder(this->videoStream->codecpar->codec_id);
                if (this->videoCodec != nullptr)
                {
                    this->videoCodecCtx = avcodec_alloc_context3(this->videoCodec);
                    if (this->videoCodecCtx != nullptr)
                    {
                        if (avcodec_parameters_to_context(this->videoCodecCtx, this->videoStream->codecpar) == 0
                            && avcodec_open2(this->videoCodecCtx, this->videoCodec, nullptr) == 0)
                        {
                            this->rawVideoFrame = av_frame_alloc();
                            this->convertedVideoFrame = av_frame_alloc();
                            this->swsCtx = sws_getContext(
                                this->videoCodecCtx->width,
                                this->videoCodecCtx->height,
                                this->videoCodecCtx->pix_fmt,
                                this->videoCodecCtx->width,
                                this->videoCodecCtx->height,
                                AV_PIX_FMT_RGB24,
                                SWS_BILINEAR,
                                nullptr,
                                nullptr,
                                nullptr);
                        }
                        else
                        {
                            avcodec_free_context(&this->videoCodecCtx);
                        }
                    }
                }
            }

            this->state = MovieState::stopped;
            this->position = 0;
            this->videoFrameIndex = 0;
        }

        void close() override
        {
            av_frame_free(&this->rawVideoFrame);

            if (this->state != MovieState::error)
                this->state = MovieState::blank;
            this->inputStream = nullptr;
            if (this->ctx->pb != nullptr)
                av_freep(&this->ctx->pb->buffer);
            avio_context_free(&this->ctx->pb);

            avformat_close_input(&this->ctx);
            this->ctx = nullptr;
            avcodec_free_context(&this->videoCodecCtx);
            ctx = nullptr;
        }

        void play() override
        {
            bufferFrames(256);

            this->state = MovieState::playing;
            this->playTimePoint = std::chrono::high_resolution_clock::now();
        }

        void stop() override
        {
            this->state = MovieState::stopped;
        }

        void setPosition(float position) {}

        AudioFormat getAudioFormat() const override
        {
            return { static_cast<uint32_t>(this->audioCodecCtx->sample_rate),
                     static_cast<uint8_t>(this->audioCodecCtx->ch_layout.nb_channels),
                     static_cast<uint8_t>(this->audioCodecCtx->sample_fmt) };
        }
        VideoFormat getVideoFormat() const override
        {
            return { { static_cast<uint32_t>(this->videoCodecCtx->width), static_cast<uint32_t>(this->videoCodecCtx->height) },
                     static_cast<uint16_t>(this->videoCodecCtx->framerate.num / this->videoCodecCtx->framerate.den) };
        }

        MovieState getState() const
        {
            return this->state;
        }

        float getPosition() const
        {
            auto now = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - this->playTimePoint).count();
            return ms / 1000.0f;
        }

        float getDuration() const
        {
            return 0;
        }

        Size getResolution() const
        {
            return { static_cast<uint32_t>(this->videoCodecCtx->width), static_cast<uint32_t>(this->videoCodecCtx->height) };
        }

        uint32_t getSampleRate() const
        {
            if (this->audioCodecCtx == nullptr)
                return 0;
            return this->audioCodecCtx->sample_rate;
        }

        uint32_t getFrameRate() const
        {
            if (this->videoCodecCtx == nullptr)
                return 0;
            return this->videoCodecCtx->framerate.num / this->videoCodecCtx->framerate.den;
        }

        MovieFrame dequeueAudioFrame() override
        {
            if (this->state != MovieState::playing)
                return {};

            bufferFrames(128);

            if (this->audioFrames.size() == 0)
                return {};

            auto frame = std::move(this->audioFrames.front());
            this->audioFrames.pop();

            if (this->audioFrames.size() == 0 && this->videoFrames.size() == 0)
                this->state = MovieState::stopped;

            return frame;
        }

        MovieFrame dequeueVideoFrame() override
        {
            if (this->state != MovieState::playing)
                return {};

            bufferFrames(128);

            if (this->videoFrames.size() == 0)
                return {};

            auto frame = std::move(this->videoFrames.front());
            this->videoFrames.pop();

            if (this->audioFrames.size() == 0 && this->videoFrames.size() == 0)
                this->state = MovieState::stopped;

            return frame;
        }

    private:
        void closeWithError()
        {
            this->state = MovieState::error;
            close();
        }

        void bufferFrames(uint32_t count)
        {
            while (this->videoFrames.size() < count || this->audioFrames.size() < count)
            {
                AVPacket packet;
                auto ret = av_read_frame(ctx, &packet);
                if (ret < 0)
                    break;

                if (packet.stream_index == audioStream->index)
                {
                    ret = avcodec_send_packet(audioCodecCtx, &packet);
                    if (ret == 0)
                    {
                        ret = avcodec_receive_frame(audioCodecCtx, this->rawAudioFrame);
                        if (ret == 0)
                        {
                            auto& frame = this->audioFrames.emplace();
                            frame.beginTime = 1000 * this->audioFrameIndex * this->audioCodecCtx->frame_size
                                / this->audioCodecCtx->sample_rate;
                            this->audioFrameIndex++;
                            frame.endTime = 1000 * this->audioFrameIndex * this->audioCodecCtx->frame_size
                                / this->audioCodecCtx->sample_rate;

                            auto numSamples = this->rawAudioFrame->nb_samples;
                            auto numChannels = this->rawAudioFrame->ch_layout.nb_channels;
                            frame.data.resize(numSamples * numChannels * sizeof(uint16_t));
                            auto dst = reinterpret_cast<uint16_t*>(frame.data.data());
                            for (auto i = 0; i < numSamples; i++)
                            {
                                for (auto n = 0; n < numChannels; n++)
                                {
                                    *dst++ = reinterpret_cast<uint16_t*>(this->rawAudioFrame->data[n])[i];
                                }
                            }
                        }
                    }
                }
                else if (packet.stream_index == videoStream->index)
                {
                    ret = avcodec_send_packet(videoCodecCtx, &packet);
                    if (ret == 0)
                    {
                        ret = avcodec_receive_frame(videoCodecCtx, this->rawVideoFrame);
                        if (ret == 0)
                        {
                            ret = sws_scale_frame(swsCtx, this->convertedVideoFrame, this->rawVideoFrame);
                            if (ret >= 0)
                            {
                                auto& frame = this->videoFrames.emplace();
                                frame.beginTime = this->videoFrameIndex * ((int64_t)1000 * this->videoCodecCtx->framerate.den)
                                    / this->videoCodecCtx->framerate.num;
                                this->videoFrameIndex++;
                                frame.endTime = this->videoFrameIndex * ((int64_t)1000 * this->videoCodecCtx->framerate.den)
                                    / this->videoCodecCtx->framerate.num;

                                auto frameSize = this->convertedVideoFrame->width * this->convertedVideoFrame->height * 3;
                                frame.data.resize(frameSize);
                                std::memcpy(frame.data.data(), this->convertedVideoFrame->data[0], frameSize);
                            }
                        }
                    }
                }
                av_packet_unref(&packet);
            }
        }
    };

    std::unique_ptr<MoviePlayer> createMoviePlayer()
    {
        return std::make_unique<FFMpegMovie>();
    }
}

#else

using namespace openre::movie;

namespace openre::movie
{
    /**
     * Default implementation of MoviePlayer that does nothing, state is always
     * unsupported when a stream is opened.
     */
    class DummyMoviePlayer : public MoviePlayer
    {
    private:
        MovieState state{};

    public:
        void open(std::unique_ptr<Stream> stream) override
        {
            state = MovieState::unsupported;
        }

        void close() override
        {
            state = MovieState::blank;
        }

        void play() override {}

        void stop() override {}

        void setPosition(float position) override {}

        MovieState getState() const override
        {
            return state;
        }

        float getPosition() const override
        {
            return 0;
        }

        float getDuration() const override
        {
            return 0;
        }

        openre::audio::AudioFormat getAudioFormat() const override
        {
            return {};
        }

        openre::graphics::VideoFormat getVideoFormat() const override
        {
            return {};
        }

        MovieFrame dequeueAudioFrame() override
        {
            return {};
        }

        MovieFrame dequeueVideoFrame() override
        {
            return {};
        }
    };

    std::unique_ptr<MoviePlayer> createMoviePlayer()
    {
        return std::make_unique<DummyMoviePlayer>();
    }
}

#endif
