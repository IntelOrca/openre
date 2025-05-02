#pragma once

#include "re2.h"
#include <cstdint>

struct Md1;

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
        FG_SYSTEM_0 = 0,
        FG_SYSTEM_2 = 2,
        FG_SYSTEM_1 = 1,
        FG_SYSTEM_4 = 4,
        FG_SYSTEM_DOOR_TRANSITION = 6,
        FG_SYSTEM_EX_BATTLE = 7,
        FG_SYSTEM_8 = 8,
        FG_SYSTEM_10 = 10,
        FG_SYSTEM_12 = 12,
        FG_SYSTEM_14 = 14,
        FG_SYSTEM_15 = 15,
        FG_SYSTEM_DEMO = 18,
        FG_SYSTEM_BGM_DISABLED = 18,
        FG_SYSTEM_19 = 19,
        FG_SYSTEM_20 = 20,
        FG_SYSTEM_22 = 22,
        FG_SYSTEM_ARRANGE = 25,
        FG_SYSTEM_EASY = 26,
        FG_SYSTEM_4TH_SURVIVOR = 28,
        FG_SYSTEM_30 = 30,
        FG_SYSTEM_31 = 31,
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
        FG_STATUS_INTERACT = 10,
        FG_STATUS_11 = 11,
        FG_STATUS_12 = 12,
        FG_STATUS_13 = 13,
        FG_STATUS_14 = 14,
        FG_STATUS_MIRROR = 15,
        FG_STATUS_SCREEN = 16,
        FG_STATUS_PUSH_OBJECT = 18,
        FG_STATUS_20 = 20,
        FG_STATUS_21 = 21,
        FG_STATUS_CAMERA_LOCKED = 23,
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
        FG_STOP_02 = 2,
        FG_STOP_06 = 6,
        FG_STOP_DISABLE_INPUT = 7,
    };

    enum
    {
        FG_ROOM_ENEMY_25 = 25,
        FG_ROOM_ENEMY_26 = 26,
    };

    enum
    {
        FG_ZAPPING_5 = 5,
        FG_ZAPPING_6 = 6,
        FG_ZAPPING_15 = 15,
    };

    enum
    {
        EX_BATTLE_MODE_LEVEL_1 = 1,
        EX_BATTLE_MODE_LEVEL_2 = 3,
        EX_BATTLE_MODE_LEVEL_3 = 3,
    };

    extern GameTable& gGameTable;
    extern uint32_t& gGameFlags;
    extern uint32_t& gErrorCode;
    extern uint32_t& _memTop;
    extern PlayerEntity& gPlayerEntity;
    extern uint16_t& gPoisonStatus;
    extern uint8_t& gPoisonTimer;

    static const char* gStageSymbols = "123456789abcdefg";

    void mess_print(int x, int y, const uint8_t* str, short a4);
    uint8_t rnd();
    uint8_t rnd_area();
    void set_view(const Vec32p& pVp, const Vec32p& pVr);
    void bg_set_mode(int mode, int rgb);
    void set_geom_screen(int prj);
    bool check_flag(FlagGroup group, uint32_t index);
    void set_flag(FlagGroup group, uint32_t index, bool value);
    void set_stage();
    void stage_init_item();
    int set_game_seconds(int a0);
    void show_message(int a0, int a1, int a2, int a3);
    void update_timer();

    void* work_alloc(size_t len);
    template<typename T> static T* work_alloc()
    {
        return reinterpret_cast<T*>(work_alloc(sizeof(T)));
    }

    template<typename T> static T align(T value, size_t a = sizeof(size_t))
    {
        auto iValue = (uintptr_t)value;
        auto mask = a - 1;
        auto remainder = iValue & mask;
        return (T)(remainder == 0 ? iValue : iValue + a - remainder);
    }
}
