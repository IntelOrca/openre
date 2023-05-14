#include <windows.h>
#include "interop.hpp"
#include "re2.h"
#include "scd.h"
#include "sce.h"
#include "player.h"

using namespace openre;
using namespace openre::player;
using namespace openre::scd;
using namespace openre::sce;

void snd_se_walk(int, int, PLAYER_WORK* pEm)
{
}

void onAttach()
{
    interop::writeJmp(0x4EDF40, &snd_se_walk);
    scd_init_hooks();
    sce_init_hooks();
    player_init_hooks();
}

extern "C"
{
    __declspec(dllexport) BOOL WINAPI openre_main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
    {
#pragma comment(linker, "/EXPORT:" __FUNCTION__"=" __FUNCDNAME__)
        return 0;
    }

    BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved)
    {
        // Perform actions based on the reason for calling.
        switch (fdwReason)
        {
        case DLL_PROCESS_ATTACH:
            // Initialize once for each new process.
            // Return FALSE to fail DLL load.
            onAttach();
            break;

        case DLL_THREAD_ATTACH:
            // Do thread-specific initialization.
            break;

        case DLL_THREAD_DETACH:
            // Do thread-specific cleanup.
            break;

        case DLL_PROCESS_DETACH:
            // Perform any necessary cleanup.
            break;
        }
        return TRUE;  // Successful DLL_PROCESS_ATTACH.
    }
}
