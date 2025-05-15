#include "shell.h"
#include "font.h"
#include "gfx.h"
#include "logger.h"
#include "movie.h"
#include "relua.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

namespace fs = std::filesystem;

using namespace openre::graphics;
using namespace openre::input;
using namespace openre::logging;
using namespace openre::movie;

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

        int64_t seek(int64_t offset, int origin) override
        {
            return SDL_SeekIO(this->baseStream, offset, (SDL_IOWhence)origin);
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

    struct Font
    {
        FontHandle handle{};
        GLuint textureGlHandle{};
        FontData fontData;
    };

    struct MovieWrapper
    {
        MovieHandle handle{};
        std::unique_ptr<MoviePlayer> movie;
        GLuint textureGlHandle{};
        SDL_AudioStream* audioStream{};
        MovieFrame nextFrame;
    };

    enum class InputBindingKind
    {
        Keyboard,
        Gamepad
    };

    struct InputBinding
    {
        InputBindingKind kind;
        int code;
    };

    class SDL2OpenREShell : public OpenREShell
    {
    private:
        std::unique_ptr<Logger> logger;

        SDL_Window* window{};

        SDL_GLContext glContext{};
        GLuint renderFrameBufferHandle{};
        GLuint renderFrameBufferTexture{};
        std::vector<GLTexture> textures;
        std::vector<OpenREPrim> primitives;

        std::function<void()> updateCallback;

        uint32_t windowWidth = 320 * 2;
        uint32_t windowHeight = 240 * 2;
        uint32_t renderWidth = 320 * 2;
        uint32_t renderHeight = 240 * 2;
        // uint32_t windowWidth = 1920;
        // uint32_t windowHeight = 1080;
        // uint32_t renderWidth = 1920;
        // uint32_t renderHeight = 1080;

        std::vector<fs::path> basePaths;

        PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers{};
        PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer{};
        PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D{};
        PFNGLCLEARTEXIMAGEPROC glClearTexImage{};

        std::vector<Font> fonts;
        std::vector<MovieWrapper> movies;

        SDL_AudioDeviceID outputAudioDevice{};

        std::vector<std::vector<InputBinding>> inputBindings;
        std::vector<SDL_Gamepad*> gamePads;
        uint64_t gamePadsLastCheck{};
        InputState inputState{};

    public:
        SDL2OpenREShell()
        {
#if DEBUG
            logger = createConsoleLogger(LogVerbosity::debug);
#else
            logger = createConsoleLogger(LogVerbosity::verbose);
#endif
        }

        ~SDL2OpenREShell() {}

        Logger& getLogger() override
        {
            return *logger;
        }

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

        Size getRenderSize() override
        {
            return { this->renderWidth, this->renderHeight };
        }

        TextureHandle loadTexture(const TextureBuffer& textureBuffer) override
        {
            auto nextHandle = textures.size() + 1;
            this->logger->log(
                LogVerbosity::verbose, "Load texture %d (%dx%d)", nextHandle, textureBuffer.width, textureBuffer.height);

            auto& texture = textures.emplace_back();
            texture.handle = allocateTexture(textureBuffer);
            texture.width = textureBuffer.width;
            texture.height = textureBuffer.height;
            return nextHandle;
        }

        FontHandle loadFont(const openre::graphics::TextureBuffer& textureBuffer, const FontData& fontData) override
        {
            auto nextHandle = this->fonts.size() + 1;
            this->logger->log(LogVerbosity::verbose, "Load font %d", nextHandle);

            auto& font = this->fonts.emplace_back();
            font.handle = nextHandle;
            font.textureGlHandle = allocateTexture(textureBuffer);
            font.fontData = fontData;
            return nextHandle;
        }

        const FontData* getFontData(FontHandle handle) override
        {
            if (this->fonts.size() >= handle)
            {
                auto& font = this->fonts[handle - 1];
                return &font.fontData;
            }
            return nullptr;
        }

        void pushPrimitive(const OpenREPrim& prim) override
        {
            primitives.push_back(prim);
        }

        MovieHandle loadMovie(std::unique_ptr<MoviePlayer> movie) override
        {
            auto nextHandle = this->movies.size() + 1;
            this->logger->log(LogVerbosity::verbose, "Load movie %d", nextHandle);

            auto& movieWrapper = this->movies.emplace_back();
            movieWrapper.handle = nextHandle;
            movieWrapper.movie = std::move(movie);
            updateMovie(movieWrapper);
            return nextHandle;
        }

        MoviePlayer* getMovie(MovieHandle handle) override
        {
            if (handle > 0 && handle <= movies.size())
            {
                return movies[handle - 1].movie.get();
            }
            return nullptr;
        }

        void setUpdate(std::function<void()> callback) override
        {
            updateCallback = callback;
        }

        void run() override
        {
            init();

            auto tickRate = 60;
            auto msPerTick = 1000 / tickRate;
            auto lastTick = SDL_GetTicks();
            auto done = false;
            while (!done)
            {
                auto now = SDL_GetTicks();
                auto duration = now - lastTick;
                if (duration < msPerTick)
                {
                    auto remaining = msPerTick - duration;
                    if (duration >= 2)
                    {
                        SDL_Delay(static_cast<uint32_t>(duration - 1));
                    }
                    continue;
                }

                lastTick = now;

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

        InputState& getInputState() override
        {
            return inputState;
        }

    private:
        void init()
        {
            basePaths = { "M:\\git\\openre\\games\\re2hd", "M:\\git\\openre\\games\\re2" };

            this->logger->log(LogVerbosity::info, "Initialize SDL");
            SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD);

            this->logger->log(LogVerbosity::info, "Create window (%dx%d)", windowWidth, windowHeight);
            this->window = SDL_CreateWindow("OpenRE", windowWidth, windowHeight, SDL_WINDOW_OPENGL);

            this->logger->log(LogVerbosity::info, "Create OpenGL context");
            this->glContext = SDL_GL_CreateContext(window);
            SDL_GL_MakeCurrent(window, glContext);
            SDL_GL_SetSwapInterval(1);

            initGl();
            glViewport(0, 0, windowWidth, windowHeight);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, renderWidth, renderHeight, 0, 1, -1);
            glEnable(GL_CULL_FACE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_DEPTH_TEST);
            glClearColor(0, 0, 0, 0);

            createRenderBuffer();

            SDL_AudioSpec spec;
            spec.channels = 2;
            spec.format = SDL_AUDIO_S16;
            spec.freq = 44100;
            outputAudioDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
        }

        void initGl()
        {
            glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
            glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
            glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
            glClearTexImage = (PFNGLCLEARTEXIMAGEPROC)SDL_GL_GetProcAddress("glClearTexImage");
        }

        GLuint allocateTexture(const TextureBuffer& textureBuffer)
        {
            GLuint handle{};
            glGenTextures(1, &handle);
            glBindTexture(GL_TEXTURE_2D, handle);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            auto fmt = textureBuffer.bpp == 32 ? GL_RGBA : GL_RGB;
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                fmt,
                textureBuffer.width,
                textureBuffer.height,
                0,
                fmt,
                GL_UNSIGNED_BYTE,
                textureBuffer.pixels.data());
            return handle;
        }

        void createRenderBuffer()
        {
            this->logger->log(LogVerbosity::info, "Create render buffer");

            renderFrameBufferHandle = 0;
            glGenFramebuffers(1, &renderFrameBufferHandle);
            if (renderFrameBufferHandle == 0)
            {
                this->logger->log(LogVerbosity::error, "Failed to create render buffer");
                return;
            }

            renderFrameBufferTexture = 0;
            glGenTextures(1, &renderFrameBufferTexture);
            if (renderFrameBufferTexture == 0)
            {
                this->logger->log(LogVerbosity::error, "Failed to create texture for render buffer");
                return;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, renderFrameBufferHandle);
            glBindTexture(GL_TEXTURE_2D, renderFrameBufferTexture);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, this->renderWidth, this->renderHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, renderFrameBufferTexture, 0);
        }

        void update()
        {
            updateInputState();
            updateMovies();
            updateCallback();
            updateInputEffects();
        }

        void updateMovies()
        {
            for (auto& movieWrapper : movies)
            {
                updateMovie(movieWrapper);
            }
        }

        void updateMovie(MovieWrapper& movieWrapper)
        {
            if (movieWrapper.movie->getState() != MovieState::playing)
            {
                if (movieWrapper.textureGlHandle != 0)
                {
                    this->logger->log(LogVerbosity::verbose, "Destroy texture for movie %d", movieWrapper.handle);
                    glDeleteTextures(1, &movieWrapper.textureGlHandle);
                    movieWrapper.textureGlHandle = 0;
                }
                if (movieWrapper.audioStream != nullptr)
                {
                    this->logger->log(LogVerbosity::verbose, "Destroy audio stream for movie %d", movieWrapper.handle);
                    SDL_DestroyAudioStream(movieWrapper.audioStream);
                    movieWrapper.audioStream = nullptr;
                }
                movieWrapper.nextFrame = {};
                return;
            }

            auto currentTimeCode = (int64_t)(movieWrapper.movie->getPosition() * 1000);
            if (movieWrapper.nextFrame.endTime <= currentTimeCode)
            {
                do
                {
                    movieWrapper.nextFrame = movieWrapper.movie->dequeueVideoFrame();
                } while (movieWrapper.nextFrame.endTime != 0 && movieWrapper.nextFrame.endTime <= currentTimeCode);
            }

            auto videoFormat = movieWrapper.movie->getVideoFormat();
            if (movieWrapper.textureGlHandle == 0)
            {
                this->logger->log(LogVerbosity::verbose, "Create texture for movie %d", movieWrapper.handle);
                glGenTextures(1, &movieWrapper.textureGlHandle);
                if (movieWrapper.textureGlHandle == 0)
                {
                    this->logger->log(LogVerbosity::error, "Failed to create texture for movie %d", movieWrapper.handle);
                }
                else
                {
                    glBindTexture(GL_TEXTURE_2D, movieWrapper.textureGlHandle);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
            }
            if (movieWrapper.textureGlHandle != 0)
            {
                glBindTexture(GL_TEXTURE_2D, movieWrapper.textureGlHandle);
                if (movieWrapper.nextFrame.endTime == 0)
                {
                    GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    glClearTexImage(movieWrapper.textureGlHandle, 0, GL_RGB, GL_UNSIGNED_BYTE, clearColor);
                }
                else
                {
                    glTexImage2D(
                        GL_TEXTURE_2D,
                        0,
                        GL_RGB,
                        videoFormat.resolution.width,
                        videoFormat.resolution.height,
                        0,
                        GL_RGB,
                        GL_UNSIGNED_BYTE,
                        movieWrapper.nextFrame.data.data());
                }
            }

            auto audioFormat = movieWrapper.movie->getAudioFormat();
            auto audioFrame = movieWrapper.movie->dequeueAudioFrame();
            if (movieWrapper.audioStream == nullptr)
            {
                this->logger->log(LogVerbosity::verbose, "Create audio stream for movie %d", movieWrapper.handle);
                SDL_AudioSpec spec;
                spec.channels = audioFormat.channels;
                spec.format = SDL_AUDIO_S16;
                spec.freq = audioFormat.sampleRate;
                movieWrapper.audioStream = SDL_CreateAudioStream(&spec, nullptr);
                if (movieWrapper.audioStream == nullptr)
                {
                    this->logger->log(LogVerbosity::error, "Failed to create audio stream for movie %d", movieWrapper.handle);
                }
                else
                {
                    SDL_BindAudioStream(this->outputAudioDevice, movieWrapper.audioStream);
                }
            }
            if (movieWrapper.audioStream != nullptr)
            {
                SDL_PutAudioStreamData(movieWrapper.audioStream, audioFrame.data.data(), audioFrame.data.size());
            }
        }

        void render()
        {
            std::stable_sort(primitives.begin(), primitives.end(), [](const OpenREPrim& a, const OpenREPrim& b) {
                return a.vertices[0].z < b.vertices[0].z;
            });

            glBindFramebuffer(GL_FRAMEBUFFER, renderFrameBufferHandle);
            glViewport(0, 0, this->renderWidth, this->renderHeight);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, this->renderWidth, this->renderHeight, 0, 1, -1);
            for (auto& p : primitives)
            {
                if (p.kind == OpenREPrimKind::TextureQuad || p.kind == OpenREPrimKind::FontQuad
                    || p.kind == OpenREPrimKind::MovieQuad)
                {
                    if (p.color.a == 0)
                        continue;

                    auto textureEnabled = false;
                    if (p.kind == OpenREPrimKind::TextureQuad)
                    {
                        if (p.texture != 0 && p.texture <= textures.size())
                        {
                            glEnable(GL_TEXTURE_2D);
                            glBindTexture(GL_TEXTURE_2D, textures[p.texture - 1].handle);
                            textureEnabled = true;
                        }
                        else
                        {
                            glDisable(GL_TEXTURE_2D);
                        }
                    }
                    else if (p.kind == OpenREPrimKind::FontQuad)
                    {
                        if (p.font != 0 && p.font <= fonts.size())
                        {
                            glEnable(GL_TEXTURE_2D);
                            glBindTexture(GL_TEXTURE_2D, fonts[p.font - 1].textureGlHandle);
                            textureEnabled = true;
                        }
                        else
                        {
                            glDisable(GL_TEXTURE_2D);
                        }
                    }
                    else if (p.kind == OpenREPrimKind::MovieQuad)
                    {
                        if (p.movie != 0 && p.movie <= movies.size())
                        {
                            auto& movieWrapper = movies[p.movie - 1];
                            if (movieWrapper.textureGlHandle != 0)
                            {
                                glEnable(GL_TEXTURE_2D);
                                glBindTexture(GL_TEXTURE_2D, movieWrapper.textureGlHandle);
                                textureEnabled = true;
                            }
                        }
                        else
                        {
                            glDisable(GL_TEXTURE_2D);
                        }
                    }

                    if (textureEnabled || p.color.a != 1)
                    {
                        glEnable(GL_BLEND);
                    }
                    else
                    {
                        glDisable(GL_BLEND);
                    }

                    glBegin(GL_QUADS);
                    glColor4f(p.color.r, p.color.g, p.color.b, p.color.a);
                    for (auto i = 0; i < 4; i++)
                    {
                        if (textureEnabled)
                        {
                            glTexCoord2f(p.vertices[i].s, p.vertices[i].t);
                        }
                        glVertex3f(p.vertices[i].x, p.vertices[i].y, p.vertices[i].z);
                    }
                    glEnd();
                }
            }
            primitives.clear();
            renderToBackBuffer();
        }

        void renderToBackBuffer()
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, this->windowWidth, this->windowHeight);
            glBindTexture(GL_TEXTURE_2D, renderFrameBufferTexture);
            glDisable(GL_BLEND);
            glEnable(GL_TEXTURE_2D);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, this->windowWidth, this->windowHeight, 0, 1, -1);
            glBegin(GL_QUADS);
            glColor3d(1, 1, 1);
            glTexCoord2f(0.0f, 1.0f);
            glVertex2f(0, 0);
            glTexCoord2f(0.0f, 0.0f);
            glVertex2f(0, (float)this->windowHeight);
            glTexCoord2f(1.0f, 0.0f);
            glVertex2f((float)this->windowWidth, (float)this->windowHeight);
            glTexCoord2f(1.0f, 1.0f);
            glVertex2f((float)this->windowWidth, 0);
            glEnd();
            SDL_GL_SwapWindow(window);
        }

        void initGamePadBindings()
        {
            addBinding(InputCommand::up, InputBindingKind::Keyboard, SDL_SCANCODE_UP);
            addBinding(InputCommand::up, InputBindingKind::Keyboard, SDL_SCANCODE_W);
            addBinding(InputCommand::down, InputBindingKind::Keyboard, SDL_SCANCODE_DOWN);
            addBinding(InputCommand::down, InputBindingKind::Keyboard, SDL_SCANCODE_S);
            addBinding(InputCommand::left, InputBindingKind::Keyboard, SDL_SCANCODE_LEFT);
            addBinding(InputCommand::left, InputBindingKind::Keyboard, SDL_SCANCODE_A);
            addBinding(InputCommand::right, InputBindingKind::Keyboard, SDL_SCANCODE_RIGHT);
            addBinding(InputCommand::right, InputBindingKind::Keyboard, SDL_SCANCODE_D);
            addBinding(InputCommand::menuCancel, InputBindingKind::Keyboard, SDL_SCANCODE_ESCAPE);
            addBinding(InputCommand::menuApply, InputBindingKind::Keyboard, SDL_SCANCODE_RETURN);
            addBinding(InputCommand::menuStart, InputBindingKind::Keyboard, SDL_SCANCODE_SPACE);

            addBinding(InputCommand::up, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
            addBinding(InputCommand::left, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            addBinding(InputCommand::right, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            addBinding(InputCommand::down, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            addBinding(InputCommand::menuStart, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
            addBinding(InputCommand::menuStart, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_START);
            addBinding(InputCommand::menuApply, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
            addBinding(InputCommand::menuCancel, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_EAST);
            addBinding(InputCommand::menuCancel, InputBindingKind::Gamepad, SDL_GAMEPAD_BUTTON_BACK);
        }

        void addBinding(InputCommand command, InputBindingKind kind, int code)
        {
            auto index = static_cast<size_t>(command);
            inputBindings.resize(std::max(inputBindings.size(), index + 1));

            auto& commandBinding = inputBindings[index];
            commandBinding.push_back({ kind, code });
        }

        void updateGamePadList()
        {
            int count;
            auto gamePadIds = SDL_GetGamepads(&count);
            for (auto i = 0; i < count; i++)
            {
                auto gamePad = SDL_OpenGamepad(gamePadIds[i]);
                if (gamePad != nullptr)
                {
                    gamePads.push_back(gamePad);
                }
            }
            SDL_free(gamePadIds);
        }

        void updateInputState()
        {
            if (inputBindings.size() == 0)
            {
                initGamePadBindings();
            }

            auto now = SDL_GetTicks();
            if (gamePadsLastCheck == 0 || now - gamePadsLastCheck > 15000)
            {
                gamePadsLastCheck = now;
                updateGamePadList();
            }

            for (size_t i = 0; i < inputBindings.size(); i++)
            {
                auto oldState = inputState.commandsDown[i];
                inputState.commandsDown[i] = false;
                inputState.commandsPressed[i] = false;

                const auto& commandBinding = inputBindings[i];
                for (const auto& b : commandBinding)
                {
                    if (checkBinding(b))
                    {
                        inputState.commandsDown[i] = true;
                        if (!oldState)
                            inputState.commandsPressed[i] = true;
                        break;
                    }
                }
            }
        }

        bool checkBinding(InputBinding b)
        {
            if (b.kind == InputBindingKind::Keyboard)
            {
                int numKeys;
                auto keys = SDL_GetKeyboardState(&numKeys);
                if (numKeys > b.code)
                {
                    if (keys[b.code])
                    {
                        return true;
                    }
                }
            }
            else if (b.kind == InputBindingKind::Gamepad)
            {
                for (const auto& gamePad : gamePads)
                {
                    if (SDL_GetGamepadButton(gamePad, (SDL_GamepadButton)b.code))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        template<typename T> static inline T denormalize(float value, T max)
        {
            return static_cast<T>(std::clamp<float>((value * max) / 1.0f, 0, max));
        }

        void updateInputEffects()
        {
            auto& inputState = this->inputState;
            auto red = denormalize<uint8_t>(inputState.led.r, 0xFF);
            auto green = denormalize<uint8_t>(inputState.led.g, 0xFF);
            auto blue = denormalize<uint8_t>(inputState.led.b, 0xFF);
            auto low = denormalize<uint16_t>(inputState.rumble.low, 0xFFFF);
            auto high = denormalize<uint16_t>(inputState.rumble.high, 0xFFFF);
            for (const auto& gamePad : gamePads)
            {
                SDL_SetGamepadLED(gamePad, red, green, blue);
                SDL_RumbleGamepad(gamePad, low, high, 1024);
            }
        }
    };

    std::unique_ptr<OpenREShell> createShell()
    {
        return std::make_unique<SDL2OpenREShell>();
    }
}
