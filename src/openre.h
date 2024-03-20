#pragma once

#include "re2.h"
#include <cstdint>

namespace openre
{
    enum class FlagGroup : uint8_t
    {
        System,
        Status,
        Stop,
        Scenario,
        Common,
        Room,
        Enemy,
        Enemy2,
        Item,
        Map,
        Use,
        Message,
        RoomEnemy,
        Pbf00,
        PBF01,
        PBF02,
        PBF03,
        PBF04,
        PBF05,
        PBF06,
        PBF07,
        PBF08,
        PBF09,
        PBF0A,
        PBF0B,
        PBF0C,
        PBF0D,
        PBF0E,
        PBF0F,
        Zapping,
        RbjSet,
        Key,
        MapC,
        MapI,
        Item2,
    };

    // 00 | 00 | 80000000
    // 01 | 01 | 40000000
    // 02 | 02 | 20000000
    // 03 | 03 | 10000000
    // 04 | 04 | 08000000
    // 05 | 05 | 04000000
    // 06 | 06 | 02000000
    // 07 | 07 | 01000000
    // 08 | 08 | 00800000
    // 09 | 09 | 00400000
    // 10 | 0A | 00200000
    // 11 | 0B | 00100000
    // 12 | 0C | 00080000
    // 13 | 0D | 00040000
    // 14 | 0E | 00020000
    // 15 | 0F | 00010000
    // 16 | 10 | 00008000
    // 17 | 11 | 00004000
    // 18 | 12 | 00002000
    // 19 | 13 | 00001000
    // 20 | 14 | 00000800
    // 21 | 15 | 00000400
    // 22 | 16 | 00000200
    // 23 | 17 | 00000100
    // 24 | 18 | 00000080
    // 25 | 19 | 00000040
    // 26 | 1A | 00000020
    // 27 | 1B | 00000010
    // 28 | 1C | 00000008
    // 29 | 1D | 00000004
    // 30 | 1E | 00000002
    // 31 | 1F | 00000001

    enum
    {
        FG_SYSTEM_DOOR_TRANSITION = 6,
        FG_SYSTEM_7 = 7,
        FG_SYSTEM_15 = 15,
        FG_SYSTEM_BGM_DISABLED = 18,
        FG_SYSTEM_ARRANGE = 25,
        FG_SYSTEM_28 = 28,
    };

    enum
    {
        FG_STATUS_PLAYER = 0,
        FG_STATUS_SCENARIO = 1,
        FG_STATUS_PARTNER = 3,
        FG_STATUS_EASY = 5,
        FG_STATUS_BONUS = 6,
        FG_STATUS_GAMEPLAY = 8,
        FG_STATUS_9 = 9,
        FG_STATUS_10 = 10,
        FG_STATUS_11 = 11,
        FG_STATUS_12 = 12,
        FG_STATUS_13 = 13,
        FG_STATUS_14 = 14,
        FG_STATUS_SCREEN = 16,
        // Pushing object ???
        FG_STATUS_18 = 18,
        FG_STATUS_20 = 20,
        FG_STATUS_21 = 21,
        FG_STATUS_23 = 23,
        FG_STATUS_24 = 24,
        FG_STATUS_25 = 25,
        FG_STATUS_26 = 26,
        FG_STATUS_CUTSCENE = 27,
        FG_STATUS_28 = 28,
        FG_STATUS_29 = 29,
        FG_STATUS_30 = 30,
        FG_STATUS_31 = 31,
    };

    enum
    {
        FG_STOP_06 = 6,
        FG_STOP_DISABLE_INPUT = 7,
    };

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
    bool check_flag(FlagGroup group, uint32_t index);
    void set_flag(FlagGroup group, uint32_t index, bool value);
}
