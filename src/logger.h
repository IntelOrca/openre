#pragma once

#include <cstdio>
#include <memory>
#include <string_view>

namespace openre::logging
{
    enum class LogVerbosity
    {
        none,
        error,
        warning,
        info,
        verbose,
        debug,
    };

    class Logger
    {
    public:
        virtual ~Logger() = default;

        virtual LogVerbosity getVerbosity()
        {
            return LogVerbosity::none;
        }

        virtual void log(LogVerbosity verbosity, const char* s) {}

        bool isLogging(LogVerbosity verbosity)
        {
            return getVerbosity() >= verbosity;
        }

        template<typename... Args> void log(LogVerbosity verbosity, const char* format, Args... args)
        {
            if (isLogging(verbosity))
            {
                auto size = std::snprintf(nullptr, 0, format, args...);
                if (size > 0)
                {
                    size++;
                    std::unique_ptr<char[]> buf(new char[size]);
                    std::snprintf(buf.get(), size, format, args...);
                    log(verbosity, buf.get());
                }
            }
        }
    };

    std::unique_ptr<Logger> createConsoleLogger(LogVerbosity verbosity);
}
