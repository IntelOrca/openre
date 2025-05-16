#include "shell.h"
#include "gfx.h"
#include "logger.h"
#include "movie.h"
#include "relua.h"
#include "resmgr.h"

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
    class SDL2OpenREShell;

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

    class TextureResource : public Resource
    {
    public:
        SDL2OpenREShell& shell;
        ResourceHandle handle{};
        Size size;
        GLuint glHandle{};

        TextureResource(SDL2OpenREShell& shell)
            : shell(shell)
        {
        }

        const char* getName() const override
        {
            return "texture";
        }
    };

    class MovieResource : public Resource
    {
    public:
        SDL2OpenREShell& shell;
        ResourceHandle handle{};
        std::unique_ptr<MoviePlayer> movie;
        GLuint textureGlHandle{};
        SDL_AudioStream* audioStream{};
        MovieFrame nextFrame;

        MovieResource(SDL2OpenREShell& shell)
            : shell(shell)
            , movie(createMoviePlayer())
        {
        }

        ~MovieResource() override
        {
            releaseResources();
        }

        const char* getName() const override
        {
            return "movie";
        }

        void releaseResources();
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
        std::unique_ptr<ResourceManager> resourceManager;

        SDL_Window* window{};

        SDL_GLContext glContext{};
        GLuint renderFrameBufferHandle{};
        GLuint renderFrameBufferTexture{};
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

        SDL_AudioDeviceID outputAudioDevice{};

        std::vector<std::vector<InputBinding>> inputBindings;
        std::vector<SDL_Gamepad*> gamePads;
        uint64_t gamePadsLastCheck{};
        InputState inputState{};

    public:
        SDL2OpenREShell()
        {
#if DEBUG
            this->logger = createConsoleLogger(LogVerbosity::debug);
#else
            this->logger = createConsoleLogger(LogVerbosity::verbose);
#endif
            this->resourceManager = std::make_unique<ResourceManager>(*this->logger);
        }

        ~SDL2OpenREShell() {}

        Logger& getLogger() override
        {
            return *logger;
        }

        ResourceManager& getResourceManager() override
        {
            return *resourceManager;
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

        ResourceCookie loadTexture(std::string_view path, uint32_t width, uint32_t height) override
        {
            auto& resourceManager = *this->resourceManager;
            auto cookie = resourceManager.addRef<TextureResource>(path);
            if (cookie)
                return cookie;

            cookie = resourceManager.addFirstRef(path, std::make_unique<TextureResource>(*this));

            auto textureResource = resourceManager.fromCookie<TextureResource>(cookie);

            auto textureBuffer = shellextensions::loadTextureBuffer(*this, path, width, height);
            textureResource->glHandle = allocateTexture(textureBuffer);
            textureResource->size.width = textureBuffer.width;
            textureResource->size.height = textureBuffer.height;

            return cookie;
        }

        void pushPrimitive(const OpenREPrim& prim) override
        {
            primitives.push_back(prim);
        }

        ResourceCookie loadMovie(std::string_view path) override
        {
            auto& resourceManager = *this->resourceManager;
            auto cookie = resourceManager.addRef<MovieResource>(path);
            if (cookie)
                return cookie;

            auto stream = getStream(path, { ".mp4", ".mpg" });
            if (!stream.found)
                return 0;

            auto movieResource = std::make_unique<MovieResource>(*this);
            movieResource->movie->open(std::move(stream.stream));
            auto handle = resourceManager.add(path, std::move(movieResource));

            auto movieResource2 = resourceManager.fromHandle<MovieResource>(handle);
            movieResource2->handle = handle;
            cookie = resourceManager.addRef(handle);

            updateMovie(handle);
            return cookie;
        }

        MoviePlayer* getMovie(ResourceCookie cookie) override
        {
            auto resource = resourceManager->fromCookie<MovieResource>(cookie);
            return resource == nullptr ? nullptr : resource->movie.get();
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
            auto& resourceManager = *this->resourceManager;
            auto handle = resourceManager.getFirst<MovieResource>();
            while (handle != 0)
            {
                updateMovie(handle);
                handle = resourceManager.getNext(handle);
            }
        }

        void updateMovie(ResourceHandle handle)
        {
            auto movieResource = this->resourceManager->fromHandle<MovieResource>(handle);
            if (movieResource == nullptr)
                return;

            if (movieResource->movie->getState() != MovieState::playing)
            {
                movieResource->releaseResources();
                return;
            }

            auto currentTimeCode = (int64_t)(movieResource->movie->getPosition() * 1000);
            if (movieResource->nextFrame.endTime <= currentTimeCode)
            {
                do
                {
                    movieResource->nextFrame = movieResource->movie->dequeueVideoFrame();
                } while (movieResource->nextFrame.endTime != 0 && movieResource->nextFrame.endTime <= currentTimeCode);
            }

            auto videoFormat = movieResource->movie->getVideoFormat();
            if (movieResource->textureGlHandle == 0)
            {
                this->logger->log(LogVerbosity::verbose, "Create texture for movie %d", handle);
                glGenTextures(1, &movieResource->textureGlHandle);
                if (movieResource->textureGlHandle == 0)
                {
                    this->logger->log(LogVerbosity::error, "Failed to create texture for movie %d", handle);
                }
                else
                {
                    glBindTexture(GL_TEXTURE_2D, movieResource->textureGlHandle);
                    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
            }
            if (movieResource->textureGlHandle != 0)
            {
                glBindTexture(GL_TEXTURE_2D, movieResource->textureGlHandle);
                if (movieResource->nextFrame.endTime == 0)
                {
                    GLfloat clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    glClearTexImage(movieResource->textureGlHandle, 0, GL_RGB, GL_UNSIGNED_BYTE, clearColor);
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
                        movieResource->nextFrame.data.data());
                }
            }

            auto audioFormat = movieResource->movie->getAudioFormat();
            auto audioFrame = movieResource->movie->dequeueAudioFrame();
            if (movieResource->audioStream == nullptr)
            {
                this->logger->log(LogVerbosity::verbose, "Create audio stream for movie %d", handle);
                SDL_AudioSpec spec;
                spec.channels = audioFormat.channels;
                spec.format = SDL_AUDIO_S16;
                spec.freq = audioFormat.sampleRate;
                movieResource->audioStream = SDL_CreateAudioStream(&spec, nullptr);
                if (movieResource->audioStream == nullptr)
                {
                    this->logger->log(LogVerbosity::error, "Failed to create audio stream for movie %d", handle);
                }
                else
                {
                    SDL_BindAudioStream(this->outputAudioDevice, movieResource->audioStream);
                }
            }
            if (movieResource->audioStream != nullptr)
            {
                SDL_PutAudioStreamData(movieResource->audioStream, audioFrame.data.data(), audioFrame.data.size());
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
                if (p.kind == OpenREPrimKind::TextureQuad || p.kind == OpenREPrimKind::MovieQuad)
                {
                    if (p.color.a == 0)
                        continue;

                    auto textureEnabled = false;
                    if (p.kind == OpenREPrimKind::TextureQuad)
                    {
                        auto textureResource = this->resourceManager->fromCookie<TextureResource>(p.texture);
                        if (textureResource != nullptr && textureResource->glHandle != 0)
                        {
                            glEnable(GL_TEXTURE_2D);
                            glBindTexture(GL_TEXTURE_2D, textureResource->glHandle);
                            textureEnabled = true;
                        }
                        else
                        {
                            glDisable(GL_TEXTURE_2D);
                        }
                    }
                    else if (p.kind == OpenREPrimKind::MovieQuad)
                    {
                        auto movieResource = this->resourceManager->fromCookie<MovieResource>(p.movie);
                        if (movieResource != nullptr)
                        {
                            if (movieResource->textureGlHandle != 0)
                            {
                                glEnable(GL_TEXTURE_2D);
                                glBindTexture(GL_TEXTURE_2D, movieResource->textureGlHandle);
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

    void MovieResource::releaseResources()
    {
        if (this->textureGlHandle != 0)
        {
            shell.getLogger().log(LogVerbosity::verbose, "Destroy texture for movie %d", this->handle);
            glDeleteTextures(1, &this->textureGlHandle);
            this->textureGlHandle = 0;
        }
        if (this->audioStream != nullptr)
        {
            shell.getLogger().log(LogVerbosity::verbose, "Destroy audio stream for movie %d", this->handle);
            SDL_DestroyAudioStream(this->audioStream);
            this->audioStream = nullptr;
        }
        this->nextFrame = {};
    }

    std::unique_ptr<OpenREShell> createShell()
    {
        return std::make_unique<SDL2OpenREShell>();
    }
}
