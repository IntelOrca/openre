#include "rdt.h"
#include "openre.h"

namespace openre::rdt
{
    template<>
    void* rdt_get_offset(RdtOffsetKind kind)
    {
        auto index = static_cast<size_t>(kind);
        return gGameTable.rdt->offsets[index];
    }
}
