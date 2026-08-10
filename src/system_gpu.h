#pragma once

#include <cstdint>

namespace openre::system::gpu
{
    // The GPU subsystem owner: the SDL_GPU device, the window's swapchain and
    // the guest framebuffer (the offscreen render target the scene is rendered
    // into before being letterboxed into the swapchain).
    //
    // Device creation is lazy: init() is called from create_guest_framebuffer
    // and present(), never from marni::init. The window is claimed exactly once
    // (at first init()).

    // Creates the SDL_GPU device and claims the game window on first use.
    // Idempotent; safe to call repeatedly. Returns false on failure.
    bool init();

    // Ensures the guest framebuffer exists at `width` x `height` (the render
    // resolution), re-creating it when the size changes. Returns the underlying
    // SDL_GPUTexture* as void* (SDL stays out of this header), or nullptr on
    // failure. The framebuffer is released by shutdown().
    void* create_guest_framebuffer(int width, int height);

    // True once the GPU device exists (and the window is claimed).
    bool is_initialized();

    // Acquires the swapchain texture and presents the guest framebuffer (the
    // GPU backend replays the frame's draws into it, then letterboxes it into
    // the swapchain). Creates the device on first use.
    void present();

    // Releases the guest framebuffer, unclaims the window and destroys the
    // device. Idempotent.
    void shutdown();
}
