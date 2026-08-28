#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

#include "encos/export.h"

#ifndef ENCOS_ENABLE_SPDLOG
#define ENCOS_ENABLE_SPDLOG 0
#endif

namespace encos {

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Critical = 5,
    Off = 6,
};

class ENCOS_BASE_API Logger {
public:
    Logger(std::string name, LogLevel level);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) noexcept;
    Logger& operator=(Logger&&) noexcept;

    void SetLevel(LogLevel level);
    LogLevel Level() const;
    const std::string& Name() const;
    void Flush();

    template <typename... Args>
    void trace(const std::string& fmt, Args&&... args) {
        LogFormatted(LogLevel::Trace, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(const std::string& fmt, Args&&... args) {
#if ENCOS_ENABLE_SPDLOG
        LogFormatted(LogLevel::Debug, fmt, std::forward<Args>(args)...);
#else
        (void) fmt;
        (void) sizeof...(args);
#endif
    }

    template <typename... Args>
    void info(const std::string& fmt, Args&&... args) {
        LogFormatted(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(const std::string& fmt, Args&&... args) {
        LogFormatted(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(const std::string& fmt, Args&&... args) {
        LogFormatted(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void critical(const std::string& fmt, Args&&... args) {
        LogFormatted(LogLevel::Critical, fmt, std::forward<Args>(args)...);
    }

private:
    template <typename... Args>
    void LogFormatted(LogLevel level, const std::string& fmt, Args&&... args) {
        LogMessage(level, Format(fmt, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static std::string Format(const std::string& fmt, Args&&... args) {
        std::ostringstream os;
        FormatInto(os, fmt, 0, std::forward<Args>(args)...);
        return os.str();
    }

    static void FormatInto(std::ostream& os, const std::string& fmt, std::size_t start) {
        os << fmt.substr(start);
    }

    template <typename T, typename... Rest>
    static void FormatInto(std::ostream& os, const std::string& fmt, std::size_t start, T&& value,
                           Rest&&... rest) {
        const auto open = fmt.find('{', start);
        const auto close = open == std::string::npos ? std::string::npos : fmt.find('}', open);
        if (open == std::string::npos || close == std::string::npos) {
            os << fmt.substr(start);
            os << ' ' << std::forward<T>(value);
            ((os << ' ' << std::forward<Rest>(rest)), ...);
            return;
        }

        os << fmt.substr(start, open - start);
        os << std::forward<T>(value);
        FormatInto(os, fmt, close + 1, std::forward<Rest>(rest)...);
    }

    void LogMessage(LogLevel level, const std::string& message);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

using LoggerPtr = std::shared_ptr<Logger>;

ENCOS_BASE_API LoggerPtr CreateLogger(const std::string& name, LogLevel level = LogLevel::Info);
ENCOS_BASE_API LogLevel LogLevelFromInt(int value);
ENCOS_BASE_API int LogLevelToInt(LogLevel level);
ENCOS_BASE_API std::string LogLevelToString(LogLevel level);
ENCOS_BASE_API LogLevel LogLevelFromString(const std::string& value);
ENCOS_BASE_API void ClearLoggers();

}  // namespace encos

#if ENCOS_ENABLE_SPDLOG
#define ENCOS_LOG_DEBUG(logger, ...)      \
    do {                                  \
        if ((logger)) {                   \
            (logger)->debug(__VA_ARGS__); \
        }                                 \
    } while (false)
#else
#define ENCOS_LOG_DEBUG(logger, ...) \
    do {                             \
    } while (false)
#endif
