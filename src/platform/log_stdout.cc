#include <iostream>
#include <mutex>

#include "platform/log.h"
#include "platform/log_internal.h"

namespace encos {

namespace {

std::mutex fallback_log_mutex;

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
    std::lock_guard<std::mutex> lock(fallback_log_mutex);
    std::cout.flush();
    std::cerr.flush();
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
    if (!detail::ShouldLog(level, impl_->level)) {
        return;
    }
    std::lock_guard<std::mutex> lock(fallback_log_mutex);
    auto& stream = level >= LogLevel::Error ? std::cerr : std::cout;
    stream << '[' << LogLevelToString(level) << "] ";
    if (!impl_->name.empty()) {
        stream << impl_->name << ": ";
    }
    stream << message << '\n';
}

LoggerPtr CreateLogger(const std::string& name, LogLevel level) {
    return std::make_shared<Logger>(name, level);
}

void ClearLoggers() {
    // Stdout backend is stateless; nothing to clear.
}

}  // namespace encos
