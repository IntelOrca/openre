#pragma once

namespace openre::lua
{
    enum class HookKind
    {
        Undefined,
        Tick,
    };

    void relua_init();
    void relua_call_hooks(HookKind kind);
}
