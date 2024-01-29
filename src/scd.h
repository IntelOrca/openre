#pragma once

#include <cstdint>

struct SceTask;

namespace openre::scd
{
    enum
    {
        SCE_TYPE_MAIN,
        SCE_TYPE_INIT,
    };

    enum
    {
        EVT_MAIN,
        EVT_FRAME,
    };

    enum
    {
        TASK_ID_RESERVED_0,
        TASK_ID_RESERVED_1,
    };

    constexpr uint8_t SCD_STATUS_EMPTY = 0;
    constexpr uint8_t SCD_STATUS_1 = 1;

    using SceTaskId = uint8_t;

    void scd_init_hooks();
    void scd_init_tasks();
    SceTask* get_task(SceTaskId index);
    void sce_scheduler_main();
    void scd_event_exec(int taskIndex, int evt);
}
