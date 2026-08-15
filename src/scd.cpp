#include "scd.h"
#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "interop.hpp"
#include "math.h"
#include "openre.h"
#include "rdt.h"
#include "re2.h"
#include "sce.h"
#include <cassert>
#include <cstring>

using namespace openre::audio;
using namespace openre::enemy;
using namespace openre::sce;
using namespace openre::rdt;
using namespace openre::camera;
using namespace openre::math;

namespace openre::scd
{
    using ScdOpcode = uint8_t;
    using ItemType = uint8_t;
    using HudKind = uint8_t;
    using ScdOpcodeImpl = int (*)(SceTask*);

    extern ScdOpcodeImpl gScdImplTable[];

    enum
    {
        SCD_NOP = 0x00,
        SCD_EVT_END = 0x01,
        SCD_EVT_NEXT = 0x02,
        SCD_EVT_CHAIN = 0x03,
        SCD_EVT_EXEC = 0x04,
        SCD_EVT_KILL = 0x05,
        SCD_IF = 0x06,
        SCD_ELSE = 0x07,
        SCD_ENDIF = 0x08,
        SCD_SLEEP = 0x09,
        SCD_SLEEPING = 0x0A,
        SCD_WSLEEP = 0x0B,
        SCD_WSLEEPING = 0x0C,
        SCD_FOR = 0x0D,
        SCD_NEXT = 0x0E,
        SCD_WHILE = 0x0F,
        SCD_EWHILE = 0x10,
        SCD_DO = 0x11,
        SCD_EDWHILE = 0x12,
        SCD_SWITCH = 0x13,
        SCD_CASE = 0x14,
        SCD_DEFAULT = 0x15,
        SCD_ESWITCH = 0x16,
        SCD_GOTO = 0x17,
        SCD_GOSUB = 0x18,
        SCD_RETURN = 0x19,
        SCD_BREAK = 0x1A,
        SCD_FOR2 = 0x1B,
        SCD_BREAK_POINT = 0x1C,
        SCD_WORK_COPY = 0x1D,
        SCD_NOP_1E = 0x1E,
        SCD_NOP_1F = 0x1F,
        SCD_NOP_20 = 0x20,
        SCD_CK = 0x21,
        SCD_SET = 0x22,
        SCD_CMP = 0x23,
        SCD_SAVE = 0x24,
        SCD_COPY = 0x25,
        SCD_CALC = 0x26,
        SCD_CALC2 = 0x27,
        SCD_SCE_RND = 0x28,
        SCD_CUT_CHG = 0x29,
        SCD_CUT_OLD = 0x2A,
        SCD_MESSAGE_ON = 0x2B,
        SCD_AOT_SET = 0x2C,
        SCD_OBJ_MODEL_SET = 0x2D,
        SCD_WORK_SET = 0x2E,
        SCD_SPEED_SET = 0x2F,
        SCD_ADD_SPEED = 0x30,
        SCD_ADD_ASPEED = 0x31,
        SCD_POS_SET = 0x32,
        SCD_DIR_SET = 0x33,
        SCD_MEMBER_SET = 0x34,
        SCD_MEMBER_SET2 = 0x35,
        SCD_SE_ON = 0x36,
        SCD_SCA_ID_SET = 0x37,
        SCD_FLR_SET = 0x38,
        SCD_DIR_CK = 0x39,
        SCD_SCE_ESPR_ON = 0x3A,
        SCD_DOOR_AOT_SE = 0x3B,
        SCD_CUT_AUTO = 0x3C,
        SCD_MEMBER_COPY = 0x3D,
        SCD_MEMBER_CMP = 0x3E,
        SCD_PLC_MOTION = 0x3F,
        SCD_PLC_DEST = 0x40,
        SCD_PLC_NECK = 0x41,
        SCD_PLC_RET = 0x42,
        SCD_PLC_FLG = 0x43,
        SCD_SCE_EM_SET = 0x44,
        SCD_COL_CHG_SET = 0x45,
        SCD_AOT_RESET = 0x46,
        SCD_AOT_ON = 0x47,
        SCD_SUPER_SET = 0x48,
        SCD_SUPER_RESET = 0x49,
        SCD_PLC_GUN = 0x4A,
        SCD_CUT_REPLACE = 0x4B,
        SCD_SCE_ESPR_KILL = 0x4C,
        SCD_DOOR_MODEL_SET = 0x4D,
        SCD_ITEM_AOT_SET = 0x4E,
        SCD_SCE_KEY_CK = 0x4F,
        SCD_SCE_TRG_CK = 0x50,
        SCD_SCE_BGM_CONTROL = 0x51,
        SCD_SCE_ESPR_CONTROL = 0x52,
        SCD_SCE_FADE_SET = 0x53,
        SCD_SCE_ESPR3D_ON = 0x54,
        SCD_MEMBER_CALC = 0x55,
        SCD_MEMBER_CALC2 = 0x56,
        SCD_SCE_BGMTBL_SET = 0x57,
        SCD_PLC_ROT = 0x58,
        SCD_XA_ON = 0x59,
        SCD_WEAPON_CHG = 0x5A,
        SCD_PLC_CNT = 0x5B,
        SCD_SCE_SHAKE_ON = 0x5C,
        SCD_MIZU_DIV_SET = 0x5D,
        SCD_KEEP_ITEM_CK = 0x5E,
        SCD_XA_VOL = 0x5F,
        SCD_KAGE_SET = 0x60,
        SCD_CUT_BE_SET = 0x61,
        SCD_SCE_ITEM_LOST = 0x62,
        SCD_PLC_GUN_EFF = 0x63,
        SCD_SCE_ESPR_ON2 = 0x64,
        SCD_SCE_ESPR_KILL2 = 0x65,
        SCD_PLC_STOP = 0x66,
        SCD_AOT_SET_4P = 0x67,
        SCD_DOOR_AOT_SET_4P = 0x68,
        SCD_ITEM_AOT_SET_4P = 0x69,
        SCD_LIGHT_POS_SET = 0x6A,
        SCD_LIGHT_KIDO_SET = 0x6B,
        SCD_RBJ_RESET = 0x6C,
        SCD_SCE_SCR_MOVE = 0x6D,
        SCD_PARTS_SET = 0x6E,
        SCD_MOVIE_ON = 0x6F,
        SCD_SPLC_RET = 0x70,
        SCD_SPLC_SCE = 0x71,
        SCD_SUPER_ON = 0x72,
        SCD_MIRROR_SET = 0x73,
        SCD_SCE_FADE_ADJUST = 0x74,
        SCD_SCE_ESPR3D_ON2 = 0x75,
        SCD_SCE_ITEM_GET = 0x76,
        SCD_SCE_LINE_START = 0x77,
        SCD_SCE_LINE_MAIN = 0x78,
        SCD_SCE_LINE_END = 0x79,
        SCD_SCE_PARTS_BOMB = 0x7A,
        SCD_SCE_PARTS_DOWN = 0x7B,
        SCD_LIGHT_COLOR_SET = 0x7C,
        SCD_LIGHT_POS_SET2 = 0x7D,
        SCD_LIGHT_KIDO_SET2 = 0x7E,
        SCD_LIGHT_COLOR_SET2 = 0x7F,
        SCD_SE_VOL = 0x80,
        SCD_ITEM_CNT_CK = 0x81,
        SCD_SCE_82 = 0x82,
        SCD_HEAL = 0x83,
        SCD_SCE_84 = 0x84,
        SCD_SCE_85 = 0x85,
        SCD_POISON_CK = 0x86,
        SCD_POISON_CLR = 0x87,
        SCD_SCE_ITEM_CK_LOST = 0x88,
        SCD_SCE_HEAL_PARTNER = 0x89,
        SCD_NOP_8A = 0x8A,
        SCD_NOP_8B = 0x8B,
        SCD_NOP_8C = 0x8C,
        SCD_SCE_8D = 0x8D,
        SCD_SCE_EM_SET2 = 0x8E,
    };
    enum
    {
        SCD_RESULT_FALSE,
        SCD_RESULT_NEXT,
        SCD_RESULT_NEXT_TICK,
    };
    enum
    {
        WK_NONE,
        WK_PLAYER,
        WK_SPLAYER,
        WK_ENEMY,
        WK_OBJECT,
        WK_DOOR,
        WK_ALL
    };
    struct ScdIfelCk
    {
        uint8_t Opcode;
        uint8_t pad_02;
        uint16_t BlockSize;
    };

    struct SceAot : SceAotBase
    {
        int16_t X;
        int16_t Z;
        uint16_t W;
        uint16_t D;
    };

    struct XZPoint
    {
        int16_t X;
        int16_t Z;
    };

    struct SceAot4p : SceAotBase
    {
        XZPoint Points[4];
    };

    struct SceAotDoor
    {
        SceAot Aot;
        SceAotDoorData Door;
    };

    struct ScdAotSet
    {
        uint8_t Opcode;
        uint8_t Id;
        SceAot Aot;
        uint8_t Data[6];
    };

    struct ScdSceAotDoor
    {
        uint8_t Opcode;
        uint8_t Id;
        SceAotDoor Data;
    };

    struct ScdAotSet4p
    {
        uint8_t Opcode;
        uint8_t Id;
        SceAot4p Aot;
        uint8_t Data[6];
    };

    struct SceAotItem
    {
        SceAot Aot;
        SceAotItemData Item;
    };

    struct ScdSceAotItem
    {
        uint8_t Opcode;
        uint8_t Id;
        SceAotItem Data;
    };

    struct ScdSceBgmControl
    {
        uint8_t Opcode;
        uint8_t var_01;
        uint8_t var_02;
        uint8_t var_03;
        uint8_t var_04;
        uint8_t var_05;
    };

    struct ScdSceBgmTblSet
    {
        uint8_t Opcode;
        uint8_t pad_01;
        uint16_t roomstage;
        uint16_t var_04;
        uint16_t var_06;
    };

    struct ScdCutBeSet
    {
        uint8_t Opcode;
        uint8_t Id;
        uint8_t Value;
        uint8_t OnOff;
    };

    struct ScdSceKeyCk
    {
        uint8_t Opcode;
        uint8_t flag;
        uint16_t key;
    };

    struct ScdCutCh
    {
        uint8_t Opcode;
        uint8_t Id;
    };

    struct ScdCutOld
    {
        uint8_t Opcode;
    };

    struct ScdMessageOn
    {
        uint8_t Opcode;
        uint8_t var_01;
        uint8_t var_02;
        uint8_t var_03;
        uint16_t var_04;
    };

    struct ScdCutAuto
    {
        uint8_t Opcode;
        uint8_t on;
    };

    struct ScdCutReplace
    {
        uint8_t Opcode;
        uint8_t Id;
        uint8_t value;
    };

    struct ScdSceEmSet
    {
        uint8_t opcode;
        uint8_t pad_01;
        uint8_t id;
        uint8_t type;
        uint8_t pose;
        uint8_t behaviour;
        uint8_t floor;
        uint8_t soundBank;
        uint8_t texture;
        uint8_t globalId;
        int16_t x;
        int16_t y;
        int16_t z;
        int16_t d;
        int16_t animation;
        int16_t var_14;
    };

    struct ScdSceEsprOn
    {
        uint8_t opcode;
        uint8_t nop;
        uint8_t esp_id;
        uint8_t esp_dt;
        uint8_t work_kind;
        uint8_t work_no;
        uint16_t espmv;
        int16_t svec_x;
        int16_t svec_y;
        int16_t svec_z;
        uint16_t dir_y;
    };

    struct ScdElseCk
    {
        uint8_t opcode;
        uint8_t var_01;
        uint16_t size;
    };

    struct SceItemAotSet
    {
        uint8_t opcode;
        uint8_t aot_id;
        SceAotBase aot;
        int16_t x;
        int16_t z;
        uint16_t w;
        uint16_t d;
        uint16_t item_id;
        uint16_t item_quantity;
        uint16_t flag;
        uint8_t md1;
        uint8_t action;
    };

    struct ScdMirrorSet
    {
        uint8_t opcode;
        uint8_t flag;
        uint16_t position;
        uint16_t min;
        uint16_t max;
    };
    constexpr uint8_t SAT_4P = (1 << 7);

    static int get_max_tasks()
    {
        return 14;
    }

    SceTask* get_task(SceTaskId index)
    {
        assert(index < get_max_tasks());
        return &((SceTask*)0x00694A00)[index];
    }

    static uint8_t* get_scd_event(int index)
    {
        auto scd = gGameTable.scd;
        auto evtTable = (uint16_t*)scd;
        return scd + evtTable[index];
    }

    // 0x004E39E0
    void scd_init()
    {
        auto maxTasks = get_max_tasks();
        for (auto i = 0; i < maxTasks; i++)
        {
            auto task = get_task(i);
            task->status = SCD_STATUS_EMPTY;
            task->task_level = maxTasks - i - 1;
            task->sub_ctr = 0;
            task->ifel_ctr[0] = 0xFF;
            task->loop_ctr[0] = 0xFF;
        }
        gGameTable.random_base = 0x138201C3;
    }

    void scd_init_tasks()
    {
        for (auto i = 0; i < 10; i++)
        {
            auto task = get_task(i);
            task->sub_ctr = i;
            task->routine = 0;
            task->status = 0;
            task->task_level = 0xFF;
            task->ifel_ctr[3] = 0xFF;
        }
    }

    int scd_execute_opcode(SceTask* task, ScdOpcode instruction)
    {
        return gScdImplTable[instruction](task);
    }

    // 0x004E4310
    void sce_scheduler_main()
    {
        for (auto i = 0; i < 10; i++)
        {
            auto task = get_task(i);
            if (task->status != SCD_STATUS_EMPTY)
            {
                while (true)
                {
                    auto opcode = *task->data;
                    auto result = scd_execute_opcode(task, opcode);
                    if (gGameTable.ctcb->var_13 != 0)
                        return;
                    if (result == SCD_RESULT_NEXT)
                        continue;
                    if (result == SCD_RESULT_NEXT_TICK)
                        break;
                    auto eax = task->sub_ctr;
                    auto cl = task->ifel_ctr[eax];
                    if (cl & 0x80)
                        break;
                    task->sp--;
                    task->data = *task->sp;
                    task->ifel_ctr[eax]--;
                }
            }
        }
        sce_work_clr();
        sce_work_clr_at();
    }

    // 0x004E3F60
    void scd_event_init(SceTask* task, int evt)
    {
        task->status = SCD_STATUS_1;
        task->routine = 0;
        task->sp = (uint8_t**)((uintptr_t)task + ((task->sub_ctr + 6) * 32));
        task->ifel_ctr[0] = -1;
        task->loop_ctr[0] = -1;
        task->data = get_scd_event(evt);
    }

    static SceTask* get_empty_task(int min, int max)
    {
        for (auto i = min; i < max; i++)
        {
            auto task = get_task(i);
            if (task->status == SCD_STATUS_EMPTY)
            {
                return task;
            }
        }
        return nullptr;
    }

    // 0x004E3FA0
    void scd_event_exec(int taskIndex, int evt)
    {
#if MORE_SCD_EVENTS
        auto min = 32;
        auto max = 100;
        auto cap = 10;
        if (gGameTable.sce_type != SCE_TYPE_MAIN)
        {
            min = 100;
            max = 140;
            cap = 14;
            if (taskIndex < cap)
                taskIndex = 100 + taskIndex;
        }
#else
        auto min = 2;
        auto max = 10;
        auto cap = 10;
        if (gGameTable.sce_type != SCE_TYPE_MAIN)
        {
            min = 10;
            max = 14;
            cap = 14;
        }
#endif

        auto task = taskIndex >= cap ? get_empty_task(min, max) : get_task(taskIndex);
        if (task != nullptr)
        {
            task->sub_ctr = 0;
            memset(task->spd, 0, (size_t)&task->r_no_bak - (size_t)task->spd);
            scd_event_init(task, evt);
        }
    }

    static bool is_enemy_dead(uint8_t globalId)
    {
        auto fgEnemy = FlagGroup::Enemy2;
        if (gGameTable.current_stage < 3 && !check_flag(FlagGroup::Status, FG_STATUS_BONUS))
        {
            fgEnemy = FlagGroup::Enemy;
        }
        return globalId != 0xFF && check_flag(fgEnemy, globalId);
    }

    static void* psx_alloc(size_t len)
    {
        auto mem = gGameTable.mem_top;
        gGameTable.mem_top = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(mem) + len);
#ifdef DEBUG
        // std::memset(mem, 0xCD, len);
#endif
        return mem;
    }

    template<typename T> static T* psx_alloc()
    {
        return reinterpret_cast<T*>(psx_alloc(sizeof(T)));
    }

    // 0x004B8470
    static int esp_call(int a0, int a1, Mat16& matrix, Vec16p& vec)
    {
        using sig = int (*)(int, int, Mat16&, Vec16p&);
        auto p = (sig)0x004B8470;
        return p(a0, a1, matrix, vec);
    }

    // 0x004E43B0
    static int scd_nop(SceTask* sce)
    {
        sce->data++;
        return SCD_RESULT_NEXT;
    }

    // 0x004E43D0
    static int scd_evt_end(SceTask* sce)
    {
        auto subroutineDepth = sce->sub_ctr;
        if (subroutineDepth == 0)
        {
            sce->status = SCD_STATUS_EMPTY;
            return SCD_RESULT_NEXT_TICK;
        }

        auto stackOffset = *(&sce->task_level + subroutineDepth);
        auto callerIndex = subroutineDepth - 1;
        sce->data = reinterpret_cast<uint8_t*>(sce->ret_addr[callerIndex]);
        sce->sub_ctr = callerIndex;
        sce->sp = reinterpret_cast<uint8_t**>(&(sce->stack[callerIndex + (stackOffset + 1)]));
        return SCD_RESULT_NEXT;
    }

    // 0x004E4420
    static int scd_evt_next(SceTask* sce)
    {
        sce->data++;
        return SCD_RESULT_NEXT_TICK;
    }

    // 0x004E4440
    static int scd_evt_chain(SceTask* sce)
    {
        scd_event_init(sce, *(sce->data + 3));
        return SCD_RESULT_NEXT;
    }

    // 0x004E4460
    static int scd_evt_exec(SceTask* sce)
    {
        sce->data++;
        auto taskIndex = *sce->data++;
        sce->data++;
        auto eventIndex = *sce->data++;
        scd_event_exec(taskIndex, eventIndex);
        return SCD_RESULT_NEXT;
    }

    // 0x004E4490
    static int scd_evt_kill(SceTask* sce)
    {
        sce->data++;
        auto taskId = *sce->data++;
        auto taskToKill = get_task(taskId);
        taskToKill->status = SCD_STATUS_EMPTY;
        return SCD_RESULT_NEXT;
    }

    // 0x004E44C0
    static int scd_if(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdIfelCk*>(sce->data);
        auto blockSize = opcode->BlockSize;
        sce->data += 4;
        sce->ifel_ctr[sce->sub_ctr]++;
        *sce->sp++ = sce->data + blockSize;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4510
    static int scd_else(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdElseCk*>(sce->data);
        sce->data += opcode->size;
        sce->sp -= 4;
        sce->ifel_ctr[sce->sub_ctr]--;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4550
    static int scd_endif(SceTask* sce)
    {
        sce->sp--;
        sce->ifel_ctr[sce->sub_ctr]--;
        sce->data += 2;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4580
    static int scd_sleep(SceTask* sce)
    {
        return interop::call<int>(0x004E4580, sce);
    }

    // 0x004E45C0
    static int scd_sleeping(SceTask* sce)
    {
        return interop::call<int>(0x004E45C0, sce);
    }

    // 0x004E4610
    static int scd_wsleep(SceTask* sce)
    {
        return interop::call<int>(0x004E4610, sce);
    }

    // 0x004E4640
    static int scd_wsleeping(SceTask* sce)
    {
        return interop::call<int>(0x004E4640, sce);
    }

    // 0x004E4670
    static int scd_for(SceTask* sce)
    {
        return interop::call<int>(0x004E4670, sce);
    }

    // 0x004E4770
    static int scd_next(SceTask* sce)
    {
        return interop::call<int>(0x004E4770, sce);
    }

    // 0x004E47C0
    static int scd_while(SceTask* sce)
    {
        return interop::call<int>(0x004E47C0, sce);
    }

    // 0x004E4830
    static int scd_ewhile(SceTask* sce)
    {
        return interop::call<int>(0x004E4830, sce);
    }

    // 0x004E4860
    static int scd_do(SceTask* sce)
    {
        return interop::call<int>(0x004E4860, sce);
    }

    // 0x004E48B0
    static int scd_edwhile(SceTask* sce)
    {
        return interop::call<int>(0x004E48B0, sce);
    }

    // 0x004E4970
    static int scd_switch(SceTask* sce)
    {
        return interop::call<int>(0x004E4970, sce);
    }

    // 0x004E9110
    static int scd_case(SceTask* sce)
    {
        return interop::call<int>(0x004E9110, sce);
    }

    // 0x004E4A00
    static int scd_default(SceTask* sce)
    {
        return interop::call<int>(0x004E4A00, sce);
    }

    // 0x004E4A20
    static int scd_eswitch(SceTask* sce)
    {
        return interop::call<int>(0x004E4A20, sce);
    }

    // 0x004E4A50
    static int scd_goto(SceTask* sce)
    {
        return interop::call<int>(0x004E4A50, sce);
    }

    // 0x004E4AA0
    static int scd_gosub(SceTask* sce)
    {
        return interop::call<int>(0x004E4AA0, sce);
    }

    // 0x004E4B00
    static int scd_return(SceTask* sce)
    {
        return interop::call<int>(0x004E4B00, sce);
    }

    // 0x004E4B40
    static int scd_break(SceTask* sce)
    {
        return interop::call<int>(0x004E4B40, sce);
    }

    // 0x004E46E0
    static int scd_for2(SceTask* sce)
    {
        return interop::call<int>(0x004E46E0, sce);
    }

    // 0x004E4B80
    static int scd_work_copy(SceTask* sce)
    {
        return interop::call<int>(0x004E4B80, sce);
    }

    // 0x004E4BF0
    static int scd_ck(SceTask* sce)
    {
        return interop::call<int>(0x004E4BF0, sce);
    }

    // 0x004E4C40
    static int scd_set(SceTask* sce)
    {
        return interop::call<int>(0x004E4C40, sce);
    }

    // 0x004E4CC0
    static int scd_cmp(SceTask* sce)
    {
        return interop::call<int>(0x004E4CC0, sce);
    }

    // 0x004E4D60
    static int scd_save(SceTask* sce)
    {
        return interop::call<int>(0x004E4D60, sce);
    }

    // 0x004E4D90
    static int scd_copy(SceTask* sce)
    {
        return interop::call<int>(0x004E4D90, sce);
    }

    // 0x004E4DD0
    static int scd_calc(SceTask* sce)
    {
        return interop::call<int>(0x004E4DD0, sce);
    }

    // 0x004E4E10
    static int scd_calc2(SceTask* sce)
    {
        return interop::call<int>(0x004E4E10, sce);
    }

    // 0x004E4F60
    static int scd_sce_rnd(SceTask* sce)
    {
        sce->data += 2;
        sce_rnd_set();
        return SCD_RESULT_NEXT;
    }

    // 0x004E4F80
    static int scd_cut_chg(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdCutCh*>(sce->data);
        set_flag(FlagGroup::Status, FG_STATUS_CAMERA_LOCKED, true);
        sub_4E5020(opcode->Id & 0x7F);
        set_flag(FlagGroup::Status, FG_STATUS_11, true);
        if (opcode->Id & 0x80)
        {
            gGameTable.byte_98F07B = 0;
        }
        sce->data += 2;
        return SCD_RESULT_NEXT;
    }

    // 0x004E4FE0
    static int scd_cut_old(SceTask* sce)
    {
        sub_4E5020(gGameTable.cut_old);
        set_flag(FlagGroup::Status, FG_STATUS_CAMERA_LOCKED, false);
        set_flag(FlagGroup::Status, FG_STATUS_11, true);
        sce->data++;
        return SCD_RESULT_NEXT;
    }

    // 0x004E5170
    static int scd_message_on(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdMessageOn*>(sce->data);
        auto a3 = opcode->var_04 << 16;
        show_message(0, opcode->var_03 + 768, opcode->var_02, a3);
        gGameTable.fg_stop |= a3;
        sce->data += 6;
        return SCD_RESULT_NEXT;
    }

    // 0x004E51C0
    static int scd_aot_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet*>(sce->data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        sce->data += sizeof(ScdAotSet);
        return SCD_RESULT_NEXT;
    }

    // 0x004E56B0
    static int scd_obj_model_set(SceTask* sce)
    {
        return interop::call<int>(0x004E56B0, sce);
    }

    // 0x004E5E90
    static int scd_work_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet4p*>(sce->data);
        auto wkKind = sce->data[1];
        auto wkIndex = sce->data[2];

        std::memset(sce->spd, 0, sizeof(sce->spd));
        std::memset(sce->dspd, 0, sizeof(sce->dspd));
        std::memset(sce->aspd, 0, sizeof(sce->aspd));
        std::memset(sce->adspd, 0, sizeof(sce->adspd));

        sce->data += 3;
        switch (wkKind)
        {
        case WK_PLAYER: sce->work = GetPlayerEntity(); break;
        case WK_SPLAYER: sce->work = GetPartnerEntity(); break;
        case WK_ENEMY: sce->work = GetEnemyEntity(wkIndex); break;
        case WK_OBJECT: sce->work = GetObjectEntity(wkIndex); break;
        case WK_DOOR: sce->work = GetDoorEntity(wkIndex); break;
        }
        return SCD_RESULT_NEXT;
    }

    // 0x004E6040
    static int scd_speed_set(SceTask* sce)
    {
        return interop::call<int>(0x004E6040, sce);
    }

    // 0x004E6070
    static int scd_add_speed(SceTask* sce)
    {
        return interop::call<int>(0x004E6070, sce);
    }

    // 0x004E60E0
    static int scd_add_aspeed(SceTask* sce)
    {
        return interop::call<int>(0x004E60E0, sce);
    }

    // 0x004E6150
    static int scd_pos_set(SceTask* sce)
    {
        return interop::call<int>(0x004E6150, sce);
    }

    // 0x004E61E0
    static int scd_dir_set(SceTask* sce)
    {
        return interop::call<int>(0x004E61E0, sce);
    }

    // 0x004E6220
    static int scd_member_set(SceTask* sce)
    {
        return interop::call<int>(0x004E6220, sce);
    }

    // 0x004E6260
    static int scd_member_set2(SceTask* sce)
    {
        return interop::call<int>(0x004E6260, sce);
    }

    // 0x004E6D10
    static int scd_se_on(SceTask* sce)
    {
        return interop::call<int>(0x004E6D10, sce);
    }

    // 0x004E6DF0
    static int scd_sca_id_set(SceTask* sce)
    {
        return interop::call<int>(0x004E6DF0, sce);
    }

    // 0x004E8BE0
    static int scd_flr_set(SceTask* sce)
    {
        return interop::call<int>(0x004E8BE0, sce);
    }

    // 0x004E6CD0
    static int scd_dir_ck(SceTask* sce)
    {
        return interop::call<int>(0x004E6CD0, sce);
    }

    // 0x004E6E30
    static int scd_sce_espr_on(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceEsprOn*>(sce->data);
        sce->data += sizeof(ScdSceEsprOn);
        uint32_t esp_id = opcode->esp_id;

        Mat16& matrix = get_matrix(opcode->work_kind, opcode->work_no);
        Vec16p vec16p{ opcode->svec_x, opcode->svec_y, opcode->svec_z };

        if ((gGameTable.current_stage == 4 && gGameTable.current_room != 0) || gGameTable.current_stage != 3
            || gGameTable.current_room != 10)
        {
            esp_call(opcode->espmv | (((esp_id << 16) | (esp_id & 0xFFFFFF00)) << 8), opcode->dir_y, matrix, vec16p);
            return SCD_RESULT_NEXT;
        }

        if (opcode->work_kind)
        {
            esp_call(opcode->espmv | (((esp_id << 16) | esp_id & 0xFFFFFF00) << 8), opcode->dir_y, matrix, vec16p);
            return SCD_RESULT_NEXT;
        }

        auto v5 = (gGameTable.blood_censor | (esp_id << 8)) << 8;
        esp_call(opcode->espmv | ((esp_id | v5) << 8), opcode->dir_y, matrix, vec16p);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5250
    static int scd_door_aot_se(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceAotDoor*>(sce->data);
        set_aot_entry(opcode->Id, &opcode->Data.Aot);
        sce->data += sizeof(ScdSceAotDoor);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5050
    static int scd_cut_auto(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdCutAuto*>(sce->data);
        set_flag(FlagGroup::Status, FG_STATUS_CAMERA_LOCKED, !opcode->on);
        sce->data += 2;
        return SCD_RESULT_NEXT;
    }

    // 0x004E6610
    static int scd_member_copy(SceTask* sce)
    {
        return interop::call<int>(0x004E6610, sce);
    }

    // 0x004E6650
    static int scd_member_cmp(SceTask* sce)
    {
        return interop::call<int>(0x004E6650, sce);
    }

    // 0x004E72D0
    static int scd_plc_motion(SceTask* sce)
    {
        auto group = sce->data[1];
        auto animation = sce->data[2];
        auto flags = sce->data[3];
        auto entity = reinterpret_cast<PlayerEntity*>(sce->work);

        entity->routine_0 = 4;
        entity->routine_1 = group;
        entity->routine_2 = 0;
        entity->routine_3 = 0;
        entity->move_no = animation;
        entity->move_cnt = 0;
        entity->sce_flg = flags;
        entity->sce_free0 = 0;
        entity->sce_free1 = 0;

        sce->data += 4;
        return SCD_RESULT_NEXT;
    }

    // 0x004E7330
    static int scd_plc_dest(SceTask* sce)
    {
        return interop::call<int>(0x004E7330, sce);
    }

    // 0x004E7510
    static int scd_plc_neck(SceTask* sce)
    {
        return interop::call<int>(0x004E7510, sce);
    }

    // 0x004E7630
    static int scd_plc_ret(SceTask* sce)
    {
        return interop::call<int>(0x004E7630, sce);
    }

    // 0x004E76C0
    static int scd_plc_flg(SceTask* sce)
    {
        return interop::call<int>(0x004E76C0, sce);
    }

    // 0x004E77D0
    static int scd_sce_em_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceEmSet*>(sce->data);
        sce->data += sizeof(ScdSceEmSet);

        if (is_enemy_dead(opcode->globalId))
        {
            return SCD_RESULT_NEXT;
        }

        EnemySpawnInfo spawnInfo;
        spawnInfo.Animation = opcode->animation;
        spawnInfo.Id = opcode->id;
        spawnInfo.Type = opcode->type;
        spawnInfo.Pose = opcode->pose;
        spawnInfo.Behaviour = opcode->behaviour;
        spawnInfo.Floor = opcode->floor;
        spawnInfo.SoundBank = opcode->soundBank;
        spawnInfo.Texture = opcode->texture;
        spawnInfo.GlobalId = opcode->globalId;
        spawnInfo.Position.x = opcode->x;
        spawnInfo.Position.y = opcode->y;
        spawnInfo.Position.z = opcode->z;
        spawnInfo.Position.d = opcode->d;
        spawnInfo.Animation = opcode->animation;
        spawnInfo.Unknown = opcode->var_14;
        spawn_enemy(spawnInfo);

        return SCD_RESULT_NEXT;
    }

    // 0x004E8F40
    static int scd_col_chg_set(SceTask* sce)
    {
        return interop::call<int>(0x004E8F40, sce);
    }

    // 0x004E5600
    static int scd_aot_reset(SceTask* sce)
    {
        return interop::call<int>(0x004E5600, sce);
    }

    // 0x004E5660
    static int scd_aot_on(SceTask* sce)
    {
        return interop::call<int>(0x004E5660, sce);
    }

    // 0x004E59E0
    static int scd_super_set(SceTask* sce)
    {
        return interop::call<int>(0x004E59E0, sce);
    }

    // 0x004E5E30
    static int scd_super_reset(SceTask* sce)
    {
        return interop::call<int>(0x004E5E30, sce);
    }

    // 0x004E74D0
    static int scd_plc_gun(SceTask* sce)
    {
        return interop::call<int>(0x004E74D0, sce);
    }

    // 0x004E5090
    static int scd_cut_replace(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdCutReplace*>(sce->data);
        auto rvd = rdt_get_offset<uintptr_t>(RdtOffsetKind::RVD);
        auto vCuts = reinterpret_cast<VCut*>((uint32_t)rvd + 2);

        if (vCuts->be_flg != -1)
        {
            uint8_t nextBeFlg = 0;
            do
            {
                if (vCuts->be_flg == opcode->Id)
                {
                    vCuts->be_flg = opcode->value;
                }
                else if (vCuts->be_flg == opcode->value)
                {
                    vCuts->be_flg = opcode->Id;
                }

                if (vCuts->nFloor == opcode->Id)
                {
                    vCuts->nFloor = opcode->value;
                }
                else if (vCuts->nFloor == opcode->value)
                {
                    vCuts->nFloor = opcode->Id;
                }
                nextBeFlg = vCuts[1].be_flg;
                ++vCuts;

            } while (nextBeFlg != 0xFF);
        }

        if (gGameTable.vcut_data[1]->fCut == opcode->Id)
        {
            cut_change(opcode->value);
        }
        sce->data += 3;
        return SCD_RESULT_NEXT;
    }

    // 0x004E70F0
    static int scd_sce_espr_kill(SceTask* sce)
    {
        return interop::call<int>(0x004E70F0, sce);
    }

    // 0x004504D0
    static int scd_door_model_set(SceTask* sce)
    {
        return interop::call<int>(0x004504D0, sce);
    }

    // 0x004E52E0
    static int scd_item_aot_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<SceItemAotSet*>(sce->data);
        sce->data += sizeof(SceItemAotSet);
        set_aot_entry(opcode->aot_id, &opcode->aot);
        auto obj = GetObjectEntity(opcode->md1);

        auto flagGroup = gGameTable.current_stage < 4 ? FlagGroup::Item : FlagGroup::Item2;
        // Item already picked up
        if (check_flag(flagGroup, opcode->flag))
        {
            opcode->aot.Sce = 0;
            if (opcode->md1 < 32)
            {
                obj->be_flg = 0x80000000;
                obj->free0 = 0;
            }
            return SCD_RESULT_NEXT;
        }
        if (opcode->md1 >= 32)
        {
            return SCD_RESULT_NEXT;
        }

        obj->free0 = opcode->action;
        if (opcode->action & 2)
        {
            obj->be_flg = 0x80000000;
        }
        return SCD_RESULT_NEXT;
    }

    // 0x004E8230
    static int scd_sce_key_ck(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceKeyCk*>(sce->data);
        sce->data += 4;
        if ((gGameTable.g_key & opcode->key) == 0)
        {
            return opcode->flag ^ 1;
        }
        return opcode->flag;
    }

    // 0x004E8260
    static int scd_sce_trg_ck(SceTask* sce)
    {
        return interop::call<int>(0x004E8260, sce);
    }

    // 0x004E8290
    static int scd_sce_bgm_control(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceBgmControl*>(sce->data);

        auto arg = (opcode->var_05) | (opcode->var_04 << 8) | (opcode->var_03 << 16) | (opcode->var_02 << 24)
            | (opcode->var_01 << 28);
        bgm_set_control(arg);

        sce->data += sizeof(ScdSceBgmControl);
        return SCD_RESULT_NEXT;
    }

    // 0x004E71D0
    static int scd_sce_espr_control(SceTask* sce)
    {
        return interop::call<int>(0x004E71D0, sce);
    }

    // 0x004E8320
    static int scd_sce_fade_set(SceTask* sce)
    {
        return interop::call<int>(0x004E8320, sce);
    }

    // 0x004E6FA0
    static int scd_sce_espr3d_on(SceTask* sce)
    {
        return interop::call<int>(0x004E6FA0, sce);
    }

    // 0x004E69C0
    static int scd_member_calc(SceTask* sce)
    {
        return interop::call<int>(0x004E69C0, sce);
    }

    // 0x004E6A10
    static int scd_member_calc2(SceTask* sce)
    {
        return interop::call<int>(0x004E6A10, sce);
    }

    // 0x004E82E0
    static int scd_sce_bgmtbl_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdSceBgmTblSet*>(sce->data);
        bgm_set_entry((opcode->roomstage << 16) | opcode->var_06 | opcode->var_04);
        sce->data += sizeof(ScdSceBgmTblSet);
        return SCD_RESULT_NEXT;
    }

    // 0x004E7740
    static int scd_plc_rot(SceTask* sce)
    {
        return interop::call<int>(0x004E7740, sce);
    }

    // 0x004E8470
    static int scd_xa_on(SceTask* sce)
    {
        return interop::call<int>(0x004E8470, sce);
    }

    // 0x004E87B0
    static int scd_weapon_chg(SceTask* sce)
    {
        return interop::call<int>(0x004E87B0, sce);
    }

    // 0x004E77A0
    static int scd_plc_cnt(SceTask* sce)
    {
        return interop::call<int>(0x004E77A0, sce);
    }

    // 0x004E8550
    static int scd_sce_shake_on(SceTask* sce)
    {
        return interop::call<int>(0x004E8550, sce);
    }

    // 0x004E85F0
    static int scd_mizu_div_set(SceTask* sce)
    {
        return interop::call<int>(0x004E85F0, sce);
    }

    // 0x004E8630
    static int scd_keep_item_ck(SceTask* sce)
    {
        return interop::call<int>(0x004E8630, sce);
    }

    // 0x004E84B0
    static int scd_xa_vol(SceTask* sce)
    {
        return interop::call<int>(0x004E84B0, sce);
    }

    // 0x004E8890
    static int scd_kage_set(SceTask* sce)
    {
        return interop::call<int>(0x004E8890, sce);
    }

    // 0x004E5120
    static int scd_cut_be_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdCutBeSet*>(sce->data);
        auto vcut = rdt_get_offset<VCut>(RdtOffsetKind::RVD);
        while (vcut->fCut != opcode->Id)
        {
            vcut++;
        }
        vcut[opcode->Value].be_flg = opcode->OnOff;
        sce->data += 4;
        return SCD_RESULT_NEXT;
    }

    // 0x004E8700
    static int scd_sce_item_lost(SceTask* sce)
    {
        return interop::call<int>(0x004E8700, sce);
    }

    // 0x004E6F10
    static int scd_sce_espr_on2(SceTask* sce)
    {
        return interop::call<int>(0x004E6F10, sce);
    }

    // 0x004E7130
    static int scd_sce_espr_kill2(SceTask* sce)
    {
        return interop::call<int>(0x004E7130, sce);
    }

    // 0x004E7670
    static int scd_plc_stop(SceTask* sce)
    {
        return interop::call<int>(0x004E7670, sce);
    }

    // 0x004E5200
    static int scd_aot_set_4p(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdAotSet4p*>(sce->data);
        set_aot_entry(opcode->Id, &opcode->Aot);
        opcode->Aot.Sat |= SAT_4P;
        sce->data += sizeof(ScdAotSet4p);
        return SCD_RESULT_NEXT;
    }

    // 0x004E5290
    static int scd_door_aot_set_4p(SceTask* sce)
    {
        return interop::call<int>(0x004E5290, sce);
    }

    // 0x004E5520
    static int scd_item_aot_set_4p(SceTask* sce)
    {
        return interop::call<int>(0x004E5520, sce);
    }

    // 0x004E8990
    static int scd_light_pos_set(SceTask* sce)
    {
        return interop::call<int>(0x004E8990, sce);
    }

    // 0x004E8A10
    static int scd_light_kido_set(SceTask* sce)
    {
        return interop::call<int>(0x004E8A10, sce);
    }

    // 0x004E4BD0
    static int scd_rbj_reset(SceTask* sce)
    {
        return interop::call<int>(0x004E4BD0, sce);
    }

    // 0x004E8BB0
    static int scd_sce_scr_move(SceTask* sce)
    {
        return interop::call<int>(0x004E8BB0, sce);
    }

    // 0x004E5F60
    static int scd_parts_set(SceTask* sce)
    {
        return interop::call<int>(0x004E5F60, sce);
    }

    // 0x004E8C40
    static int scd_movie_on(SceTask* sce)
    {
        return interop::call<int>(0x004E8C40, sce);
    }

    // 0x004E8C70
    static int scd_splc_ret(SceTask* sce)
    {
        return interop::call<int>(0x004E8C70, sce);
    }

    // 0x004E8D00
    static int scd_splc_sce(SceTask* sce)
    {
        return interop::call<int>(0x004E8D00, sce);
    }

    // 0x004E5BC0
    static int scd_super_on(SceTask* sce)
    {
        return interop::call<int>(0x004E5BC0, sce);
    }

    // 0x004E8DB0
    static int scd_mirror_set(SceTask* sce)
    {
        auto opcode = reinterpret_cast<ScdMirrorSet*>(sce->data);
        set_flag(FlagGroup::Status, FG_STATUS_MIRROR, true);
        gGameTable.byte_989E75 = opcode->flag;
        gGameTable.word_989E76 = opcode->position;
        gGameTable.word_989E78 = opcode->min;
        gGameTable.word_989E7A = opcode->max;
        sce->data += sizeof(ScdMirrorSet);
        return SCD_RESULT_NEXT;
    }

    // 0x004E83C0
    static int scd_sce_fade_adjust(SceTask* sce)
    {
        return interop::call<int>(0x004E83C0, sce);
    }

    // 0x004E7040
    static int scd_sce_espr3d_on2(SceTask* sce)
    {
        return interop::call<int>(0x004E7040, sce);
    }

    // 0x004E8830
    static int scd_sce_item_get(SceTask* sce)
    {
        return interop::call<int>(0x004E8830, sce);
    }

    // 0x004E8580
    static int scd_sce_line_start(SceTask* sce)
    {
        return interop::call<int>(0x004E8580, sce);
    }

    // 0x004E85B0
    static int scd_sce_line_main(SceTask* sce)
    {
        return interop::call<int>(0x004E85B0, sce);
    }

    // 0x004E85D0
    static int scd_sce_line_end(SceTask* sce)
    {
        return interop::call<int>(0x004E85D0, sce);
    }

    // 0x004E8E10
    static int scd_sce_parts_bomb(SceTask* sce)
    {
        return interop::call<int>(0x004E8E10, sce);
    }

    // 0x004E8EB0
    static int scd_sce_parts_down(SceTask* sce)
    {
        return interop::call<int>(0x004E8EB0, sce);
    }

    // 0x004E8A50
    static int scd_light_color_set(SceTask* sce)
    {
        return interop::call<int>(0x004E8A50, sce);
    }

    // 0x004E8AA0
    static int scd_light_pos_set2(SceTask* sce)
    {
        return interop::call<int>(0x004E8AA0, sce);
    }

    // 0x004E8B20
    static int scd_light_kido_set2(SceTask* sce)
    {
        return interop::call<int>(0x004E8B20, sce);
    }

    // 0x004E8B60
    static int scd_light_color_set2(SceTask* sce)
    {
        return interop::call<int>(0x004E8B60, sce);
    }

    // 0x004E84E0
    static int scd_se_vol(SceTask* sce)
    {
        return interop::call<int>(0x004E84E0, sce);
    }

    // 0x004E8660
    static int scd_item_cnt_ck(SceTask* sce)
    {
        return interop::call<int>(0x004E8660, sce);
    }

    // 0x004E7160
    static int scd_sce_82(SceTask* sce)
    {
        return interop::call<int>(0x004E7160, sce);
    }

    // 0x004E8FB0
    static int scd_heal(SceTask* sce)
    {
        sce->data++;
        gGameTable.pl.life = gGameTable.pl.max_life;
        gGameTable.poison_timer = 0;
        gGameTable.poison_status = 0;
        return SCD_RESULT_NEXT;
    }

    // 0x004E8FE0
    static int scd_sce_84(SceTask* sce)
    {
        return interop::call<int>(0x004E8FE0, sce);
    }

    // 0x004E9030
    static int scd_sce_85(SceTask* sce)
    {
        return interop::call<int>(0x004E9030, sce);
    }

    // 0x004E90C0
    static int scd_poison_ck(SceTask* sce)
    {
        sce->data++;
        return gGameTable.poison_status != 0 ? SCD_RESULT_NEXT : SCD_RESULT_FALSE;
    }

    // 0x004E90E0
    static int scd_poison_clr(SceTask* sce)
    {
        sce->data++;
        gGameTable.poison_timer = 0;
        gGameTable.poison_status = 0;
        gGameTable.pl.routine_0 = 1;
        return SCD_RESULT_NEXT;
    }

    // 0x004E8750
    static int scd_sce_item_ck_lost(SceTask* sce)
    {
        return interop::call<int>(0x004E8750, sce);
    }

    // 0x004E8D70
    static int scd_sce_heal_partner(SceTask* sce)
    {
        gGameTable.byte_98EE7B = 0;
        gGameTable.saved_splayer_health = 200;
        gGameTable.splayer_work->life = 200;
        gGameTable.splayer_work->max_life = 200;
        sce->data++;
        return SCD_RESULT_NEXT;
    }

    // 0x004E9110
    static int scd_nop_8a(SceTask* sce)
    {
        return interop::call<int>(0x004E9110, sce);
    }

    // 0x004E9110
    static int scd_nop_8b(SceTask* sce)
    {
        return interop::call<int>(0x004E9110, sce);
    }

    // 0x004E9130
    static int scd_nop_8c(SceTask* sce)
    {
        return interop::call<int>(0x004E9130, sce);
    }

    // 0x004E53B0
    static int scd_sce_8d(SceTask* sce)
    {
        return interop::call<int>(0x004E53B0, sce);
    }

    // 0x004E7CB0
    static int scd_sce_em_set2(SceTask* sce)
    {
        return interop::call<int>(0x004E7CB0, sce);
    }

    ScdOpcodeImpl gScdImplTable[] = {
        scd_nop,              // 0x00
        scd_evt_end,          // 0x01
        scd_evt_next,         // 0x02
        scd_evt_chain,        // 0x03
        scd_evt_exec,         // 0x04
        scd_evt_kill,         // 0x05
        scd_if,               // 0x06
        scd_else,             // 0x07
        scd_endif,            // 0x08
        scd_sleep,            // 0x09
        scd_sleeping,         // 0x0A
        scd_wsleep,           // 0x0B
        scd_wsleeping,        // 0x0C
        scd_for,              // 0x0D
        scd_next,             // 0x0E
        scd_while,            // 0x0F
        scd_ewhile,           // 0x10
        scd_do,               // 0x11
        scd_edwhile,          // 0x12
        scd_switch,           // 0x13
        scd_case,             // 0x14
        scd_default,          // 0x15
        scd_eswitch,          // 0x16
        scd_goto,             // 0x17
        scd_gosub,            // 0x18
        scd_return,           // 0x19
        scd_break,            // 0x1A
        scd_for2,             // 0x1B
        scd_nop,              // 0x1C
        scd_work_copy,        // 0x1D
        scd_nop,              // 0x1E
        scd_nop,              // 0x1F
        scd_nop,              // 0x20
        scd_ck,               // 0x21
        scd_set,              // 0x22
        scd_cmp,              // 0x23
        scd_save,             // 0x24
        scd_copy,             // 0x25
        scd_calc,             // 0x26
        scd_calc2,            // 0x27
        scd_sce_rnd,          // 0x28
        scd_cut_chg,          // 0x29
        scd_cut_old,          // 0x2A
        scd_message_on,       // 0x2B
        scd_aot_set,          // 0x2C
        scd_obj_model_set,    // 0x2D
        scd_work_set,         // 0x2E
        scd_speed_set,        // 0x2F
        scd_add_speed,        // 0x30
        scd_add_aspeed,       // 0x31
        scd_pos_set,          // 0x32
        scd_dir_set,          // 0x33
        scd_member_set,       // 0x34
        scd_member_set2,      // 0x35
        scd_se_on,            // 0x36
        scd_sca_id_set,       // 0x37
        scd_flr_set,          // 0x38
        scd_dir_ck,           // 0x39
        scd_sce_espr_on,      // 0x3A
        scd_door_aot_se,      // 0x3B
        scd_cut_auto,         // 0x3C
        scd_member_copy,      // 0x3D
        scd_member_cmp,       // 0x3E
        scd_plc_motion,       // 0x3F
        scd_plc_dest,         // 0x40
        scd_plc_neck,         // 0x41
        scd_plc_ret,          // 0x42
        scd_plc_flg,          // 0x43
        scd_sce_em_set,       // 0x44
        scd_col_chg_set,      // 0x45
        scd_aot_reset,        // 0x46
        scd_aot_on,           // 0x47
        scd_super_set,        // 0x48
        scd_super_reset,      // 0x49
        scd_plc_gun,          // 0x4A
        scd_cut_replace,      // 0x4B
        scd_sce_espr_kill,    // 0x4C
        scd_door_model_set,   // 0x4D
        scd_item_aot_set,     // 0x4E
        scd_sce_key_ck,       // 0x4F
        scd_sce_trg_ck,       // 0x50
        scd_sce_bgm_control,  // 0x51
        scd_sce_espr_control, // 0x52
        scd_sce_fade_set,     // 0x53
        scd_sce_espr3d_on,    // 0x54
        scd_member_calc,      // 0x55
        scd_member_calc2,     // 0x56
        scd_sce_bgmtbl_set,   // 0x57
        scd_plc_rot,          // 0x58
        scd_xa_on,            // 0x59
        scd_weapon_chg,       // 0x5A
        scd_plc_cnt,          // 0x5B
        scd_sce_shake_on,     // 0x5C
        scd_mizu_div_set,     // 0x5D
        scd_keep_item_ck,     // 0x5E
        scd_xa_vol,           // 0x5F
        scd_kage_set,         // 0x60
        scd_cut_be_set,       // 0x61
        scd_sce_item_lost,    // 0x62
        scd_nop,              // 0x63
        scd_sce_espr_on2,     // 0x64
        scd_sce_espr_kill2,   // 0x65
        scd_plc_stop,         // 0x66
        scd_aot_set_4p,       // 0x67
        scd_door_aot_set_4p,  // 0x68
        scd_item_aot_set_4p,  // 0x69
        scd_light_pos_set,    // 0x6A
        scd_light_kido_set,   // 0x6B
        scd_rbj_reset,        // 0x6C
        scd_sce_scr_move,     // 0x6D
        scd_parts_set,        // 0x6E
        scd_movie_on,         // 0x6F
        scd_splc_ret,         // 0x70
        scd_splc_sce,         // 0x71
        scd_super_on,         // 0x72
        scd_mirror_set,       // 0x73
        scd_sce_fade_adjust,  // 0x74
        scd_sce_espr3d_on2,   // 0x75
        scd_sce_item_get,     // 0x76
        scd_sce_line_start,   // 0x77
        scd_sce_line_main,    // 0x78
        scd_sce_line_end,     // 0x79
        scd_sce_parts_bomb,   // 0x7A
        scd_sce_parts_down,   // 0x7B
        scd_light_color_set,  // 0x7C
        scd_light_pos_set2,   // 0x7D
        scd_light_kido_set2,  // 0x7E
        scd_light_color_set2, // 0x7F
        scd_se_vol,           // 0x80
        scd_item_cnt_ck,      // 0x81
        scd_sce_82,           // 0x82
        scd_heal,             // 0x83
        scd_sce_84,           // 0x84
        scd_sce_85,           // 0x85
        scd_poison_ck,        // 0x86
        scd_poison_clr,       // 0x87
        scd_sce_item_ck_lost, // 0x88
        scd_sce_heal_partner, // 0x89
        scd_nop_8a,           // 0x8A
        scd_nop_8b,           // 0x8B
        scd_nop_8c,           // 0x8C
        scd_sce_8d,           // 0x8D
        scd_sce_em_set2,      // 0x8E
    };

    void scd_init_hooks()
    {
        interop::writeJmp(0x004E3F60, &scd_event_init);
    }

}
