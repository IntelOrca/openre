#define _CRT_SECURE_NO_WARNINGS

#include "mod.h"
#include "openre.h"
#include <cstdio>

namespace fs = std::filesystem;

namespace openre::modding
{
    Mod::Mod(const fs::path& path)
    {
        this->rootPath = path;
        this->name = path.filename().string();
        this->scriptPath = path / (this->name + ".lua");
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
    }
}
