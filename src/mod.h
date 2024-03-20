#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace openre::modding
{
    namespace fs = std::filesystem;

    class Mod
    {
    public:
        std::string name;
        fs::path rootPath;
        fs::path scriptPath;

        Mod(const fs::path& path);
        void log(const std::string& s);
    };

    class ModManager
    {
    private:
        static inline std::unique_ptr<ModManager> instance;

    public:
        std::vector<Mod> mods;

        static ModManager& get();

        ModManager();
        ModManager(const ModManager&) = delete;
        void loadMods();
    };
}
