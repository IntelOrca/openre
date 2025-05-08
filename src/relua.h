#pragma once

#include <filesystem>
#include <functional>

namespace openre::lua
{
    enum class HookKind
    {
        undefined,
        tick,
    };

    class LuaVm
    {
    public:
        virtual ~LuaVm() {}
        virtual void run(const std::filesystem::path& path) = 0;
        virtual void callHooks(HookKind kind) = 0;
        virtual void setLogCallback(std::function<void(const std::string& s)> s) = 0;
    };

    std::unique_ptr<LuaVm> createLuaVm();
}
