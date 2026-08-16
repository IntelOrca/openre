#include "logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace openre::logging
{
    namespace
    {
        // ANSI escape sequences for console colours.
        constexpr const char* kColorReset = "\x1b[0m";
        constexpr const char* kColorError = "\x1b[1;31m";   // bold red
        constexpr const char* kColorWarning = "\x1b[1;33m"; // bold yellow
        constexpr const char* kColorInfo = "\x1b[36m";      // cyan
        constexpr const char* kColorDebug = "\x1b[90m";     // grey

        const char* levelTag(LogVerbosity verbosity)
        {
            switch (verbosity)
            {
            case LogVerbosity::none: return "N/A";
            case LogVerbosity::error: return "ERR";
            case LogVerbosity::warning: return "WRN";
            case LogVerbosity::info: return "INF";
            case LogVerbosity::debug: return "DBG";
            }
            return "???";
        }

        const char* levelColor(LogVerbosity verbosity)
        {
            switch (verbosity)
            {
            case LogVerbosity::error: return kColorError;
            case LogVerbosity::warning: return kColorWarning;
            case LogVerbosity::info: return kColorInfo;
            case LogVerbosity::debug: return kColorDebug;
            case LogVerbosity::none: return "";
            }
            return "";
        }

        void getLocalTime(std::tm& out)
        {
            const std::time_t t = std::time(nullptr);
#ifdef _MSC_VER
            localtime_s(&out, &t);
#else
            localtime_r(&t, &out);
#endif
        }
    }

    std::unique_ptr<Logger> gLogger = std::make_unique<Logger>();

    class ConsoleLogger : public Logger
    {
    private:
        LogVerbosity verbosity;
        bool colorStdout;
        bool colorStderr;

        static bool enableColor(FILE* stream)
        {
#ifdef _WIN32
            DWORD mode = 0;
            HANDLE h = GetStdHandle(stream == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
            if (h == INVALID_HANDLE_VALUE || h == nullptr)
                return false;
            if (!GetConsoleMode(h, &mode))
                return false; // redirected, not a console
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(h, mode);
            return true;
#else
            return isatty(fileno(stream)) != 0;
#endif
        }

    public:
        ConsoleLogger(LogVerbosity verbosity)
            : verbosity(verbosity)
            , colorStdout(enableColor(stdout))
            , colorStderr(enableColor(stderr))
        {
        }

        ~ConsoleLogger() override {}

        LogVerbosity getVerbosity() override
        {
            return this->verbosity;
        }

        void log(LogVerbosity verbosity, const char* s) override
        {
            if (verbosity == LogVerbosity::none)
                return;
            if (!isLogging(verbosity))
                return;

            auto f = verbosity == LogVerbosity::error ? stderr : stdout;
            const bool color = (f == stderr) ? colorStderr : colorStdout;

            std::tm tm{};
            getLocalTime(tm);
            char timestamp[16];
            std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &tm);

            if (color)
            {
                std::fprintf(f, "%s[%s %s] %s%s\n", levelColor(verbosity), timestamp, levelTag(verbosity), s, kColorReset);
            }
            else
            {
                std::fprintf(f, "[%s %s] %s\n", timestamp, levelTag(verbosity), s);
            }
            std::fflush(f);
        }
    };

    static LogVerbosity verbosityFromEnv(LogVerbosity fallback)
    {
        const char* env = std::getenv("OPENRE_LOG_VERBOSITY");
        if (env == nullptr)
            return fallback;
        return parseVerbosity(env, fallback);
    }

    LogVerbosity parseVerbosity(const char* str, LogVerbosity fallback)
    {
        if (std::strcmp(str, "none") == 0)
            return LogVerbosity::none;
        if (std::strcmp(str, "error") == 0)
            return LogVerbosity::error;
        if (std::strcmp(str, "warning") == 0)
            return LogVerbosity::warning;
        if (std::strcmp(str, "info") == 0)
            return LogVerbosity::info;
        if (std::strcmp(str, "debug") == 0)
            return LogVerbosity::debug;
        return fallback;
    }

    std::unique_ptr<Logger> createConsoleLogger(LogVerbosity verbosity)
    {
        return std::make_unique<ConsoleLogger>(verbosity);
    }

    void initConsoleLogger(LogVerbosity defaultVerbosity)
    {
        auto verbosity = verbosityFromEnv(defaultVerbosity);
        gLogger = std::make_unique<ConsoleLogger>(verbosity);
    }
}
