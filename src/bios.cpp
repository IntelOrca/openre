#include "font.h"
#include "gfx.h"
#include "relua.h"
#include "shell.h"

#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

using namespace openre::graphics;
using namespace openre::input;
using namespace openre::lua;

namespace openre
{
    struct MenuItem
    {
        std::string text;
        std::function<void()> action;
    };

    class Bios
    {
    private:
        OpenREShell& shell;
        ResourceCookie defaultFont{};
        std::unique_ptr<LuaVm> gameLuaVm;

        bool gameSuspended{};
        std::vector<MenuItem> menuItems;
        int32_t selectedIndex{};

    public:
        Bios(OpenREShell& shell)
            : shell(shell)
        {
        }

        void initialize()
        {
            this->defaultFont = loadBuiltInFont(this->shell);
            this->showMenu();
        }

        void update()
        {
            auto& input = this->shell.getInputState();
            if (input.commandsPressed[static_cast<int32_t>(InputCommand::debugBios)])
            {
                gameSuspended = !gameSuspended;
            }

            if (gameSuspended || this->gameLuaVm == nullptr)
            {
                updateMenu();
                drawMenu();
            }
            else
            {
                this->gameLuaVm->gc();
                this->gameLuaVm->callHooks(openre::lua::HookKind::tick);
            }
        }

    private:
        void clearList()
        {
            this->menuItems.clear();
            this->selectedIndex = -1;
        }

        void addListItem(std::string_view s, std::function<void()> action = nullptr)
        {
            this->menuItems.push_back({ std::string(s), action });
            if (this->selectedIndex == -1)
                this->selectedIndex = 0;
        }

        void showMenu()
        {
            this->clearList();
            this->addListItem("SELECT GAME", [this]() { showGameMenu(); });
            this->addListItem("MODS", [this]() {
                this->clearList();
                this->addListItem("* RESIDENT EVIL 2 HD");
            });
            this->addListItem("OPTIONS");
            this->addListItem("DEBUG");
            this->addListItem("EXIT", [this]() { this->shell.exit(); });
        }

        void showGameMenu()
        {
            this->clearList();

            auto gamesDir = getGamesDirectory();
            for (const auto& gameDir : fs::directory_iterator(gamesDir))
            {
                if (!gameDir.is_directory())
                    continue;

                const auto& gamePath = gameDir.path();
                addListItem(gamePath.filename().u8string(), [this, gamePath]() { startGame(gamePath); });
            }
        }

        void menuBack()
        {
            showMenu();
        }

        void menuSelect()
        {
            if (this->selectedIndex >= 0 && static_cast<size_t>(this->selectedIndex) < this->menuItems.size())
            {
                auto& action = this->menuItems[this->selectedIndex].action;
                if (action)
                    action();
            }
        }

        void updateMenu()
        {
            auto& input = shell.getInputState();
            if (input.commandsPressed[static_cast<int32_t>(InputCommand::up)])
            {
                if (this->selectedIndex > 0)
                {
                    this->selectedIndex--;
                }
            }
            else if (input.commandsPressed[static_cast<int32_t>(InputCommand::down)])
            {
                if (static_cast<size_t>(this->selectedIndex) < this->menuItems.size() - 1)
                {
                    this->selectedIndex++;
                }
            }
            else if (input.commandsPressed[static_cast<int32_t>(InputCommand::menuCancel)])
            {
                menuBack();
            }
            else if (input.commandsPressed[static_cast<int32_t>(InputCommand::menuApply)])
            {
                menuSelect();
            }
        }

        void drawMenu()
        {
            TextFormatting formatting;
            formatting.scale = 1.0f;

            auto renderSize = this->shell.getRenderSize();
            drawTextLine(this->shell, this->defaultFont, "OpenRE", 0, 0, 0, formatting);

            formatting.scale = 0.5f;
            float y = 100;
            for (size_t i = 0; i < this->menuItems.size(); i++)
            {
                formatting.color = { 128, 128, 128, 1 };
                if (static_cast<size_t>(selectedIndex) == i)
                {
                    formatting.color = { 255, 255, 0, 1 };
                }

                auto& menuItem = this->menuItems[i];
                drawTextLine(this->shell, this->defaultFont, menuItem.text, 20, y, 0, formatting);
                y += 42;
            }
        }

        void startGame(const fs::path& gamePath)
        {
            showMenu();

            this->shell.setBasePaths({ gamePath });

            this->gameLuaVm = openre::lua::createLuaVm();
            this->gameLuaVm->setShell(&shell);
            this->gameLuaVm->setLogCallback([](const std::string& s) { std::printf("%s\n", s.c_str()); });
            this->gameLuaVm->run("main");

            this->gameSuspended = false;
        }

        std::filesystem::path getGamesDirectory() const
        {
            auto appdata = std::getenv("APPDATA");
            if (appdata == nullptr)
                return {};

            auto openrePath = fs::u8path(appdata) / "openre";
            auto gamesPath = openrePath / "games";
            if (!fs::is_directory(gamesPath))
                return {};

            return gamesPath;
        }
    };

    void initBios(OpenREShell& shell)
    {
        Bios bios(shell);
        auto initialized = false;
        shell.setUpdate([&initialized, &bios]() {
            if (!initialized)
            {
                initialized = true;
                bios.initialize();
            }
            bios.update();
        });
        shell.run();
    }
}
