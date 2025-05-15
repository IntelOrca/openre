#include "logger.h"
#include <cstdio>

namespace openre::logging
{
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

    std::unique_ptr<Logger> createConsoleLogger(LogVerbosity verbosity)
    {
        return std::make_unique<ConsoleLogger>(verbosity);
    }
}
