#pragma once

#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
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

    namespace detail
    {
        inline void appendFormat(std::string& result, const char*& fmt)
        {
            if (fmt)
                result.append(fmt);
        }

        template<typename T, typename... Rest>
        void appendFormat(std::string& result, const char*& fmt, const T& arg, const Rest&... rest)
        {
            while (fmt && *fmt)
            {
                if (*fmt == '{' && *(fmt + 1) == '}')
                {
                    std::ostringstream oss;
                    oss << arg;
                    result += oss.str();
                    fmt += 2;
                    appendFormat(result, fmt, rest...);
                    return;
                }
                result += *fmt++;
            }
        }

        template<typename... Args>
        std::string formatArgs(const char* format, const Args&... args)
        {
            std::string result;
            const char* f = format;
            appendFormat(result, f, args...);
            return result;
        }
    }

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

        template<typename... Args> void log(LogVerbosity verbosity, const char* format, const Args&... args)
        {
            if (isLogging(verbosity))
            {
                log(verbosity, detail::formatArgs(format, args...).c_str());
            }
        }
    };

    // Global logger instance - never null, defaults to a no-op logger
    extern std::unique_ptr<Logger> gLogger;

    // Initialize the global console logger.
    // Verbosity is taken from the OPENRE_LOG_VERBOSITY env var if set,
    // otherwise falls back to the provided default.
    void initConsoleLogger(LogVerbosity defaultVerbosity);

    // Convenience variadic template functions
    template<typename... Args> void logTrace(const char* format, Args... args)
    {
        gLogger->log(LogVerbosity::none, format, args...);
    }

    template<typename... Args> void logError(const char* format, Args... args)
    {
        gLogger->log(LogVerbosity::error, format, args...);
    }

    template<typename... Args> void logWarning(const char* format, Args... args)
    {
        gLogger->log(LogVerbosity::warning, format, args...);
    }

    template<typename... Args> void logInfo(const char* format, Args... args)
    {
        gLogger->log(LogVerbosity::info, format, args...);
    }

    template<typename... Args> void logVerbose(const char* format, Args... args)
    {
        gLogger->log(LogVerbosity::verbose, format, args...);
    }

    template<typename... Args> void logDebug(const char* format, Args... args)
    {
#ifdef DEBUG
        gLogger->log(LogVerbosity::debug, format, args...);
#endif
    }

    std::unique_ptr<Logger> createConsoleLogger(LogVerbosity verbosity);

    // Parse verbosity from string (for OPENRE_LOG_VERBOSITY env var)
    LogVerbosity parseVerbosity(const char* str, LogVerbosity fallback);
}
