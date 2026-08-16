#include "cmdline.h"
#include "logger.h"
#include "player.h"

#include <cctype>
#include <cstring>
#include <vector>

namespace openre
{
    static CmdlineOptions sCmdline;

    const CmdlineOptions& cmdline_options()
    {
        return sCmdline;
    }

    bool cmdline_start_requested()
    {
        return sCmdline.startRequested();
    }

    bool cmdline_start_active()
    {
        return sCmdline.startActive;
    }

    void cmdline_mark_start()
    {
        sCmdline.startActive = true;
    }

    void cmdline_consume_start()
    {
        sCmdline.startActive = false;
    }

    bool cmdline_save_requested()
    {
        return sCmdline.saveRequested;
    }

    const char* cmdline_save_path()
    {
        return sCmdline.savePath.c_str();
    }

    int cmdline_player()
    {
        return sCmdline.player;
    }

    int cmdline_stage()
    {
        return sCmdline.stage;
    }

    int cmdline_room()
    {
        return sCmdline.room;
    }

    int cmdline_scenario()
    {
        return sCmdline.scenario;
    }

    // Splits the command line into tokens, preserving quoted arguments such as
    // paths with spaces.
    static void cmdline_tokenize(const char* line, std::vector<std::string>& tokens)
    {
        const char* p = line;
        while (*p)
        {
            while (*p && isspace((unsigned char)*p))
                p++;
            if (!*p)
                break;
            const bool quoted = *p == '"';
            const char* start = quoted ? p + 1 : p;
            const char* end = start;
            while (*end && (quoted ? *end != '"' : !isspace((unsigned char)*end)))
                end++;
            tokens.emplace_back(start, end);
            p = *end ? end + 1 : end;
        }
    }

    // Parses a -p value: a decimal player id or a name alias.
    static bool cmdline_parse_player(const std::string& value, int& player)
    {
        if (value.empty())
            return false;
        if (isdigit((unsigned char)value[0]) || (value[0] == '-' && value.size() > 1 && isdigit((unsigned char)value[1])))
        {
            player = std::strtol(value.c_str(), nullptr, 10);
            return true;
        }

        auto iequals = [](const std::string& a, const char* b) {
            size_t n = std::strlen(b);
            if (a.size() != n)
                return false;
            for (size_t i = 0; i < n; i++)
            {
                if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
                    return false;
            }
            return true;
        };

        if (iequals(value, "leon"))
        {
            player = player::PLD_LEON_0;
            return true;
        }
        if (iequals(value, "claire"))
        {
            player = player::PLD_CLAIRE_0;
            return true;
        }
        return false;
    }

    // Parses a -r value: the community room code (stage+1 followed by the room
    // number in hex).
    static bool cmdline_parse_room(const std::string& value, int& stage, int& room)
    {
        if (value.size() < 2)
            return false;
        if (value[0] >= '1' && value[0] <= '7' && isxdigit((unsigned char)value[1]))
        {
            stage = value[0] - '1';
            room = 0;
            for (size_t i = 1; i < value.size() && isxdigit((unsigned char)value[i]); i++)
            {
                const char c = value[i];
                room = room * 16 + (c <= '9' ? c - '0' : tolower((unsigned char)c) - 'a' + 10);
            }
            return true;
        }
        return false;
    }

    // Parses a -s value: 0/a/A = scenario A, 1/b/B = scenario B.
    static bool cmdline_parse_scenario(const std::string& value, int& scenario)
    {
        if (value == "0" || value == "a" || value == "A")
        {
            scenario = 0;
            return true;
        }
        if (value == "1" || value == "b" || value == "B")
        {
            scenario = 1;
            return true;
        }
        return false;
    }

    void cmdline_parse(const char* lpCmdLine)
    {
        if (!lpCmdLine)
            return;

        std::vector<std::string> tokens;
        cmdline_tokenize(lpCmdLine, tokens);

        for (size_t i = 0; i < tokens.size(); i++)
        {
            const auto& token = tokens[i];
            const bool isSwitch = token.size() >= 2 && (token[0] == '-' || token[0] == '/');
            if (!isSwitch)
            {
                // First positional argument is the save file.
                if (!sCmdline.saveRequested)
                {
                    sCmdline.savePath = token;
                    sCmdline.saveRequested = true;
                }
                continue;
            }

            const char opt = tolower((unsigned char)token[1]);
            // Attached values (e.g. -p0, -r10C, -sb) are supported alongside
            // space separated values (e.g. -p 0, -r 10C, -s b).
            std::string value;
            if (token.size() > 2)
            {
                value = token.substr(2);
            }
            else if (i + 1 < tokens.size())
            {
                value = tokens[++i];
            }
            else
            {
                continue;
            }

            if (opt == 'p')
            {
                int player;
                if (cmdline_parse_player(value, player))
                    sCmdline.player = player;
            }
            else if (opt == 'r')
            {
                int stage, room;
                if (cmdline_parse_room(value, stage, room))
                {
                    sCmdline.stage = stage;
                    sCmdline.room = room;
                }
            }
            else if (opt == 's')
            {
                int scenario;
                if (cmdline_parse_scenario(value, scenario))
                    sCmdline.scenario = scenario;
            }
        }

        if (sCmdline.startRequested())
        {
            logging::logInfo(
                "[cmdline] start requested: save='{}' player={} stage={} room={} scenario={}",
                sCmdline.savePath,
                sCmdline.player,
                sCmdline.stage,
                sCmdline.room,
                sCmdline.scenario);
        }
    }
}
