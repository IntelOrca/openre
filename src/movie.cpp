#include "movie.h"
#include "gfx.h"
#include "shell.h"

#include <chrono>
#include <stdexcept>

#define ENABLE_MOVIE_FFMPEG
#ifdef ENABLE_MOVIE_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

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
        std::unique_ptr<TextureBuffer> textureBuffer;
        std::chrono::time_point<std::chrono::high_resolution_clock> lastUpdate;
        float position{};
        int64_t videoFrameIndex{};

        AVFormatContext* ctx{};

        AVStream* audioStream{};
        const AVCodec* audioCodec{};
        AVCodecContext* audioCodecCtx{};
        AVFrameQueue audioFrames;

        AVStream* videoStream{};
        const AVCodec* videoCodec{};
        AVCodecContext* videoCodecCtx{};
        AVFrame* rawFrame{};
        AVFrameQueue videoFrames;
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
                        if (avcodec_parameters_to_context(this->audioCodecCtx, this->audioStream->codecpar) != 0
                            || avcodec_open2(this->audioCodecCtx, this->audioCodec, nullptr) != 0)
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
                            this->rawFrame = av_frame_alloc();
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
            this->videoFrames = AVFrameQueue(8);
            this->position = 0;
            this->videoFrameIndex = 0;
        }

        void close() override
        {
            av_frame_free(&this->rawFrame);

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
            this->state = MovieState::playing;
            this->lastUpdate = std::chrono::high_resolution_clock::now();
        }

        void stop() override
        {
            this->state = MovieState::stopped;
        }

        void setPosition(float position) {}

        void queueFrames(uint32_t count) override
        {
            while (this->videoFrames.getSize() < count)
            {
                AVPacket packet;
                auto ret = av_read_frame(ctx, &packet);
                if (ret < 0)
                    break;

                if (packet.stream_index == videoStream->index)
                {
                    ret = avcodec_send_packet(videoCodecCtx, &packet);
                    if (ret == 0)
                    {
                        ret = avcodec_receive_frame(videoCodecCtx, this->rawFrame);
                        if (ret == 0)
                        {
                            auto frame = this->videoFrames.queue();
                            if (frame == nullptr)
                                break;

                            ret = sws_scale_frame(swsCtx, frame, this->rawFrame);
                            if (ret == 0)
                            {
                                // yay
                            }
                        }
                    }
                }
                av_packet_unref(&packet);
            }
        }

        void dequeueNextFrame()
        {
            if (this->state != MovieState::playing)
                return;

            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUpdate).count();
            this->position += duration / 1000.0f;
            this->lastUpdate = now;

            AVFrame* frame{};
            while (true)
            {
                queueFrames(4);

                auto frameDuration = 1.0f / this->getFrameRate();
                auto nextFameTimeCode = this->videoFrameIndex * frameDuration;
                if (this->position < nextFameTimeCode)
                    break;

                this->videoFrameIndex++;

                frame = videoFrames.dequeue();
                if (frame == nullptr)
                {
                    this->state = MovieState::finished;
                }
            }

            if (frame == nullptr)
                return;

            if (!this->textureBuffer)
            {
                this->textureBuffer = std::make_unique<TextureBuffer>();
                this->textureBuffer->width = this->videoCodecCtx->width;
                this->textureBuffer->height = this->videoCodecCtx->height;
                this->textureBuffer->bpp = 24;
                this->textureBuffer->pixels.resize(this->videoCodecCtx->width * this->videoCodecCtx->height * 3);
            }

            std::memcpy(this->textureBuffer->pixels.data(), frame->data[0], this->textureBuffer->pixels.size());
        }

        MovieState getState() const
        {
            return this->state;
        }

        float getPosition() const
        {
            return this->position;
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

        const openre::audio::AudioBuffer* getAudioFrame()
        {
            return nullptr;
        }

        const openre::graphics::TextureBuffer* getVideoFrame()
        {
            return textureBuffer.get();
        }

    private:
        void closeWithError()
        {
            this->state = MovieState::error;
            close();
        }

        void throwOnError(int returnCode)
        {
            if (returnCode != 0)
            {
                throw std::runtime_error("Failed with code");
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
        ~DummyMoviePlayer() override {}

        void open(std::unique_ptr<Stream> stream) override
        {
            state = MovieState::unsupported;
        }

        MovieState getState() const override
        {
            return state;
        }

        void play() override {}
        void stop() override {}
        void setPosition(float position) override {}
        void queueFrames(uint32_t count) override {}
        void dequeueNextFrame() override {}

        float getPosition() const override
        {
            return 0;
        }

        float getDuration() const override
        {
            return 0;
        }

        Size getResolution() const override
        {
            return {};
        }

        uint32_t getSampleRate() const override
        {
            return 0;
        }

        uint32_t getFrameRate() const override
        {
            return 0;
        }

        const openre::audio::AudioBuffer* getAudioFrame() override
        {
            return nullptr;
        }

        const openre::graphics::TextureBuffer* getVideoFrame() override
        {
            return nullptr;
        }
    };

    std::unique_ptr<MoviePlayer> createMoviePlayer()
    {
        return std::make_unique<DummyMoviePlayer>();
    }
}

#endif
