#pragma once

#include <cstdint>

struct Vec32;

namespace openre::audio
{
    void bgm_set_entry(uint32_t arg0);
    void bgm_set_control(uint32_t arg0);
    void snd_se_on(int a0);
    void snd_se_on(int a0, const Vec32& a1);
    void snd_load_core(uint8_t a0, uint8_t a1);
    void snd_sys_init();
    void snd_sys_stereo();
    void snd_sys_init_sub2();
    void snd_bgm_ck();
    void snd_room_load();
    void snd_sys_init2();
    void snd_bgm_set();
    void snd_bgm_play_ck();
    void snd_load_enemy();

    int ss_close();
    int ss_unload_group(int type);
    int ss_load_banks(int type, int id, int bank, int player);
    void bgm_init_hooks();
}
