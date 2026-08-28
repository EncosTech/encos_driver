#include "utils/log_writer.h"

#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <zstd.h>

#include "utils/log_writer_test_hook.h"

namespace encos {
namespace {

namespace fs = std::filesystem;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("encos-log-writer-test-" + std::to_string(suffix));
        fs::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& Path() const {
        return path_;
    }

private:
    fs::path path_;
};

std::string DecompressFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    const std::vector<char> compressed{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};

    ZSTD_DStream* stream = ZSTD_createDStream();
    EXPECT_NE(stream, nullptr);
    if (!stream) {
        return {};
    }
    EXPECT_FALSE(ZSTD_isError(ZSTD_initDStream(stream)));

    std::string output;
    std::array<char, 4096> chunk{};
    ZSTD_inBuffer in_buffer{compressed.data(), compressed.size(), 0};
    while (in_buffer.pos < in_buffer.size) {
        ZSTD_outBuffer out_buffer{chunk.data(), chunk.size(), 0};
        const auto result = ZSTD_decompressStream(stream, &out_buffer, &in_buffer);
        EXPECT_FALSE(ZSTD_isError(result)) << ZSTD_getErrorName(result);
        if (ZSTD_isError(result)) {
            break;
        }
        output.append(chunk.data(), out_buffer.pos);
    }

    ZSTD_freeDStream(stream);
    return output;
}

std::vector<char> ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::size_t CountZstdFrames(const fs::path& path) {
    const auto compressed = ReadFile(path);
    std::size_t offset = 0;
    std::size_t count = 0;
    while (offset < compressed.size()) {
        const auto size =
            ZSTD_findFrameCompressedSize(compressed.data() + offset, compressed.size() - offset);
        EXPECT_FALSE(ZSTD_isError(size)) << ZSTD_getErrorName(size);
        if (ZSTD_isError(size)) {
            break;
        }
        offset += size;
        ++count;
    }
    EXPECT_EQ(offset, compressed.size());
    return count;
}

enum class ExampleMode : int { Active = 7 };

TEST(LogWriterTests, WritesHeaderAndTypedCsvRow) {
    TemporaryDirectory directory;
    const auto base_name = (directory.Path() / "typed").string();
    std::string file_name;

    {
        LogWriter<6> writer(base_name, {"text", "empty", "enabled", "mode", "value", "missing"});
        writer.write("a,\"b", std::optional<int>{}, true, ExampleMode::Active, 1.25,
                     std::numeric_limits<double>::quiet_NaN());
        writer.flush();
        file_name = writer.GetFileName();

        EXPECT_EQ(writer.GetBaseName(), base_name);
        EXPECT_EQ(file_name, base_name + ".csv.zstd");
    }

    EXPECT_EQ(DecompressFile(file_name),
              "text,empty,enabled,mode,value,missing\n\"a,\"\"b\",,true,7,1.25,nan\n");
}

TEST(LogWriterTests, PreservesExistingFileAndIncrementsTimestampCandidate) {
    TemporaryDirectory directory;
    const auto base_path = directory.Path() / "collision";
    const auto unsuffixed = base_path.string() + ".csv.zstd";
    {
        std::ofstream existing(unsuffixed);
        existing << "keep";
    }

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    for (auto timestamp = now; timestamp <= now + 20; ++timestamp) {
        std::ofstream occupied(base_path.string() + "_" + std::to_string(timestamp) + ".csv.zstd");
    }

    std::string resolved;
    {
        LogWriter<1> writer(base_path.string(), {"value"});
        writer.write(1);
        writer.flush();
        resolved = writer.GetFileName();
    }

    const auto existing = ReadFile(unsuffixed);
    EXPECT_EQ(std::string(existing.begin(), existing.end()), "keep");
    EXPECT_NE(resolved, unsuffixed);
    EXPECT_TRUE(fs::exists(resolved));
    const auto suffix_start = resolved.rfind('_');
    const auto suffix_end = resolved.rfind(".csv.zstd");
    ASSERT_NE(suffix_start, std::string::npos);
    ASSERT_NE(suffix_end, std::string::npos);
    const auto timestamp =
        std::stoll(resolved.substr(suffix_start + 1, suffix_end - suffix_start - 1));
    EXPECT_GT(timestamp, now + 20);
}

TEST(LogWriterTests, WritesMultipleFramesAndFlushesOnDestruction) {
    TemporaryDirectory directory;
    const auto base_name = (directory.Path() / "frames").string();
    const std::string payload(600U * 1024U, 'x');
    std::string file_name;

    {
        LogWriter<1> writer(base_name, {"payload"});
        writer.write(payload);
        writer.write(payload);
        writer.write("tail");
        file_name = writer.GetFileName();
    }

    const auto decompressed = DecompressFile(file_name);
    EXPECT_EQ(CountZstdFrames(file_name), 2U);
    EXPECT_EQ(decompressed.size(), std::string("payload\n").size() + payload.size() * 2U +
                                       std::string("\n\ntail\n").size());
    EXPECT_EQ(decompressed.compare(0, std::string("payload\n").size(), "payload\n"), 0);
    EXPECT_EQ(decompressed.compare(decompressed.size() - std::string("\ntail\n").size(),
                                   std::string("\ntail\n").size(), "\ntail\n"),
              0);
}

TEST(LogWriterTests, SerializesConcurrentProducersWithoutLosingRows) {
    TemporaryDirectory directory;
    const auto base_name = (directory.Path() / "concurrent").string();
    std::string file_name;

    {
        LogWriter<2> writer(base_name, {"thread", "row"});
        std::vector<std::thread> producers;
        for (int thread = 0; thread < 4; ++thread) {
            producers.emplace_back([&writer, thread]() {
                for (int row = 0; row < 100; ++row) {
                    writer.write(thread, row);
                }
            });
        }
        for (auto& producer : producers) {
            producer.join();
        }
        writer.flush();
        file_name = writer.GetFileName();
    }

    std::istringstream stream(DecompressFile(file_name));
    std::string line;
    ASSERT_TRUE(static_cast<bool>(std::getline(stream, line)));
    EXPECT_EQ(line, "thread,row");
    std::set<std::string> rows;
    while (std::getline(stream, line)) {
        rows.insert(line);
    }
    EXPECT_EQ(rows.size(), 400U);
    for (int thread = 0; thread < 4; ++thread) {
        for (int row = 0; row < 100; ++row) {
            EXPECT_TRUE(rows.count(std::to_string(thread) + "," + std::to_string(row)));
        }
    }
}

TEST(LogWriterTests, IsolatesWorkerFailureToOneWriter) {
    TemporaryDirectory directory;
    LogWriter<1> failed_writer((directory.Path() / "failed").string(), {"value"});
    LogWriter<1> healthy_writer((directory.Path() / "healthy").string(), {"value"});

    detail::FailNextLogWriterTaskForTesting(failed_writer.GetFileName());
    failed_writer.write(1);
    EXPECT_THROW(failed_writer.flush(), std::runtime_error);
    EXPECT_THROW(failed_writer.write(2), std::runtime_error);

    healthy_writer.write(3);
    EXPECT_NO_THROW(healthy_writer.flush());
    EXPECT_EQ(DecompressFile(healthy_writer.GetFileName()), "value\n3\n");
}

}  // namespace
}  // namespace encos
