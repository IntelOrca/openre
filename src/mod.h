#pragma once

#include "fswatch.h"
#include "relua.h"

#include <filesystem>
#include <memory>
#include <string>

namespace openre::modding
{
    class Mod
    {
    public:
        std::string name;
        std::filesystem::path rootPath;
        std::filesystem::path scriptPath;
        std::unique_ptr<openre::lua::LuaVm> luaVm;
        bool reload{};

        Mod(const std::filesystem::path& path);
        void markForReload();
        void tick();
        void callHooks(openre::lua::HookKind kind);
        void log(const std::string& s);

    private:
        void run();
    };

    class ModManager
    {
    private:
        static inline std::unique_ptr<ModManager> instance;

        bool modsLoaded{};
        std::unique_ptr<openre::filesystem::FileWatcher> fileWatcher;

    public:
        std::vector<Mod> mods;

        static ModManager& get();

        ModManager();
        ModManager(const ModManager&) = delete;
        void tick();
        void callHooks(openre::lua::HookKind kind);

    private:
        void loadMods();
        void watch();
        std::filesystem::path getModsDirectory() const;
    };
}
