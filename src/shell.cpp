#include "shell.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include "file.h"

namespace fs = std::filesystem;

namespace openre
{
    class FileStream : public Stream
    {
    private:
        SDL_IOStream* baseStream;

    public:
        static std::unique_ptr<FileStream> fromPath(std::string_view path)
        {
            std::string szpath(path);
            auto sdlStream = SDL_IOFromFile(szpath.c_str(), "rb");
            return sdlStream == nullptr ? nullptr : std::make_unique<FileStream>(sdlStream);
        }

        FileStream(SDL_IOStream* sdlStream)
            : baseStream(sdlStream)
        {
        }

    public:
        ~FileStream() override
        {
            SDL_CloseIO(this->baseStream);
        }

        size_t read(void* buffer, size_t size) override
        {
            return SDL_ReadIO(this->baseStream, buffer, size);
        }

        size_t write(const void* buffer, size_t size) override
        {
            return SDL_WriteIO(this->baseStream, buffer, size);
        }

        void seek(int64_t offset, int origin) override
        {
            SDL_SeekIO(this->baseStream, offset, (SDL_IOWhence)origin);
        }

        int64_t tell() const override
        {
            return SDL_TellIO(this->baseStream);
        }
    };

    struct GLTexture
    {
        GLuint handle;
        uint32_t width;
        uint32_t height;
    };

    class SDL2OpenREShell : public OpenREShell
    {
    private:
        SDL_Window* window{};
        SDL_GLContext glContext{};
        std::vector<GLTexture> textures;
        std::vector<OpenREPrim> primitives;
        std::function<void()> updateCallback;

        uint32_t windowWidth = 640 * 2;
        uint32_t windowHeight = 480 * 2;
        uint32_t renderWidth = 320;
        uint32_t renderHeight = 240;

        std::vector<fs::path> basePaths;

    public:
        ~SDL2OpenREShell() {}

        StreamResult getStream(std::string_view path, const std::vector<std::string_view>& extensions) override
        {
            StreamResult result;
            for (size_t j = 0; j < this->basePaths.size(); j++)
            {
                for (size_t i = 0; i < extensions.size(); i++)
                {
                    auto newPath = this->basePaths[j] / path;
                    newPath.replace_extension(extensions[i]);
                    auto stream = FileStream::fromPath(newPath.u8string());
                    if (stream)
                    {
                        result.stream = std::move(stream);
                        result.found = 1;
                        result.extensionIndex = static_cast<uint8_t>(i);
                        result.isMod = j != this->basePaths.size() - 1;
                        return result;
                    }
                }
            }
            return result;
        }

        TextureHandle loadTexture(uint32_t width, uint32_t height, void* pixels) override
        {
            GLuint handle;
            glGenTextures(1, &handle);
            glBindTexture(GL_TEXTURE_2D, handle);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);

            auto& texture = textures.emplace_back();
            texture.handle = handle;
            texture.width = width;
            texture.height = height;
            return textures.size();
        }

        void pushPrimitive(const OpenREPrim& prim) override
        {
            primitives.push_back(prim);
        }

        void setUpdate(std::function<void()> callback) override
        {
            updateCallback = callback;
        }

        void run() override
        {
            init();

            auto done = false;
            while (!done)
            {
                SDL_Event event;
                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_EVENT_QUIT)
                        done = true;
                }

                update();
                render();
            }
        }

    private:
        void init()
        {
            basePaths = { "M:\\git\\openre\\games\\re2hd", "M:\\git\\openre\\games\\re2" };

            SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
            this->window = SDL_CreateWindow("OpenRE", windowWidth, windowHeight, SDL_WINDOW_OPENGL);
            this->glContext = SDL_GL_CreateContext(window);
            SDL_GL_MakeCurrent(window, glContext);
            SDL_GL_SetSwapInterval(1);

            glViewport(0, 0, windowWidth, windowHeight);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, renderWidth, renderHeight, 0, 1, -1);
            glEnable(GL_CULL_FACE);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        void update()
        {
            updateCallback();
        }

        void render()
        {
            for (const auto& p : primitives)
            {
                if (p.kind == OpenREPrimKind::TextureQuad)
                {
                    if (p.texture != 0 && p.texture <= textures.size())
                    {
                        glEnable(GL_TEXTURE_2D);
                        glBindTexture(GL_TEXTURE_2D, textures[p.texture - 1].handle);
                    }
                    else
                    {
                        glDisable(GL_TEXTURE_2D);
                    }
                    glBegin(GL_QUADS);
                    glColor4f(p.color.r, p.color.g, p.color.b, p.color.a);
                    for (auto i = 0; i < 4; i++)
                    {
                        glVertex3f(p.vertices[i].x, p.vertices[i].y, p.vertices[i].z);
                        glTexCoord2f(p.vertices[i].s, p.vertices[i].t);
                    }
                    glEnd();
                }
            }
            primitives.clear();

            SDL_GL_SwapWindow(window);
        }
    };

    static void pushQuad(OpenREShell& shell, TextureHandle texture, Color4f color, float x, float y, float z, float w, float h)
    {
        OpenREPrim prim;
        prim.kind = OpenREPrimKind::TextureQuad;
        prim.texture = texture;
        prim.color = color;
        prim.vertices[0].x = x;
        prim.vertices[0].y = y;
        prim.vertices[0].z = z;
        prim.vertices[0].s = 0;
        prim.vertices[0].t = 1;
        prim.vertices[1].x = x;
        prim.vertices[1].y = y + h;
        prim.vertices[1].z = z;
        prim.vertices[1].s = 1;
        prim.vertices[1].t = 1;
        prim.vertices[2].x = x + w;
        prim.vertices[2].y = y + h;
        prim.vertices[2].z = z;
        prim.vertices[2].s = 1;
        prim.vertices[2].t = 0;
        prim.vertices[3].x = x + w;
        prim.vertices[3].y = y;
        prim.vertices[3].z = z;
        prim.vertices[3].s = 0;
        prim.vertices[3].t = 0;
        shell.pushPrimitive(prim);
    }

    struct TextureBuffer
    {
        uint32_t width{};
        uint32_t height{};
        std::vector<uint8_t> pixels;
    };

#pragma pack(push, 1)
    struct BitmapFileHeader
    {
        uint16_t signature;
        uint32_t fileSize;
        uint16_t reserved1;
        uint16_t reserved2;
        uint32_t pixelOffset;
    };

    struct BitmapHeader
    {
        uint32_t size;
        uint32_t width;
        uint32_t height;
        uint16_t planes;
        uint16_t bpp;
    };
#pragma pack(pop)

    static TextureBuffer decodeAdt(std::vector<uint8_t> input, uint32_t width, uint32_t height)
    {
        auto path = "F:\\games\\openre\\data\\Common\\Data\\Title_bg.adt";
        auto buffer = new uint8_t[((256 * 256) + (128 * 128)) * 2];
        auto ret = openre::file::load_adt(path, (uint32_t*)buffer, 4);

        // Reorganize
        auto reorgBuffer = new uint8_t[320 * 240 * 2];
        auto src = buffer;
        auto dst = reorgBuffer;
        for (uint32_t y = 0; y < 240; y++)
        {
            std::memcpy(dst, src, 256 * 2);
            src += 256 * 2;
            dst += 320 * 2;
        }

        src = buffer + (256 * 256 * 2);
        dst = reorgBuffer + (256 * 2);
        for (uint32_t y = 0; y < 128; y++)
        {
            std::memcpy(dst, src, 64 * 2);
            src += 128 * 2;
            dst += 320 * 2;
        }
        src = buffer + (256 * 256 * 2) + (64 * 2);
        for (uint32_t y = 128; y < 240; y++)
        {
            std::memcpy(dst, src, 64 * 2);
            src += 128 * 2;
            dst += 320 * 2;
        }

        // R5G5B5 to RGB888
        auto numPixels = 320 * 240;
        std::vector<uint8_t> newBuffer(numPixels * 3);
        src = reorgBuffer;
        dst = newBuffer.data();
        for (int i = 0; i < numPixels; i++)
        {
            auto c16 = src[0] | (src[1] << 8);
            auto r = ((c16 >> 0) & 0b11111) * 8;
            auto g = ((c16 >> 5) & 0b11111) * 8;
            auto b = ((c16 >> 10) & 0b11111) * 8;
            *dst++ = r;
            *dst++ = g;
            *dst++ = b;
            src += 2;
        }

        TextureBuffer result;
        result.width = 320;
        result.height = 240;
        result.pixels = std::move(newBuffer);
        return result;
    }

    static TextureBuffer decodeBmp(std::vector<uint8_t> input)
    {
        auto header1 = (BitmapFileHeader*)input.data();
        auto header2 = (BitmapHeader*)(input.data() + 14);
        auto pixelData = input.data() + header1->pixelOffset;

        TextureBuffer result;
        result.width = static_cast<uint32_t>(header2->width);
        result.height = static_cast<uint32_t>(header2->height);
        result.pixels.resize(result.width * result.height * 3);

        auto pitch = ((header2->width * 3) + 3) & ~3;
        auto src = pixelData;
        auto dst = result.pixels.data() + ((result.height - 1) * result.width * 3);
        for (uint32_t y = 0; y < result.height; y++)
        {
            std::memcpy(dst, src, result.width * 3);
            src += pitch;
            dst -= result.width * 3;
        }
        return result;
    }

    static TextureHandle readTextureFile(OpenREShell& shell, std::string_view path, uint32_t width, uint32_t height)
    {
        std::vector<std::string_view> extensions = { ".adt", ".bmp" };
        auto streamResult = shell.getStream(path, extensions);
        if (!streamResult.found)
            return {};

        auto& stream = streamResult.stream;
        stream->seek(0, SEEK_END);
        auto length = static_cast<size_t>(stream->tell());
        stream->seek(0, SEEK_SET);
        std::vector<uint8_t> buffer(length);
        stream->read(buffer.data(), length);

        TextureBuffer textureBuffer;
        if (streamResult.extensionIndex == 0)
        {
            textureBuffer = decodeAdt(std::move(buffer), width, height);
        }
        else
        {
            textureBuffer = decodeBmp(std::move(buffer));
        }

        return shell.loadTexture(textureBuffer.width, textureBuffer.height, textureBuffer.pixels.data());
    }

    static bool initialized;
    static TextureHandle title;
    static float fade;
    static int timer;
    static int state;

    static void tick(OpenREShell& shell)
    {
        const int fadeTime = 60;
        const int waitTime = 120;

        if (!initialized)
        {
            initialized = true;
            title = readTextureFile(shell, "texture/splashbg", 320, 240);
            fade = 1;
            timer = 30;
            state = -1;
        }

        if (state == -1)
        {
            timer--;
            if (timer <= 0)
            {
                timer = fadeTime;
                state++;
            }
        }
        else if (state == 0)
        {
            timer--;
            fade -= (1.0f / fadeTime);
            if (timer <= 0)
            {
                timer = waitTime;
                state++;
            }
        }
        else if (state == 1)
        {
            timer--;
            if (timer <= 0)
            {
                timer = fadeTime;
                state++;
            }
        }
        else if (state == 2)
        {
            timer--;
            fade += (1.0f / fadeTime);
            if (timer <= 0)
            {
                state++;
            }
        }

        pushQuad(shell, title, { 1, 1, 1, 1 }, 0, 0, 0, 320, 240);
        pushQuad(shell, 0, { 0, 0, 0, fade }, 0, 0, 0, 320, 240);
    }

    void openreMain(int argc, const char** argv)
    {
        auto shell = std::make_unique<SDL2OpenREShell>();
        auto shellPtr = shell.get();
        shell->setUpdate([shellPtr]() { tick(*shellPtr); });
        shell->run();
    }
}
