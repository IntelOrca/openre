#include "interop.hpp"
#include "openre.h"
#include "re2.h"

namespace openre::door
{
    using DoorAction = void (*)();

    enum
    {
        DOOR_STATE_INIT,
        DOOR_STATE_MOVE,
        DOOR_STATE_EXIT,
    };

    static void door_init();
    static void door_move();
    static void door_exit();

    // 0x00526254
    static DoorAction _doorActions[] = {
        door_init,
        door_move,
        door_exit,
        nullptr,
        nullptr,
    };

    // 0x0044FEA0
    static void door_main()
    {
        auto& ctcb = *gGameTable.ctcb;
        if (ctcb.var_08 != 0)
        {
            if (ctcb.var_08 != 1)
                return;
        }
        else
        {
            gGameTable.door_state = DOOR_STATE_INIT;
            ctcb.var_08 = 1;
        }

        auto action = _doorActions[gGameTable.door_state];
        if (action != nullptr)
        {
            action();
            if (ctcb.var_13 == 0)
                gGameTable.door_state++;
        }
    }

    // 0x0044FEF0
    static void door_init()
    {
        interop::call(0x0044FEF0);
    }

    // 0x00450120
    static void door_move()
    {
        interop::call(0x00450120);
    }

    // 0x004502D0
    static void door_exit()
    {
        interop::call(0x004502D0);
    }

    void door_init_hooks()
    {
        interop::writeJmp(0x0044FEA0, &door_main);
    }
}
