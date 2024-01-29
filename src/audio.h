#pragma once

#include <cstdint>

struct Vec32;

namespace openre::audio
{
    void bgm_set_entry(uint32_t arg0);
    void bgm_set_control(uint32_t arg0);
    void snd_se_on(int a0);
    void snd_se_on(int a0, const Vec32& a1);
    void bgm_init_hooks();
}
