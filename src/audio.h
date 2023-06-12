#pragma once

#include <cstdint>

namespace openre::audio
{
    void bgm_set_entry(uint32_t arg0);
    void bgm_set_control(uint32_t arg0);
    void snd_se_on(int a0, int a1);
    void bgm_init_hooks();
}
