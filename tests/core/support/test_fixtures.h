#pragma once

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <variant>

#include "bus/bus.h"
#include "encos/driver_manager.h"
#include "motor/motor.h"
#include "motor/types.h"
#include "plugins/fake/fake_adapter.h"

extern "C" {
int FloatToUint(float x, float x_min, float x_max, int bits);
float UintToFloat(int x_int, float x_min, float x_max, int bits);
}

namespace fs = std::filesystem;

namespace encos {

inline std::vector<FakeCommandRecord> WaitForCommandRecords(const FakeAdapter& adapter,
                                                            std::size_t count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    auto commands = adapter.GetCommandRecords();
    while (commands.size() < count && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        commands = adapter.GetCommandRecords();
    }
    EXPECT_GE(commands.size(), count);
    return commands;
}

template <typename T>
T LastPayloadAs(const FakeAdapter& adapter) {
    const auto commands = WaitForCommandRecords(adapter, 1);
    if (commands.empty()) {
        throw std::runtime_error("Timed out waiting for Fake Adapter command");
    }
    return std::get<T>(commands.back().payload);
}

inline FakeCommandRecord LastCommand(const FakeAdapter& adapter) {
    const auto commands = WaitForCommandRecords(adapter, 1);
    if (commands.empty()) {
        throw std::runtime_error("Timed out waiting for Fake Adapter command");
    }
    return commands.back();
}

inline void ExpectCommandKind(const FakeAdapter& adapter, FakeCommandKind kind) {
    EXPECT_EQ(LastCommand(adapter).kind, kind);
}

inline uint16_t DecodeU16Be(const std::vector<uint8_t>& raw) {
    EXPECT_GE(raw.size(), 2u);
    return static_cast<uint16_t>((static_cast<uint16_t>(raw[0]) << 8) | raw[1]);
}

inline int16_t DecodeI16Be(const std::vector<uint8_t>& raw) {
    return static_cast<int16_t>(DecodeU16Be(raw));
}

inline void ExpectRawBytes(const std::vector<uint8_t>& raw,
                           std::initializer_list<uint8_t> expected) {
    std::vector<uint8_t> expected_vec(expected);
    EXPECT_EQ(raw, expected_vec);
}

/**
 * @brief 电机测试基类，统一使用 FakeAdapter
 */
class MotorTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        static int test_count = 0;
        setenv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").c_str(), 1);
        std::string adapter_name = GetAdapterName() + std::to_string(test_count++);
        adapter = static_cast<FakeAdapter*>(
            EncosDriverManager::Instance().CreateAdapterWithFactory(adapter_name, [adapter_name]() {
                return new FakeAdapter(adapter_name);
            }));
        adapter->SeedMotor(0, 1, MotorModel::EC_A4310_P2);
        bus = adapter->GetBus(0);
        motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);
    }

    virtual std::string GetAdapterName() const {
        return "MotorTestAdapter";
    }

    void TearDown() override {
        ASSERT_TRUE(EncosDriverManager::Instance().DestroyAdapter(adapter));
        adapter = nullptr;
        bus = nullptr;
        motor = nullptr;
    }

    FakeAdapter* adapter = nullptr;
    Bus* bus = nullptr;
    Motor* motor = nullptr;
};

}  // namespace encos
