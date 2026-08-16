#include "rdt.h"
#include "openre.h"

namespace
{
    // Known spawn points for command-line room warps (-p/-r). Position and cut
    // values come from the game's new game and demo definitions (Pl0\pld\pdemo*.dat).
    const openre::rdt::RoomSpawnPoint kSpawnTable[] = {
        { 0, 0x00, 18802, 0, -3164, 2048, 0 },       // main hall (new game A)
        { 0, 0x04, -17920, 0, -21722, 400, 0 },      // new game B
        { 0, 0x0A, -24306, 0, -25311, 3007, 0 },     // demo 01
        { 0, 0x0C, -25458, -3600, -15973, -456, 7 }, // demo 00
        { 0, 0x18, -9340, 0, 3146, 5135, 11 },       // demo 02
        { 0, 0x1B, -9084, 0, -20156, 267, 0 },       // demo 12
        { 1, 0x03, -25967, 0, -21525, 3784, 0 },     // demo 10
    };
}

namespace openre::rdt
{
    template<> void* rdt_get_offset(RdtOffsetKind kind)
    {
        auto index = static_cast<size_t>(kind);
        return gGameTable.rdt->offsets[index];
    }

    const RoomSpawnPoint* rdt_get_spawn_point(int stage, int room)
    {
        for (const auto& entry : kSpawnTable)
        {
            if (entry.stage == stage && entry.room == room)
                return &entry;
        }
        return nullptr;
    }
}
