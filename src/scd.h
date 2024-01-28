#pragma once

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

    void scd_init_hooks();
    void scd_init_tasks();
    void sce_scheduler_main();
    void scd_event_exec(int taskIndex, int evt);
}
