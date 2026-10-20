// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <variant>

#include "bus/bus.h"
#include "device/motor/motor.h"
#include "device/motor/types.h"
#include "encos/driver_manager.h"
#include "platform/os.h"
#include "plugins/adapter/fake/fake_adapter.h"

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
 *
 * 默认模拟旧版固件：快照 `fw_version` 为 0（应答 0x32 但版本未知），因此
 * `GetProperties().fw_version` 为 0，所有版本相关分支都走旧版语义，且初始化不需要
 * 等待探测超时。需要新版语义的用例请派生 `NewFirmwareMotorTestFixture`；需要覆盖
 * “固件完全不认识 0x32”的用例请重写 `ConfigureSnapshot` 把 `fw_version` 设为 `nullopt`。
 */
class MotorTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        static int test_count = 0;
        encos::platform::SetEnv("ENCOS_PLUGIN_PATH", fs::absolute("./plugins").string());
        std::string adapter_name = GetAdapterName() + std::to_string(test_count++);
        adapter = static_cast<FakeAdapter*>(
            EncosDriverManager::Instance().CreateAdapterWithFactory(adapter_name, [adapter_name]() {
                return new FakeAdapter(adapter_name);
            }));
        adapter->SeedMotor(0, 1,
                           ConfigureSnapshot(adapter->CreateSnapshot(MotorModel::EC_A4310_P2)));
        bus = adapter->GetBus(0);
        motor = bus->GetMotor(1, MotorModel::EC_A4310_P2);
        // 构造期的固件版本探测会留下命令记录，统一清空，使用例断言只看到自己发出的命令。
        adapter->ClearCommandRecords();
    }

    /**
     * @brief 在播种快照前调整模拟电机的初始状态
     * @param snapshot 由型号生成的初始快照
     * @return 调整后的快照
     */
    virtual FakeMotorSnapshot ConfigureSnapshot(FakeMotorSnapshot snapshot) {
        return snapshot;
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

/**
 * @brief 模拟新版固件电机的测试基类
 *
 * 快照带固件版本、产品标识、硬件版本与编码器角度，因此 0x27/0x30/0x31/0x32 均有应答，
 * 且 `Motor::Brake`、`Motor::Stop`、温度解码、故障码映射与环路增益倍率都走新版分支。
 */
class NewFirmwareMotorTestFixture : public MotorTestFixture {
protected:
    /** @brief 模拟固件版本：BCD 2026-09-12 14 时 */
    static constexpr uint32_t kFirmwareVersion = 0x26091214;
    /** @brief 模拟产品标识 */
    static constexpr uint32_t kProductId = 0x07216000;
    /** @brief 模拟硬件版本：BCD 2026-06-02 01 时 */
    static constexpr uint32_t kHardwareVersion = 0x26060201;

    FakeMotorSnapshot ConfigureSnapshot(FakeMotorSnapshot snapshot) override {
        snapshot.fw_version = kFirmwareVersion;
        snapshot.product_id = kProductId;
        snapshot.hw_version = kHardwareVersion;
        snapshot.cal_flag = 1;
        snapshot.raw_angle_deg = 30.0f;
        snapshot.cal_angle_deg = 29.5f;
        return snapshot;
    }

    std::string GetAdapterName() const override {
        return "NewFirmwareMotorTestAdapter";
    }
};

}  // namespace encos
