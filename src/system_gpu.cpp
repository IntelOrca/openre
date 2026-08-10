#include "system_gpu.h"

#include "gfx_backend.h"
#include "logger.h"
#include "system_window.h"

#include <SDL3/SDL.h>

#include <cstdio>

namespace openre::system::gpu
{
    namespace
    {
        SDL_GPUDevice* g_device = nullptr;
        SDL_Window* g_window = nullptr;

        // The guest framebuffer: the offscreen render target the scene renders
        // into (owned here, presented by the GPU backend). Re-created by
        // create_guest_framebuffer when the requested size changes.
        SDL_GPUTexture* g_guestFramebuffer = nullptr;
        int32_t g_fbWidth = 0;
        int32_t g_fbHeight = 0;
    }

    bool init()
    {
        if (g_device != nullptr)
            return true;

        g_window = static_cast<SDL_Window*>(system::window::get_window());
        if (g_window == nullptr)
        {
            logging::logError("[system:gpu] init failed: no SDL window available");
            return false;
        }

        g_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV, false, nullptr);
        if (g_device == nullptr)
        {
            logging::logError("[system:gpu] SDL_CreateGPUDevice failed: {}", SDL_GetError());
            return false;
        }
        logging::logInfo("[system:gpu] device created (driver={})", SDL_GetGPUDeviceDriver(g_device));

        if (!SDL_ClaimWindowForGPUDevice(g_device, g_window))
        {
            logging::logError("[system:gpu] SDL_ClaimWindowForGPUDevice failed: {}", SDL_GetError());
            SDL_DestroyGPUDevice(g_device);
            g_device = nullptr;
            return false;
        }

        const auto format = SDL_GetGPUSwapchainTextureFormat(g_device, g_window);
        logging::logInfo("[system:gpu] window claimed, swapchain format={}", static_cast<int>(format));

        gfx::backend_gpu()->attach_device(g_device, g_window);
        return true;
    }

    void* create_guest_framebuffer(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return nullptr;
        if (!init())
            return nullptr;

        if (g_guestFramebuffer != nullptr && g_fbWidth == width && g_fbHeight == height)
            return g_guestFramebuffer;

        // present() fences one frame deep, so the previous frame may still
        // reference the old framebuffer; drain the GPU before replacing it.
        SDL_WaitForGPUIdle(g_device);
        if (g_guestFramebuffer != nullptr)
        {
            SDL_ReleaseGPUTexture(g_device, g_guestFramebuffer);
            g_guestFramebuffer = nullptr;
        }

        SDL_GPUTextureCreateInfo info = {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<Uint32>(width);
        info.height = static_cast<Uint32>(height);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        g_guestFramebuffer = SDL_CreateGPUTexture(g_device, &info);
        if (g_guestFramebuffer == nullptr)
        {
            logging::logError("[system:gpu] guest framebuffer creation failed ({}x{}): {}", width, height, SDL_GetError());
            g_fbWidth = 0;
            g_fbHeight = 0;
            return nullptr;
        }
        g_fbWidth = width;
        g_fbHeight = height;
        logging::logInfo("[system:gpu] guest framebuffer created ({}x{})", width, height);

        gfx::backend_gpu()->set_guest_framebuffer(g_guestFramebuffer, width, height);
        return g_guestFramebuffer;
    }

    bool is_initialized()
    {
        return g_device != nullptr;
    }

    void present()
    {
        if (!init())
            return;
        gfx::backend_gpu()->present();
    }

    void set_movie_frame(const void* pixels, int width, int height, int pitch)
    {
        if (!init())
            return;
        gfx::backend_gpu()->set_movie_frame(pixels, width, height, pitch);
    }

    void clear_movie_frame()
    {
        if (g_device == nullptr)
            return;
        gfx::backend_gpu()->set_movie_frame(nullptr, 0, 0, 0);
    }

    void shutdown()
    {
        // The backend holds the surface layer and per-frame replay resources;
        // release them (with an idle wait) before tearing the device down.
        gfx::backend_gpu()->shutdown();

        if (g_device != nullptr)
        {
            if (g_guestFramebuffer != nullptr)
            {
                SDL_ReleaseGPUTexture(g_device, g_guestFramebuffer);
                g_guestFramebuffer = nullptr;
            }
            g_fbWidth = 0;
            g_fbHeight = 0;
            if (g_window != nullptr)
                SDL_ReleaseWindowFromGPUDevice(g_device, g_window);
            SDL_DestroyGPUDevice(g_device);
            g_device = nullptr;
        }
        g_window = nullptr;
        logging::logInfo("[system:gpu] shutdown (framebuffer released, window released, device destroyed)");
    }
}
