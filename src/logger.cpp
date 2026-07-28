#include "logger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace openre::logging
{
    std::unique_ptr<Logger> gLogger = std::make_unique<Logger>();

    class ConsoleLogger : public Logger
    {
    private:
        LogVerbosity verbosity;

    public:
        ConsoleLogger(LogVerbosity verbosity)
            : verbosity(verbosity)
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
            std::fputs(s, f);
            std::fputc('\n', f);
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
