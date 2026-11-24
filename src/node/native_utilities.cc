// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT
#include "node/native_utilities.h"

#include <mutex>
#include <unordered_map>

#include "node/native_devices.h"
#include "utils/log_writer.h"

namespace encos::node {
namespace {
struct Writer {
    std::shared_ptr<detail::LogWriterState> state;
    std::string base_name;
    std::size_t columns = 0;
    std::vector<std::byte> buffer;
    ~Writer() {
        if (!state)
            return;
        try {
            detail::FlushLogWriter(state, std::move(buffer));
        } catch (...) {
            detail::ReportLogWriterError(state);
        }
    }
    void Write(const Json& fields) {
        if (!fields.is_array() || fields.size() != columns)
            throw std::invalid_argument("log row width must match headers");
        detail::RethrowLogWriterError(state);
        std::vector<std::byte> row;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i != 0)
                row.push_back(static_cast<std::byte>(','));
            const auto& field = fields[i];
            std::string text;
            if (field.is_null())
                text = "";
            else if (field.is_string())
                text = field.get<std::string>();
            else if (field.is_boolean())
                text = detail::CsvField(field.get<bool>());
            else if (field.is_number_float())
                text = detail::CsvField(field.get<double>());
            else if (field.is_number_unsigned())
                text = detail::CsvField(field.get<uint64_t>());
            else if (field.is_number_integer())
                text = detail::CsvField(field.get<int64_t>());
            else
                throw std::invalid_argument("log fields must be scalar values");
            detail::AppendBytes(row, detail::EscapeCsvString(text));
        }
        row.push_back(static_cast<std::byte>('\n'));
        buffer.insert(buffer.end(), row.begin(), row.end());
        if (buffer.size() >= 1024U * 1024U) {
            detail::EnqueueLogWriterData(state, std::move(buffer));
            buffer.clear();
        }
    }
};
struct Utilities {
    uint32_t next = 1;
    std::unordered_map<uint32_t, LoggerPtr> loggers;
    std::unordered_map<uint32_t, std::unique_ptr<Writer>> writers;
    uint32_t Allocate() {
        if (next == 0)
            throw std::overflow_error("utility handles exhausted");
        return next++;
    }
};
std::mutex utility_mutex;
std::unordered_map<wasm::RuntimeStore*, Utilities> utilities;
LogLevel ParseLevel(const Json& j) {
    int value = CheckedInteger<int>(j);
    if (value < 0 || value > 6)
        throw std::invalid_argument("invalid log level");
    return static_cast<LogLevel>(value);
}
}  // namespace
void ClearNativeUtilities(wasm::RuntimeStore& store) noexcept {
    std::lock_guard<std::mutex> lock(utility_mutex);
    utilities.erase(&store);
}
Json InvokeLogUtility(wasm::RuntimeStore& store, const std::string& op, const Json& a) {
    std::lock_guard<std::mutex> lock(utility_mutex);
    auto& u = utilities[&store];
    if (op == "utility.createLogger") {
        auto logger = CreateLogger(a.at("name"), ParseLevel(a.value("level", Json(2))));
        const auto id = u.Allocate();
        u.loggers.emplace(id, std::move(logger));
        return id;
    }
    if (op == "utility.createLogWriter") {
        const auto& headers = a.at("headers");
        if (!headers.is_array())
            throw std::invalid_argument("headers must be an array");
        for (const auto& header : headers)
            if (!header.is_string())
                throw std::invalid_argument("headers must be strings");
        auto writer = std::make_unique<Writer>();
        writer->base_name = a.at("baseName");
        writer->columns = headers.size();
        writer->state = detail::CreateLogWriterState(writer->base_name);
        writer->Write(headers);
        const auto id = u.Allocate();
        u.writers.emplace(id, std::move(writer));
        return id;
    }
    const auto id = CheckedInteger<uint32_t>(a.at("handle"));
    if (op.rfind("utility.logger", 0) == 0) {
        auto it = u.loggers.find(id);
        if (it == u.loggers.end())
            throw wasm::ErrorState{wasm::ErrorCode::InvalidHandle, "invalid logger handle"};
        auto& logger = *it->second;
        if (op == "utility.loggerLevel")
            return static_cast<int>(logger.Level());
        if (op == "utility.loggerName")
            return logger.Name();
        if (op == "utility.loggerSetLevel")
            logger.SetLevel(ParseLevel(a.at("level")));
        else if (op == "utility.loggerFlush")
            logger.Flush();
        else if (op == "utility.loggerDispose") {
            logger.Flush();
            u.loggers.erase(it);
        } else if (op == "utility.loggerLog") {
            auto message = a.at("message").get<std::string>();
            switch (ParseLevel(a.at("level"))) {
                case LogLevel::Trace:
                    logger.trace(message);
                    break;
                case LogLevel::Debug:
                    logger.debug(message);
                    break;
                case LogLevel::Info:
                    logger.info(message);
                    break;
                case LogLevel::Warn:
                    logger.warn(message);
                    break;
                case LogLevel::Error:
                    logger.error(message);
                    break;
                case LogLevel::Critical:
                    logger.critical(message);
                    break;
                case LogLevel::Off:
                    break;
            }
        } else
            throw std::invalid_argument("unknown logger operation");
        return nullptr;
    }
    auto it = u.writers.find(id);
    if (it == u.writers.end())
        throw wasm::ErrorState{wasm::ErrorCode::InvalidHandle, "invalid writer handle"};
    auto& writer = *it->second;
    if (op == "utility.writerBaseName")
        return writer.base_name;
    if (op == "utility.writerFileName")
        return detail::GetLogWriterFileName(writer.state);
    if (op == "utility.writerWrite")
        writer.Write(a.at("fields"));
    else if (op == "utility.writerFlush" || op == "utility.writerDispose") {
        detail::FlushLogWriter(writer.state, std::move(writer.buffer));
        writer.buffer.clear();
        if (op == "utility.writerDispose")
            u.writers.erase(it);
    } else
        throw std::invalid_argument("unknown writer operation");
    return nullptr;
}
}  // namespace encos::node
