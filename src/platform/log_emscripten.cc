#include <emscripten/console.h>
#include <mutex>
#include <string>

#include "platform/log.h"
#include "platform/log_internal.h"

namespace encos {

namespace {

std::mutex fallback_log_mutex;

void ConsoleLog(LogLevel level, const std::string& text) {
    switch (level) {
        case LogLevel::Warn:
            emscripten_console_warn(text.c_str());
            break;
        case LogLevel::Error:
        case LogLevel::Critical:
            emscripten_console_error(text.c_str());
            break;
        default:
            emscripten_console_log(text.c_str());
            break;
    }
}

}  // namespace

struct Logger::Impl {
    std::string name;
    LogLevel level = LogLevel::Info;
};

Logger::Logger(std::string name, LogLevel level) : impl_(std::make_unique<Impl>()) {
    impl_->name = std::move(name);
    impl_->level = level;
}

Logger::~Logger() = default;
Logger::Logger(Logger&&) noexcept = default;
Logger& Logger::operator=(Logger&&) noexcept = default;

void Logger::SetLevel(LogLevel level) {
    impl_->level = level;
}

LogLevel Logger::Level() const {
    return impl_->level;
}

const std::string& Logger::Name() const {
    return impl_->name;
}

void Logger::Flush() {
    // Emscripten console output is synchronous; nothing to flush.
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
    if (!detail::ShouldLog(level, impl_->level)) {
        return;
    }
    std::lock_guard<std::mutex> lock(fallback_log_mutex);
    std::string text = std::string("[") + LogLevelToString(level) + "] ";
    if (!impl_->name.empty()) {
        text += impl_->name + ": ";
    }
    text += message;
    ConsoleLog(level, text);
}

LoggerPtr CreateLogger(const std::string& name, LogLevel level) {
    return std::make_shared<Logger>(name, level);
}

void ClearLoggers() {
    // Emscripten console backend is stateless; nothing to clear.
}

}  // namespace encos
