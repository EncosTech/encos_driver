#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <zstd.h>

#include "motor/motor_log_transport.h"
#include "test_fixtures.h"
#include "utils/log_writer_test_hook.h"
#include "waiter_test_access.h"

namespace encos {
namespace {

class MotorLogDirectory {
public:
    MotorLogDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("encos-motor-log-test-" + std::to_string(suffix));
        fs::create_directories(path_);
    }

    ~MotorLogDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& Path() const {
        return path_;
    }

private:
    fs::path path_;
};

std::string DecompressMotorLog(const fs::path& path) {
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

std::vector<std::string> SplitCsvRow(const std::string& row) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const auto comma = row.find(',', start);
        if (comma == std::string::npos) {
            fields.push_back(row.substr(start));
            return fields;
        }
        fields.push_back(row.substr(start, comma - start));
        start = comma + 1;
    }
}

std::vector<std::vector<std::string>> ParseSimpleCsv(const std::string& csv) {
    std::vector<std::vector<std::string>> rows;
    std::istringstream stream(csv);
    std::string line;
    while (std::getline(stream, line)) {
        rows.push_back(SplitCsvRow(line));
    }
    return rows;
}

class MotorLoggingTests : public MotorTestFixture {};

TEST(MotorLogTransportTests, UsesTriviallyCopyableFixedRecords) {
    static_assert(std::is_trivially_copyable_v<detail::MotorCommandLogRecord>);
    static_assert(std::is_trivially_copyable_v<detail::MotorStatusLogRecord>);
    static_assert(sizeof(detail::MotorCommandLogRecord) == 48U);
    static_assert(sizeof(detail::MotorStatusLogRecord) == 32U);
    SUCCEED();
}

TEST(MotorLogTransportTests, CommandPortRetainsNewestRecords) {
    detail::MotorLogPortSession session;
    for (std::int64_t timestamp = 1; timestamp <= detail::kMotorCommandLogPortCapacity + 2U;
         ++timestamp) {
        detail::MotorCommandLogRecord record;
        record.timestamp_ns = timestamp;
        session.command_port.Push(record);
    }

    const auto first = session.command_port.Pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->timestamp_ns, 3);
}

TEST(MotorLogTransportTests, StatusPortRetainsNewestRecords) {
    detail::MotorLogPortSession session;
    for (std::int64_t timestamp = 1; timestamp <= detail::kMotorStatusLogPortCapacity + 2U;
         ++timestamp) {
        detail::MotorStatusLogRecord record;
        record.timestamp_ns = timestamp;
        session.status_port.Push(record);
    }

    const auto first = session.status_port.Pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->timestamp_ns, 3);
}

std::size_t CountFilesWithPrefix(const fs::path& directory, const std::string& prefix) {
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().filename().string().find(prefix) == 0) {
            ++count;
        }
    }
    return count;
}

template <typename Predicate>
bool WaitForMotorLogCondition(Predicate&& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

bool WaitForMotorLogFailure(Motor* motor) {
    return WaitForMotorLogCondition([motor]() {
        return MotorWaiterTestAccess::LogSessionHasError(motor);
    });
}

TEST_F(MotorLoggingTests, StatusLoggingCoexistsWithUserCallbackAcrossDisable) {
    MotorLogDirectory directory;
    const auto base_name = (directory.Path() / "session").string();
    int callback_count = 0;
    motor->SetOnStatus([&](const MotorStatus&) {
        ++callback_count;
    });

    motor->EnableLog(base_name);
    EXPECT_TRUE(motor->IsLogged());
    motor->EnableLog(base_name);

    MotorStatus status;
    status.error = MotorError::OverCurrent;
    status.position = 1.0F;
    status.speed = 2.0F;
    status.current = 3.0F;
    status.motor_temperature = 4.0F;
    status.mos_temperature = 5.0F;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status));
    EXPECT_EQ(callback_count, 1);

    motor->DisableLog();
    EXPECT_FALSE(motor->IsLogged());
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status));
    EXPECT_EQ(callback_count, 2);

    const auto status_log = DecompressMotorLog(base_name + "_status.csv.zstd");
    const auto command_log = DecompressMotorLog(base_name + "_command.csv.zstd");
    EXPECT_EQ(command_log,
              "timestamp_ns,type,kp,kd,position,speed,current,torque,stop_mode,"
              "brake_enabled,feedback\n");

    const std::string header =
        "timestamp_ns,error,position,speed,current,motor_temperature,mos_temperature\n";
    ASSERT_EQ(status_log.compare(0, header.size(), header), 0);
    const auto row_end = status_log.find('\n', header.size());
    ASSERT_NE(row_end, std::string::npos) << status_log;
    const auto fields = SplitCsvRow(status_log.substr(header.size(), row_end - header.size()));
    ASSERT_EQ(fields.size(), 7U) << status_log;
    EXPECT_GT(std::stoll(fields[0]), 0);
    EXPECT_EQ(std::stoi(fields[1]), 2);
    EXPECT_NEAR(std::stof(fields[2]), 1.0F, kDecodedFloatTolerance);
    EXPECT_NEAR(std::stof(fields[3]), 2.0F, kDecodedFloatTolerance);
    EXPECT_NEAR(std::stof(fields[4]), 3.0F, kDecodedFloatTolerance);
    EXPECT_NEAR(std::stof(fields[5]), 4.0F, kDecodedFloatTolerance);
    EXPECT_NEAR(std::stof(fields[6]), 5.0F, kDecodedFloatTolerance);
}

TEST_F(MotorLoggingTests, SameBaseIsNoOpAndDifferentBaseSwitchesThePair) {
    MotorLogDirectory directory;
    const auto first_base = (directory.Path() / "first").string();
    const auto second_base = (directory.Path() / "second").string();
    int callback_count = 0;
    motor->SetOnStatus([&](const MotorStatus&) {
        ++callback_count;
    });

    motor->EnableLog(first_base);
    motor->EnableLog(first_base);
    EXPECT_EQ(CountFilesWithPrefix(directory.Path(), "first_status"), 1U);
    EXPECT_EQ(CountFilesWithPrefix(directory.Path(), "first_command"), 1U);

    MotorStatus status;
    status.error = MotorError::OverCurrent;
    status.position = 1.0F;
    status.speed = 2.0F;
    status.current = 3.0F;
    status.motor_temperature = 4.0F;
    status.mos_temperature = 5.0F;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status));
    motor->EnableLog(second_base);
    EXPECT_TRUE(motor->IsLogged());
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status));
    motor->DisableLog();
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status));

    EXPECT_EQ(callback_count, 3);
    EXPECT_EQ(CountFilesWithPrefix(directory.Path(), "first_status"), 1U);
    EXPECT_EQ(CountFilesWithPrefix(directory.Path(), "first_command"), 1U);
    EXPECT_EQ(CountFilesWithPrefix(directory.Path(), "second_status"), 1U);
    EXPECT_EQ(CountFilesWithPrefix(directory.Path(), "second_command"), 1U);
    EXPECT_EQ(ParseSimpleCsv(DecompressMotorLog(first_base + "_status.csv.zstd")).size(), 2U);
    EXPECT_EQ(ParseSimpleCsv(DecompressMotorLog(second_base + "_status.csv.zstd")).size(), 2U);
}

TEST_F(MotorLoggingTests, LogsSevenControlTypesAndSkipsManagementCommands) {
    MotorLogDirectory directory;
    const auto base_name = (directory.Path() / "commands").string();
    const auto ranges = motor->GetPVTRanges();
    motor->EnableLog(base_name);

    motor->PVTControl<0>(ranges.kp.max + 1.0F, ranges.kd.max + 1.0F, ranges.position.max + 1.0F,
                         ranges.speed.max + 1.0F, ranges.torque.max + 1.0F);
    motor->PosControl<0>(1.0F, 2.0F, 3.0F);
    motor->SpdControl<0>(4.0F, 5.0F);
    motor->CurControl<0>(6.0F);
    motor->TorControl<0>(7.0F);
    motor->Stop<0>(MotorStopMode::DynamicBrake, 8.0F);
    EXPECT_TRUE(motor->Brake(true, false));
    EXPECT_TRUE(motor->SetAcceleration(9.0F, false));
    motor->DisableLog();

    const auto rows = ParseSimpleCsv(DecompressMotorLog(base_name + "_command.csv.zstd"));
    ASSERT_EQ(rows.size(), 8U);
    for (const auto& row : rows) {
        ASSERT_EQ(row.size(), 11U);
    }
    EXPECT_EQ(rows[0][1], "type");

    EXPECT_EQ(rows[1][1], "PVTControl");
    EXPECT_NEAR(std::stof(rows[1][2]), ranges.kp.max, 1e-6F);
    EXPECT_NEAR(std::stof(rows[1][3]), ranges.kd.max, 1e-6F);
    EXPECT_NEAR(std::stof(rows[1][4]), ranges.position.max, 1e-6F);
    EXPECT_NEAR(std::stof(rows[1][5]), ranges.speed.max, 1e-6F);
    EXPECT_TRUE(rows[1][6].empty());
    EXPECT_NEAR(std::stof(rows[1][7]), ranges.torque.max, 1e-6F);
    EXPECT_TRUE(rows[1][8].empty());
    EXPECT_TRUE(rows[1][9].empty());
    EXPECT_EQ(rows[1][10], "0");

    EXPECT_EQ(rows[2][1], "PosControl");
    EXPECT_NEAR(std::stof(rows[2][4]), 1.0F, 1e-6F);
    EXPECT_NEAR(std::stof(rows[2][5]), 2.0F, 1e-6F);
    EXPECT_NEAR(std::stof(rows[2][6]), 3.0F, 1e-6F);
    EXPECT_EQ(rows[2][10], "0");

    EXPECT_EQ(rows[3][1], "SpdControl");
    EXPECT_NEAR(std::stof(rows[3][5]), 4.0F, 1e-6F);
    EXPECT_NEAR(std::stof(rows[3][6]), 5.0F, 1e-6F);

    EXPECT_EQ(rows[4][1], "CurControl");
    EXPECT_NEAR(std::stof(rows[4][6]), 6.0F, 1e-6F);

    EXPECT_EQ(rows[5][1], "TorControl");
    EXPECT_NEAR(std::stof(rows[5][7]), 7.0F, 1e-6F);

    EXPECT_EQ(rows[6][1], "Stop");
    EXPECT_NEAR(std::stof(rows[6][6]), 8.0F, 1e-6F);
    EXPECT_EQ(rows[6][8], std::to_string(static_cast<int>(MotorStopMode::DynamicBrake)));

    EXPECT_EQ(rows[7][1], "Brake");
    EXPECT_EQ(rows[7][9], "true");
}

TEST_F(MotorLoggingTests, RebuildsBothWritersAfterAnAsynchronousFailure) {
    MotorLogDirectory directory;
    const auto base_name = (directory.Path() / "recover").string();
    adapter->SetReplyMode(FakeReplyMode::Manual);
    motor->EnableLog(base_name);

    detail::FailNextLogWriterInteractionForTesting();
    EXPECT_NO_THROW(motor->SpdControl<0>(1.5F, 2.5F));
    ASSERT_TRUE(WaitForMotorLogFailure(motor));
    EXPECT_NO_THROW(motor->SpdControl<0>(1.5F, 2.5F));
    EXPECT_TRUE(motor->IsLogged());
    motor->DisableLog();

    std::size_t status_file_count = 0;
    std::size_t command_file_count = 0;
    bool found_retried_command = false;
    for (const auto& entry : fs::directory_iterator(directory.Path())) {
        const auto name = entry.path().filename().string();
        if (name.find("recover_status") == 0) {
            ++status_file_count;
        }
        if (name.find("recover_command") == 0) {
            ++command_file_count;
            const auto csv = DecompressMotorLog(entry.path());
            found_retried_command =
                found_retried_command || csv.find("SpdControl") != std::string::npos;
        }
    }
    EXPECT_EQ(status_file_count, 2U);
    EXPECT_EQ(command_file_count, 2U);
    EXPECT_TRUE(found_retried_command);
}

TEST_F(MotorLoggingTests, DisablesImmediatelyWhenAllReconstructionAttemptsFail) {
    MotorLogDirectory directory;
    const auto session_directory = directory.Path() / "removed";
    fs::create_directories(session_directory);
    adapter->SetReplyMode(FakeReplyMode::Manual);
    motor->EnableLog((session_directory / "session").string());
    fs::remove_all(session_directory);

    detail::FailNextLogWriterInteractionForTesting();
    EXPECT_NO_THROW(motor->SpdControl<0>(1.0F, 2.0F));
    ASSERT_TRUE(WaitForMotorLogFailure(motor));
    EXPECT_NO_THROW(motor->SpdControl<0>(1.0F, 2.0F));
    EXPECT_FALSE(motor->IsLogged());
}

TEST_F(MotorLoggingTests, StatusLogFailureDoesNotPreventUserCallback) {
    MotorLogDirectory directory;
    int callback_count = 0;
    motor->SetOnStatus([&](const MotorStatus&) {
        ++callback_count;
    });
    motor->EnableLog((directory.Path() / "status-recovery").string());

    detail::FailNextLogWriterInteractionForTesting();
    MotorStatus status;
    status.error = MotorError::NoError;
    adapter->InjectMessage(adapter->MakeFeedbackMessage(0, 1, status));

    EXPECT_EQ(callback_count, 1);
    EXPECT_TRUE(motor->IsLogged());
}

TEST_F(MotorLoggingTests, RecoveryOfOneMotorDoesNotAffectAnotherMotor) {
    MotorLogDirectory directory;
    const auto first_base = (directory.Path() / "motor-one").string();
    const auto second_base = (directory.Path() / "motor-two").string();
    adapter->SetReplyMode(FakeReplyMode::Manual);
    adapter->SeedMotor(0, 2, MotorModel::EC_A4310_P2);
    auto second_motor = bus->GetMotor(2, MotorModel::EC_A4310_P2);
    motor->EnableLog(first_base);
    second_motor->EnableLog(second_base);

    detail::FailNextLogWriterInteractionForTesting();
    EXPECT_NO_THROW(motor->SpdControl<0>(1.0F, 2.0F));
    ASSERT_TRUE(WaitForMotorLogFailure(motor));
    EXPECT_NO_THROW(motor->SpdControl<0>(1.0F, 2.0F));
    EXPECT_TRUE(motor->IsLogged());
    EXPECT_TRUE(second_motor->IsLogged());
    EXPECT_NO_THROW(second_motor->SpdControl<0>(3.0F, 4.0F));

    motor->DisableLog();
    second_motor->DisableLog();
    EXPECT_NE(DecompressMotorLog(second_base + "_command.csv.zstd").find("SpdControl"),
              std::string::npos);
    EXPECT_EQ(CountFilesWithPrefix(directory.Path(), "motor-two_status"), 1U);
    EXPECT_EQ(CountFilesWithPrefix(directory.Path(), "motor-two_command"), 1U);
}

TEST_F(MotorLoggingTests, ExplicitDisableCleansUpBeforeReportingFlushFailure) {
    MotorLogDirectory directory;
    const auto base_name = (directory.Path() / "disable-failure").string();
    adapter->SetReplyMode(FakeReplyMode::Manual);
    motor->EnableLog(base_name);
    motor->SpdControl<0>(1.0F, 2.0F);
    detail::FailNextLogWriterTaskForTesting(base_name + "_command.csv.zstd");

    EXPECT_THROW(motor->DisableLog(), std::runtime_error);
    EXPECT_FALSE(motor->IsLogged());
}

}  // namespace
}  // namespace encos
