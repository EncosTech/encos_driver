#include "utils/log_writer.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unordered_map>

#include "platform/log.h"
#include "utils/log_writer_test_hook.h"

#if !defined(__EMSCRIPTEN__)
#include <zstd.h>
#endif

namespace encos::detail {

#if defined(__EMSCRIPTEN__)

struct LogWriterState {};

std::shared_ptr<LogWriterState> CreateLogWriterState(const std::string&) {
    throw std::runtime_error("File logging is unsupported on Emscripten");
}

void EnqueueLogWriterData(const std::shared_ptr<LogWriterState>&, std::vector<std::byte>) {
    throw std::runtime_error("File logging is unsupported on Emscripten");
}

void FlushLogWriter(const std::shared_ptr<LogWriterState>&, std::vector<std::byte>) {
    throw std::runtime_error("File logging is unsupported on Emscripten");
}

void RethrowLogWriterError(const std::shared_ptr<LogWriterState>&) {}

void ReportLogWriterError(const std::shared_ptr<LogWriterState>&) noexcept {}

const std::string& GetLogWriterFileName(const std::shared_ptr<LogWriterState>&) {
    static const std::string empty;
    return empty;
}

void FailNextLogWriterTaskForTesting(const std::string&) {}

void FailNextLogWriterInteractionForTesting() {}

#else

namespace {

platform::Mutex injected_failures_mutex;
std::unordered_map<std::string, std::size_t> injected_failures;
std::atomic<std::size_t> injected_interaction_failures{0};

bool ConsumeInjectedFailure(const std::string& file_name) {
    platform::LockGuard<platform::Mutex> lock(injected_failures_mutex);
    const auto it = injected_failures.find(file_name);
    if (it == injected_failures.end()) {
        return false;
    }
    if (--it->second == 0) {
        injected_failures.erase(it);
    }
    return true;
}

bool ConsumeInjectedInteractionFailure() {
    auto count = injected_interaction_failures.load();
    while (count != 0) {
        if (injected_interaction_failures.compare_exchange_weak(count, count - 1)) {
            return true;
        }
    }
    return false;
}

std::runtime_error FileError(const std::string& action, const std::string& file_name) {
    return std::runtime_error(action + " '" + file_name + "': " + std::strerror(errno));
}

std::pair<std::string, std::FILE*> CreateOutputFile(const std::string& base_name) {
    std::string candidate = base_name + ".csv.zstd";
    if (std::FILE* file = std::fopen(candidate.c_str(), "wbx")) {
        return {candidate, file};
    }
    if (errno != EEXIST) {
        throw FileError("Failed to create log file", candidate);
    }

    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    for (;;) {
        candidate = base_name + "_" + std::to_string(timestamp) + ".csv.zstd";
        if (std::FILE* file = std::fopen(candidate.c_str(), "wbx")) {
            return {candidate, file};
        }
        if (errno != EEXIST) {
            throw FileError("Failed to create log file", candidate);
        }
        ++timestamp;
    }
}

}  // namespace

struct LogWriterState {
    explicit LogWriterState(std::string file_name_in, std::FILE* file_in)
        : file_name(std::move(file_name_in)), file(file_in) {}

    ~LogWriterState() {
        if (file) {
            std::fclose(file);
        }
    }

    std::string file_name;
    std::FILE* file = nullptr;
    platform::Mutex mutex;
    std::condition_variable_any completed_cv;
    std::uint64_t submitted = 0;
    std::uint64_t completed = 0;
    std::exception_ptr error;
};

namespace {

struct LogWriteTask {
    std::shared_ptr<LogWriterState> state;
    std::vector<std::byte> data;
    std::uint64_t sequence = 0;
    bool flush = false;
};

class LogWriteWorker {
public:
    LogWriteWorker()
        : thread_([this]() {
              Run();
          }) {}

    std::uint64_t Submit(const std::shared_ptr<LogWriterState>& state, std::vector<std::byte> data,
                         bool flush) {
        std::uint64_t sequence = 0;
        {
            platform::LockGuard<platform::Mutex> state_lock(state->mutex);
            sequence = ++state->submitted;
        }
        {
            platform::LockGuard<platform::Mutex> lock(mutex_);
            tasks_.push_back({state, std::move(data), sequence, flush});
        }
        cv_.notify_one();
        return sequence;
    }

private:
    void Run() noexcept {
        for (;;) {
            LogWriteTask task;
            {
                platform::UniqueLock<platform::Mutex> lock(mutex_);
                cv_.wait(lock, [this]() {
                    return !tasks_.empty();
                });
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            Process(task);
        }
    }

    static void Process(const LogWriteTask& task) noexcept {
        std::exception_ptr failure;
        bool already_failed = false;
        {
            platform::LockGuard<platform::Mutex> lock(task.state->mutex);
            already_failed = static_cast<bool>(task.state->error);
        }

        if (!already_failed) {
            try {
                if (ConsumeInjectedFailure(task.state->file_name)) {
                    throw std::runtime_error("Injected log writer task failure");
                }
                if (!task.data.empty()) {
                    std::vector<std::byte> compressed(ZSTD_compressBound(task.data.size()));
                    const auto size =
                        ZSTD_compress(compressed.data(), compressed.size(), task.data.data(),
                                      task.data.size(), ZSTD_CLEVEL_DEFAULT);
                    if (ZSTD_isError(size)) {
                        throw std::runtime_error(std::string("Zstd compression failed: ") +
                                                 ZSTD_getErrorName(size));
                    }
                    if (std::fwrite(compressed.data(), 1, size, task.state->file) != size) {
                        throw FileError("Failed to write log file", task.state->file_name);
                    }
                }
                if (task.flush && std::fflush(task.state->file) != 0) {
                    throw FileError("Failed to flush log file", task.state->file_name);
                }
            } catch (...) {
                failure = std::current_exception();
            }
        }

        {
            platform::LockGuard<platform::Mutex> lock(task.state->mutex);
            if (failure && !task.state->error) {
                task.state->error = failure;
            }
            task.state->completed = task.sequence;
        }
        task.state->completed_cv.notify_all();
    }

    platform::Mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<LogWriteTask> tasks_;
    std::thread thread_;
};

LogWriteWorker& Worker() {
    // 电机日志在进程级管理器析构时仍会提交最终刷盘任务。
    static auto* const worker = new LogWriteWorker();
    return *worker;
}

void WaitFor(const std::shared_ptr<LogWriterState>& state, std::uint64_t sequence) {
    platform::UniqueLock<platform::Mutex> lock(state->mutex);
    state->completed_cv.wait(lock, [&]() {
        return state->completed >= sequence;
    });
    if (state->error) {
        std::rethrow_exception(state->error);
    }
}

}  // namespace

std::shared_ptr<LogWriterState> CreateLogWriterState(const std::string& base_name) {
    auto [file_name, file] = CreateOutputFile(base_name);
    std::unique_ptr<std::FILE, int (*)(std::FILE*)> file_guard(file, &std::fclose);
    (void) Worker();
    auto state = std::make_shared<LogWriterState>(std::move(file_name), file_guard.get());
    (void) file_guard.release();
    return state;
}

void EnqueueLogWriterData(const std::shared_ptr<LogWriterState>& state,
                          std::vector<std::byte> data) {
    (void) Worker().Submit(state, std::move(data), false);
}

void FlushLogWriter(const std::shared_ptr<LogWriterState>& state, std::vector<std::byte> data) {
    const auto sequence = Worker().Submit(state, std::move(data), true);
    WaitFor(state, sequence);
}

void RethrowLogWriterError(const std::shared_ptr<LogWriterState>& state) {
    platform::LockGuard<platform::Mutex> lock(state->mutex);
    if (!state->error && ConsumeInjectedInteractionFailure()) {
        state->error =
            std::make_exception_ptr(std::runtime_error("Injected log writer interaction failure"));
    }
    if (state->error) {
        std::rethrow_exception(state->error);
    }
}

void ReportLogWriterError(const std::shared_ptr<LogWriterState>& state) noexcept {
    try {
        RethrowLogWriterError(state);
    } catch (const std::exception& error) {
        try {
            CreateLogger("LogWriter", LogLevel::Error)->error("{}", error.what());
        } catch (...) {}
    } catch (...) {}
}

const std::string& GetLogWriterFileName(const std::shared_ptr<LogWriterState>& state) {
    return state->file_name;
}

void FailNextLogWriterTaskForTesting(const std::string& file_name) {
    platform::LockGuard<platform::Mutex> lock(injected_failures_mutex);
    ++injected_failures[file_name];
}

void FailNextLogWriterInteractionForTesting() {
    injected_interaction_failures.fetch_add(1);
}

#endif

}  // namespace encos::detail
