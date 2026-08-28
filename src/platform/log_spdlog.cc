#include "platform/log.h"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "platform/log_internal.h"

namespace encos {

namespace {

spdlog::level::level_enum ToSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return spdlog::level::trace;
        case LogLevel::Debug:
            return spdlog::level::debug;
        case LogLevel::Info:
            return spdlog::level::info;
        case LogLevel::Warn:
            return spdlog::level::warn;
        case LogLevel::Error:
            return spdlog::level::err;
        case LogLevel::Critical:
            return spdlog::level::critical;
        case LogLevel::Off:
            return spdlog::level::off;
    }
    return spdlog::level::info;
}

LogLevel FromSpdlogLevel(spdlog::level::level_enum level) {
    switch (level) {
        case spdlog::level::trace:
            return LogLevel::Trace;
        case spdlog::level::debug:
            return LogLevel::Debug;
        case spdlog::level::info:
            return LogLevel::Info;
        case spdlog::level::warn:
            return LogLevel::Warn;
        case spdlog::level::err:
            return LogLevel::Error;
        case spdlog::level::critical:
            return LogLevel::Critical;
        case spdlog::level::off:
            return LogLevel::Off;
        default:
            return LogLevel::Info;
    }
}

}  // namespace

struct Logger::Impl {
    std::string name;
    LogLevel level = LogLevel::Info;
    std::shared_ptr<spdlog::logger> backend;
};

Logger::Logger(std::string name, LogLevel level) : impl_(std::make_unique<Impl>()) {
    impl_->name = std::move(name);
    impl_->level = level;
    if (impl_->name.empty()) {
        impl_->name = "encos";
    }
    auto logger = spdlog::get(impl_->name);
    if (!logger) {
        logger = spdlog::stdout_color_mt<spdlog::async_factory>(impl_->name);
    }
    logger->set_level(ToSpdlogLevel(level));
    impl_->backend = std::move(logger);
}

Logger::~Logger() = default;
Logger::Logger(Logger&&) noexcept = default;
Logger& Logger::operator=(Logger&&) noexcept = default;

void Logger::SetLevel(LogLevel level) {
    impl_->level = level;
    impl_->backend->set_level(ToSpdlogLevel(level));
}

LogLevel Logger::Level() const {
    return FromSpdlogLevel(impl_->backend->level());
}

const std::string& Logger::Name() const {
    return impl_->name;
}

void Logger::Flush() {
    impl_->backend->flush();
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
    if (!detail::ShouldLog(level, impl_->level)) {
        return;
    }
    impl_->backend->log(ToSpdlogLevel(level), "{}", message);
}

LoggerPtr CreateLogger(const std::string& name, LogLevel level) {
    return std::make_shared<Logger>(name, level);
}

void ClearLoggers() {
    spdlog::drop_all();
    spdlog::shutdown();
}

}  // namespace encos
