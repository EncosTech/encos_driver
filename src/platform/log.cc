#include "platform/log.h"

#include <algorithm>
#include <cctype>

namespace encos {

namespace {

std::string NormalizeLevelName(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}  // namespace

LogLevel LogLevelFromInt(int value) {
    if (value < static_cast<int>(LogLevel::Trace) || value > static_cast<int>(LogLevel::Off)) {
        return LogLevel::Info;
    }
    return static_cast<LogLevel>(value);
}

int LogLevelToInt(LogLevel level) {
    return static_cast<int>(level);
}

std::string LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return "trace";
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
        case LogLevel::Critical:
            return "critical";
        case LogLevel::Off:
            return "off";
    }
    return "info";
}

LogLevel LogLevelFromString(const std::string& value) {
    const auto normalized = NormalizeLevelName(value);
    if (normalized == "trace") {
        return LogLevel::Trace;
    }
    if (normalized == "debug") {
        return LogLevel::Debug;
    }
    if (normalized == "info") {
        return LogLevel::Info;
    }
    if (normalized == "warn" || normalized == "warning") {
        return LogLevel::Warn;
    }
    if (normalized == "err" || normalized == "error") {
        return LogLevel::Error;
    }
    if (normalized == "critical") {
        return LogLevel::Critical;
    }
    if (normalized == "off") {
        return LogLevel::Off;
    }
    return LogLevel::Info;
}

}  // namespace encos
