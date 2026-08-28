#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "encos/export.h"
#include "platform/sync.h"

namespace encos {
namespace detail {

struct LogWriterState;

ENCOS_BASE_API std::shared_ptr<LogWriterState> CreateLogWriterState(const std::string& base_name);
ENCOS_BASE_API void EnqueueLogWriterData(const std::shared_ptr<LogWriterState>& state,
                                         std::vector<std::byte> data);
ENCOS_BASE_API void FlushLogWriter(const std::shared_ptr<LogWriterState>& state,
                                   std::vector<std::byte> data);
ENCOS_BASE_API void RethrowLogWriterError(const std::shared_ptr<LogWriterState>& state);
ENCOS_BASE_API void ReportLogWriterError(const std::shared_ptr<LogWriterState>& state) noexcept;
ENCOS_BASE_API const std::string& GetLogWriterFileName(
    const std::shared_ptr<LogWriterState>& state);

template <typename>
struct IsOptional : std::false_type {};

template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {};

template <typename>
struct AlwaysFalse : std::false_type {};

inline std::string EscapeCsvString(std::string_view value) {
    if (value.find_first_of(",\"\r\n") == std::string_view::npos) {
        return std::string(value);
    }

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (const char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

template <typename T>
std::string CsvField(const T& value) {
    using Value = std::decay_t<T>;
    if constexpr (IsOptional<Value>::value) {
        return value ? CsvField(*value) : std::string{};
    } else if constexpr (std::is_same_v<Value, std::string>) {
        return value;
    } else if constexpr (std::is_same_v<Value, std::string_view>) {
        return std::string(value);
    } else if constexpr (std::is_same_v<Value, const char*> || std::is_same_v<Value, char*>) {
        return value ? std::string(value) : std::string{};
    } else if constexpr (std::is_same_v<Value, bool>) {
        return value ? "true" : "false";
    } else if constexpr (std::is_enum_v<Value>) {
        return CsvField(static_cast<std::underlying_type_t<Value>>(value));
    } else if constexpr (std::is_floating_point_v<Value>) {
        if (std::isnan(value)) {
            return "nan";
        }
        if (std::isinf(value)) {
            return value < 0 ? "-inf" : "inf";
        }
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream.precision(std::numeric_limits<Value>::max_digits10);
        stream << value;
        return stream.str();
    } else if constexpr (std::is_integral_v<Value>) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        if constexpr (std::is_same_v<Value, char> || std::is_same_v<Value, signed char> ||
                      std::is_same_v<Value, unsigned char>) {
            stream << static_cast<int>(value);
        } else {
            stream << value;
        }
        return stream.str();
    } else {
        static_assert(AlwaysFalse<Value>::value, "Unsupported LogWriter field type");
    }
}

inline void AppendBytes(std::vector<std::byte>& buffer, std::string_view text) {
    if (text.empty()) {
        return;
    }
    const auto* first = reinterpret_cast<const std::byte*>(text.data());
    buffer.insert(buffer.end(), first, first + text.size());
}

}  // namespace detail

/**
 * @brief 定长列异步 CSV+Zstd 写入器
 * @tparam Columns CSV 列数
 */
template <std::size_t Columns>
class LogWriter {
public:
    /**
     * @brief 创建写入器并写入表头
     * @param base_name 不含 `.csv.zstd` 后缀的基础文件名
     * @param headers 固定列数的表头
     */
    LogWriter(std::string base_name, const std::array<std::string, Columns>& headers)
        : base_name_(std::move(base_name)), state_(detail::CreateLogWriterState(base_name_)) {
        AppendRow(headers);
    }

    ~LogWriter() noexcept {
        try {
            flush();
        } catch (...) {
            detail::ReportLogWriterError(state_);
        }
    }

    LogWriter(const LogWriter&) = delete;
    LogWriter& operator=(const LogWriter&) = delete;
    LogWriter(LogWriter&&) = delete;
    LogWriter& operator=(LogWriter&&) = delete;

    /**
     * @brief 写入一行 CSV 数据
     * @tparam Args 支持的字段类型
     * @param args 各列字段
     */
    template <typename... Args>
    void write(Args&&... args) {
        static_assert(sizeof...(Args) == Columns, "LogWriter row width must match headers");
        platform::LockGuard<platform::Mutex> lock(mutex_);
        detail::RethrowLogWriterError(state_);

        const std::array<std::string, Columns> fields{
            detail::CsvField(std::forward<Args>(args))...};
        AppendRow(fields);
        if (buffer_.size() >= kFlushThreshold) {
            detail::EnqueueLogWriterData(state_, std::move(buffer_));
            buffer_.clear();
        }
    }

    /**
     * @brief 等待当前写入器的全部数据压缩并刷新文件流
     */
    void flush() {
        platform::LockGuard<platform::Mutex> lock(mutex_);
        detail::FlushLogWriter(state_, std::move(buffer_));
        buffer_.clear();
    }

    /**
     * @brief 获取构造时传入的基础文件名
     * @return 不含自动后缀的基础文件名
     */
    const std::string& GetBaseName() const {
        return base_name_;
    }

    /**
     * @brief 获取实际创建的完整文件名
     * @return 包含冲突时间戳和 `.csv.zstd` 后缀的文件名
     */
    const std::string& GetFileName() const {
        return detail::GetLogWriterFileName(state_);
    }

private:
    static constexpr std::size_t kFlushThreshold = 1024U * 1024U;

    void AppendRow(const std::array<std::string, Columns>& fields) {
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i != 0) {
                buffer_.push_back(static_cast<std::byte>(','));
            }
            detail::AppendBytes(buffer_, detail::EscapeCsvString(fields[i]));
        }
        buffer_.push_back(static_cast<std::byte>('\n'));
    }

    std::string base_name_;
    std::shared_ptr<detail::LogWriterState> state_;
    platform::Mutex mutex_;
    std::vector<std::byte> buffer_;
};

}  // namespace encos
