#pragma once

#include "re2.h"
#include <cstdint>

namespace openre
{
    extern GameTable& gGameTable;
    extern uint32_t& gGameFlags;
    extern uint32_t& gErrorCode;
    extern uint32_t& _memTop;
    extern Unknown68A204*& dword_68A204;
    extern PlayerEntity& gPlayerEntity;
    extern uint16_t& gPoisonStatus;
    extern uint8_t& gPoisonTimer;

    void task_sleep(int frames);
    void task_exit();
    bool fade_status(int no);
    void fade_set(short a0, short add, char mask, char pri);
    void fade_adjust(int no, short kido, int rgb, PsxRect* rect);
    void mess_print(int x, int y, const uint8_t* str, short a4);
    uint8_t rnd();
    void set_view(const Vec32p& pVp, const Vec32p& pVr);
    void bg_set_mode(int mode, int rgb);
    void mapping_tmd(int a1, void* pTmd, int page, int clut);
    void set_geom_screen(int prj);
}
