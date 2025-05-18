#include "mod.h"
#include "openre.h"
#include "relua.h"
#include <cstdio>

using namespace openre::lua;

namespace fs = std::filesystem;

namespace openre::modding
{
    Mod::Mod(const fs::path& path)
    {
        this->rootPath = path;
        this->name = path.filename().string();
        this->scriptPath = path / (this->name + ".lua");
        this->reload = true;
    }

    void Mod::markForReload()
    {
        this->reload = true;
    }

    void Mod::tick()
    {
        if (this->reload)
        {
            this->reload = false;
            this->run();
        }
    }

    void Mod::run()
    {
        if (this->luaVm)
        {
            log("Shutdown");
        }
        log("Startup");
        this->luaVm = createLuaVm();
        this->luaVm->setLogCallback([this](const std::string& s) -> void { this->log(s); });
        this->luaVm->run(this->scriptPath.u8string());
    }

    void Mod::callHooks(HookKind kind)
    {
        if (this->luaVm)
        {
            this->luaVm->callHooks(kind);
        }
    }

    void Mod::log(const std::string& s)
    {
        std::printf("[%s] %s\n", this->name.c_str(), s.c_str());
    }

    ModManager& ModManager::get()
    {
        if (instance == nullptr)
        {
            instance = std::make_unique<ModManager>();
        }
        return *instance;
    }

    ModManager::ModManager() {}

    void ModManager::loadMods()
    {
        auto appdata = std::getenv("APPDATA");
        if (appdata == nullptr)
            return;

        auto openrePath = fs::u8path(appdata) / "openre";
        auto modsPath = openrePath / "mods";
        if (!fs::is_directory(modsPath))
            return;

        for (auto& e : std::filesystem::directory_iterator(modsPath))
        {
            if (!e.is_directory())
                continue;

            mods.emplace_back(e.path());
        }

        watch();
    }

    void ModManager::tick()
    {
        if (!modsLoaded)
        {
            modsLoaded = true;
            loadMods();
        }
        for (auto& mod : mods)
        {
            mod.tick();
        }
        callHooks(HookKind::tick);
    }

    void ModManager::callHooks(HookKind kind)
    {
        auto& modManager = ModManager::get();
        for (auto& mod : modManager.mods)
        {
            mod.callHooks(kind);
        }
    }

    void ModManager::watch()
    {
        auto modsPath = getModsDirectory();
        if (modsPath.empty())
            return;

        fileWatcher = std::make_unique<openre::filesystem::FileWatcher>(modsPath);
        fileWatcher->onFileChanged = [this](fs::path fileName) -> void {
            for (auto& mod : mods)
            {
                if (fileName == mod.scriptPath)
                {
                    mod.markForReload();
                }
            }
        };
    }

    std::filesystem::path ModManager::getModsDirectory() const
    {
        auto appdata = std::getenv("APPDATA");
        if (appdata == nullptr)
            return {};

        auto openrePath = fs::u8path(appdata) / "openre";
        auto modsPath = openrePath / "mods";
        if (!fs::is_directory(modsPath))
            return {};

        return modsPath;
    }
}
