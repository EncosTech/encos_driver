// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "ethercat_base_handle.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <vector>

#include "ethercat_cycle_options.h"
#include "platform/log.h"
#include "plugins/adapter/ethercat/ethercat_handle.h"

namespace {

TEST(EthercatLoopPeriodTests, SoemAdapterDefaultsToOneKilohertz) {
    EXPECT_EQ(EthercatHandle::kDefaultLoopPeriod, std::chrono::microseconds(1000));
}

TEST(EthercatLoopPeriodTests, ExtractsCycleAndPreservesCanConfiguration) {
    auto defaults = encos::AdapterExtend::Parse("");
    EXPECT_EQ(encos::ethercat::ExtractCyclePeriod(defaults).count(), 1000);
    EXPECT_TRUE(defaults.Empty());
    auto config = encos::AdapterExtend::Parse(
        R"({"cycle_period_us":250,"bitrate":500000,"buses":{"1:2":{"dbitrate":2000000}}})");
    EXPECT_EQ(encos::ethercat::ExtractCyclePeriod(config).count(), 250);
    EXPECT_FALSE(config.Common().contains("cycle_period_us"));
    EXPECT_EQ(config.Scope(1, 2).at("bitrate"), 500000);
    EXPECT_EQ(config.Scope(1, 2).at("dbitrate"), 2000000);
    EXPECT_EQ(config.DeclaredBuses().size(), 1U);
    for (const auto period : {1, 1000000}) {
        auto boundary =
            encos::AdapterExtend::Parse(nlohmann::json{{"cycle_period_us", period}}.dump());
        EXPECT_EQ(encos::ethercat::ExtractCyclePeriod(boundary).count(), period);
    }
}

TEST(EthercatLoopPeriodTests, RejectsInvalidCyclesAndPerBusOverrides) {
    for (const auto* value : {"0", "-1", "1000001", "18446744073709551615", "1.5", "1000.0", "true",
                              "null", "\"250\""}) {
        auto config =
            encos::AdapterExtend::Parse(std::string("{\"cycle_period_us\":") + value + "}");
        EXPECT_THROW(encos::ethercat::ExtractCyclePeriod(config), std::invalid_argument) << value;
        EXPECT_TRUE(config.Common().contains("cycle_period_us"));
    }
    auto scoped = encos::AdapterExtend::Parse(R"({"buses":{"1:0":{"cycle_period_us":250}}})");
    EXPECT_THROW(encos::ethercat::ExtractCyclePeriod(scoped), std::invalid_argument);
}

TEST(EthercatLoopPeriodTests, RejectsRuntimeCycleChangesBeforeApplyingBusUpdates) {
    for (const auto* json :
         {R"({"cycle_period_us":1000})", R"({"buses":{"1:0":{"cycle_period_us":500}}})"}) {
        EXPECT_THROW(encos::ethercat::RejectCyclePeriodUpdate(encos::AdapterExtend::Parse(json)),
                     std::invalid_argument);
    }
    EXPECT_NO_THROW(encos::ethercat::RejectCyclePeriodUpdate(
        encos::AdapterExtend::Parse(R"({"buses":{"1:0":{"bitrate":500000}}})")));
}

class TestEthercatBaseHandle : public EthercatBaseHandle {
public:
    using Frame = OutputFrame;

    TestEthercatBaseHandle() : EthercatBaseHandle(encos::CreateLogger("ethercat_base_test")) {}

    void ConfigureCanFd8Bus() {
        SlaveConfig config;
        config.format = SlaveFormat::CanFd8Bus;
        config.bus_count = 8;
        config.motors_per_bus = 8;
        config.message_size = sizeof(EthercatCanFdMsg8);
        slave_configs_ = {config};
    }

    void ConfigureCanFd8Bus10Slots() {
        SlaveConfig config;
        config.format = SlaveFormat::CanFd8Bus10Slots;
        config.bus_count = 8;
        config.motors_per_bus = 10;
        config.message_size = sizeof(EthercatCanFdMsg8x10);
        slave_configs_ = {config};
    }

    void ConfigureCanFd3Bus() {
        SlaveConfig config;
        config.format = SlaveFormat::CanFd3Bus;
        config.bus_count = 3;
        config.motors_per_bus = 8;
        config.message_size = sizeof(EthercatCanFdMsg3);
        slave_configs_ = {config};
    }

    void ConfigureGloveSlots() {
        slave_configs_ = {ClassifyOutputPdoSize(sizeof(EthercatGloveSlots))};
    }

    void ConfigureClassicCan8Bus() {
        SlaveConfig config;
        config.format = SlaveFormat::ClassicCan8Bus;
        config.bus_count = 8;
        config.motors_per_bus = 3;
        config.message_size = sizeof(EthercatClassicCanMsg8);
        slave_configs_ = {config};
    }

    void ConfigureClassicCan2Bus() {
        SlaveConfig config;
        config.format = SlaveFormat::ClassicCan2Bus;
        config.bus_count = 2;
        config.motors_per_bus = 3;
        config.message_size = sizeof(EthercatClassicCanMsg2);
        slave_configs_ = {config};
    }

    std::vector<int> ConfigureByOutputPdoSizes(std::initializer_list<std::size_t> output_pdo_sizes,
                                               std::size_t input_pdo_size = 0) {
        slave_configs_.clear();
        for (const auto output_pdo_size : output_pdo_sizes) {
            slave_configs_.push_back(ClassifyOutputPdoSize(output_pdo_size, input_pdo_size));
        }
        return GetBusSizes();
    }

    bool OutputPdoSizeIsSupported(std::size_t output_pdo_size) {
        return HasSupportedPdo(ClassifyOutputPdoSize(output_pdo_size));
    }

    void Queue(const encos::MotorMessage& message) {
        QueueMessage(message);
    }

    void Queue(const encos::MotorMessages& messages) {
        QueueMessages(messages);
    }

    void QueueSynchronized(const encos::MotorMessages& messages) {
        QueueSynchronizedMessages(messages);
    }

    bool NextFrame(Frame& frame) {
        return PrepareNextFrame(frame);
    }

    std::vector<Frame> Pack(const encos::MotorMessages& messages) const {
        return PackMessages(messages, 1);
    }

    encos::MotorMessages Decode(const std::vector<const uint8_t*>& inputs,
                                InputClock::time_point now = {}) {
        return DecodeInputs(inputs, 1, std::chrono::milliseconds(1), now);
    }
};

encos::MotorMessage MakeMessage(int bus_idx, uint32_t id, uint8_t marker) {
    encos::MotorMessage message{};
    message.bus_idx = bus_idx;
    message.data.id = id;
    message.data.len = 8;
    for (std::size_t i = 0; i < sizeof(message.data.data); ++i) {
        message.data.data[i] = static_cast<uint8_t>(marker + i);
    }
    return message;
}

using FragmentedPdo = std::array<uint8_t, 420>;

void SetPdoSlot(FragmentedPdo& pdo, std::size_t bus, std::size_t slot, uint32_t id, uint8_t flags,
                uint8_t length, uint8_t value) {
    auto* wire = pdo.data() + bus * 140 + slot * 14;
    for (unsigned i = 0; i < 4; ++i) {
        wire[i] = static_cast<uint8_t>(id >> (i * 8));
    }
    wire[4] = flags;
    wire[5] = length;
    std::fill_n(wire + 6, 8, value);
}

}  // namespace

TEST(EthercatPayloadTests, PreservesShortWireBytesAndRejectsLengthsPast64) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd8Bus();
    auto valid = MakeMessage(0, 0x123, 0xA0);
    valid.data.frame_flags = encos::kCanFrameFlagFdMask;
    auto oversized = valid;
    oversized.data.len = 65;
    EXPECT_TRUE(handle.Pack({oversized}).empty());
    const auto frames = handle.Pack({oversized, valid});
    ASSERT_EQ(frames.size(), 1U);
    ASSERT_EQ(frames[0][0].size(), 896U);
    const std::vector<uint8_t> expected{0x23, 0x01, 0,    0,    0x06, 8,    0xA0,
                                        0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7};
    EXPECT_EQ(std::vector<uint8_t>(frames[0][0].data(), frames[0][0].data() + 14), expected);
    for (std::size_t i = 14; i < frames[0][0].size(); ++i) {
        EXPECT_EQ(frames[0][0].data()[i], 0);
    }
    const auto decoded = handle.Decode({frames[0][0].data()});
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded[0].data.len, 8);
    for (std::size_t i = 0; i < 8; ++i) {
        EXPECT_EQ(decoded[0].data.data[i], valid.data.data[i]);
    }
    for (std::size_t i = 8; i < sizeof(decoded[0].data.data); ++i) {
        EXPECT_EQ(decoded[0].data.data[i], 0);
    }

    valid.data.len = 3;
    const auto short_frames = handle.Pack({valid});
    ASSERT_EQ(short_frames.size(), 1U);
    const auto* slot = short_frames[0][0].data();
    EXPECT_EQ(slot[5], 3);
    EXPECT_TRUE(std::all_of(slot + 9, slot + 14, [](uint8_t byte) {
        return byte == 0;
    }));
}

TEST(EthercatPayloadTests, IgnoresMalformedPdoLength) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd8Bus();
    EthercatCanFdMsg8 packet{};
    packet.motor[0].len = 64;
    packet.motor[1].len = 8;
    packet.motor[1].id = 42;
    const auto decoded = handle.Decode({reinterpret_cast<const uint8_t*>(&packet)});
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded[0].data.id, 42U);
}

TEST(EthercatPayloadTests, CanFd3BusFragmentedPdoPreservesPayloadAndWireOffsets) {
    TestEthercatBaseHandle handle;
    ASSERT_EQ(handle.ConfigureByOutputPdoSizes({420}, 420), (std::vector<int>{3}));

    auto message = MakeMessage(2, 0x123, 0xA0);
    message.data.len = 20;
    message.data.frame_flags = encos::kCanFrameFlagFdMask;
    auto full = MakeMessage(0, 0x12345678, 0x31);
    full.data.len = 64;
    full.data.frame_flags = encos::kCanFrameFlagEff | encos::kCanFrameFlagFdMask;
    const auto frames = handle.Pack({message, full});
    ASSERT_EQ(frames.size(), 1U);
    ASSERT_EQ(frames[0][0].size(), 420U);

    const auto* wire = frames[0][0].data();
    EXPECT_EQ(wire[280], 0x23);
    EXPECT_EQ(wire[281], 0x01);
    EXPECT_EQ(wire[284], 0x16);
    EXPECT_EQ(wire[298], 0x36);
    EXPECT_EQ(wire[312], 0x56);
    EXPECT_EQ(wire[313], 20);
    EXPECT_EQ(wire[317], 0xB3);
    for (std::size_t i = 318; i < 322; ++i) {
        EXPECT_EQ(wire[i], 0);
    }
    EXPECT_EQ(wire[7 * 14 + 4], 0xF7);

    const auto decoded = handle.Decode({wire});
    ASSERT_EQ(decoded.size(), 2U);
    EXPECT_EQ(decoded[0].bus_idx, 0);
    EXPECT_EQ(decoded[0].data, full.data);
    EXPECT_EQ(decoded[1].bus_idx, 2);
    EXPECT_EQ(decoded[1].data.len, 20);
    EXPECT_EQ(std::memcmp(decoded[1].data.data, message.data.data, message.data.len), 0);
}

TEST(EthercatPayloadTests, ExistingCanFdLayoutsUseTheSameFragmentsWithoutChangingPdoSize) {
    for (const auto size : {336U, 420U, 896U, 1120U}) {
        SCOPED_TRACE(size);
        TestEthercatBaseHandle handle;
        const auto buses = handle.ConfigureByOutputPdoSizes({size}, size);
        ASSERT_GT(buses[0], 0);
        const auto bus = buses[0] - 1;
        const auto bus_bytes = size / buses[0];
        const auto offset = bus * bus_bytes;
        auto message = MakeMessage(bus, 0x123, 0x41);
        message.data.frame_flags = encos::kCanFrameFlagFdMask;
        message.data.len = 64;
        const auto frames = handle.Pack({message});
        ASSERT_EQ(frames.size(), 1U);
        ASSERT_EQ(frames[0][0].size(), size);
        const auto* wire = frames[0][0].data();
        EXPECT_EQ(wire[offset + 4], 0x16);
        EXPECT_EQ(wire[offset + 7 * 14 + 4], 0xF6);

        std::vector<uint8_t> first(wire, wire + size);
        std::fill_n(first.data() + offset + 4 * 14, 4 * 14, 0);
        std::vector<uint8_t> second(wire, wire + size);
        std::fill_n(second.data() + offset, 4 * 14, 0);
        EXPECT_TRUE(handle.Decode({first.data()}).empty());
        const auto decoded = handle.Decode({second.data()});
        ASSERT_EQ(decoded.size(), 1U);
        EXPECT_EQ(decoded[0].bus_idx, bus);
        EXPECT_EQ(decoded[0].data, message.data);
    }
}

TEST(EthercatPayloadTests, RequiresMatching420ByteMappingAndKeepsLegacyThreeBusFormat) {
    TestEthercatBaseHandle handle;
    EXPECT_EQ(handle.ConfigureByOutputPdoSizes({420}, 336), (std::vector<int>{0}));
    EXPECT_EQ(handle.ConfigureByOutputPdoSizes({420}), (std::vector<int>{0}));
    EXPECT_EQ(handle.ConfigureByOutputPdoSizes({336}, 336), (std::vector<int>{3}));

    auto message = MakeMessage(0, 0x123, 0x41);
    message.data.frame_flags = encos::kCanFrameFlagFdMask;
    const auto frames = handle.Pack({message});
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0][0].size(), 336U);
    EXPECT_EQ(frames[0][0].data()[4], 0x06);
}

TEST(EthercatPayloadTests, ClassicCanAndGloveKeepTheirEightByteLimit) {
    for (const auto size : {86U, 340U, 700U}) {
        SCOPED_TRACE(size);
        TestEthercatBaseHandle handle;
        ASSERT_GT(handle.ConfigureByOutputPdoSizes({size})[0], 0);
        auto message = MakeMessage(0, 0x123, 0x41);
        message.data.len = 64;
        message.data.frame_flags = encos::kCanFrameFlagFdMask;
        EXPECT_TRUE(handle.Pack({message}).empty());
    }
}

TEST(EthercatPayloadTests, KeepsLongMessagesWholeAndInOrderAcrossQueuedPdos) {
    TestEthercatBaseHandle handle;
    ASSERT_EQ(handle.ConfigureByOutputPdoSizes({420}, 420), (std::vector<int>{3}));

    handle.Queue({MakeMessage(0, 1, 1), MakeMessage(0, 2, 2), MakeMessage(0, 3, 3)});
    auto long_message = MakeMessage(0, 0x100, 0x10);
    long_message.data.len = 64;
    long_message.data.frame_flags = encos::kCanFrameFlagFdMask;
    handle.QueueSynchronized({long_message, MakeMessage(0, 4, 4)});
    handle.QueueSynchronized({MakeMessage(0, 5, 5)});

    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    auto decoded = handle.Decode({frame[0].data()});
    ASSERT_EQ(decoded.size(), 3U);
    EXPECT_EQ(decoded.back().data.id, 3U);
    EXPECT_EQ(frame[0].data()[3 * 14 + 5], 0);

    ASSERT_TRUE(handle.NextFrame(frame));
    decoded = handle.Decode({frame[0].data()});
    ASSERT_EQ(decoded.size(), 2U);
    EXPECT_EQ(decoded[0].data, long_message.data);
    EXPECT_EQ(decoded[1].data.id, 4U);

    ASSERT_TRUE(handle.NextFrame(frame));
    decoded = handle.Decode({frame[0].data()});
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded[0].data.id, 5U);
    EXPECT_FALSE(handle.NextFrame(frame));

    auto newest = long_message;
    newest.data.id = 0x101;
    handle.Queue({long_message, long_message, long_message, newest});
    ASSERT_TRUE(handle.NextFrame(frame));
    decoded = handle.Decode({frame[0].data()});
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded[0].data, newest.data);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatPayloadTests, ReassemblesAcrossPdosAndKeepsRawBrsInTheMatchingKey) {
    TestEthercatBaseHandle handle;
    ASSERT_EQ(handle.ConfigureByOutputPdoSizes({420}, 420), (std::vector<int>{3}));

    FragmentedPdo first{};
    SetPdoSlot(first, 1, 0, 0x123, 0x16, 20, 0xA0);
    SetPdoSlot(first, 1, 1, 0x123, 0x56, 20, 0xC0);
    SetPdoSlot(first, 1, 2, 0x123, 0x56, 20, 0xC0);
    first[140 + 2 * 14 + 13] = 0xFF;
    SetPdoSlot(first, 1, 3, 0x124, 0x08, 8, 0xDD);
    auto decoded = handle.Decode({first.data()});
    ASSERT_EQ(decoded.size(), 1U);
    EXPECT_EQ(decoded[0].data.id, 0x124U);
    EXPECT_EQ(decoded[0].data.data[0], 0);

    FragmentedPdo second{};
    SetPdoSlot(second, 1, 0, 0x123, 0x32, 20, 0xFF);
    SetPdoSlot(second, 2, 0, 0x123, 0x36, 20, 0xFF);
    SetPdoSlot(second, 1, 1, 0x123, 0x36, 20, 0xB0);
    SetPdoSlot(second, 1, 2, 0x125, 0x02, 4, 0xDD);
    decoded = handle.Decode({second.data()},
                            std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(1));
    ASSERT_EQ(decoded.size(), 2U);
    EXPECT_EQ(decoded[0].bus_idx, 1);
    EXPECT_EQ(decoded[0].data.len, 20);
    EXPECT_EQ(decoded[0].data.frame_flags, encos::kCanFrameFlagFdMask);
    EXPECT_EQ(decoded[0].data.data[0], 0xA0);
    EXPECT_EQ(decoded[0].data.data[8], 0xB0);
    EXPECT_EQ(decoded[0].data.data[19], 0xC0);
    EXPECT_EQ(decoded[0].data.data[20], 0);
    EXPECT_EQ(decoded[1].data.id, 0x125U);
    EXPECT_EQ(decoded[1].data.frame_flags, encos::kCanFrameFlagFdMask);
}

TEST(EthercatPayloadTests, DiscardsIncompletePdoGroupsAfterAGapOrDeadline) {
    TestEthercatBaseHandle handle;
    ASSERT_EQ(handle.ConfigureByOutputPdoSizes({420}, 420), (std::vector<int>{3}));

    FragmentedPdo start{};
    SetPdoSlot(start, 0, 0, 1, 0x16, 9, 0x31);
    FragmentedPdo finish{};
    SetPdoSlot(finish, 0, 0, 1, 0x36, 9, 0x32);
    const FragmentedPdo empty{};
    const std::chrono::steady_clock::time_point now{};

    EXPECT_TRUE(handle.Decode({start.data()}, now).empty());
    EXPECT_TRUE(handle.Decode({nullptr}, now).empty());
    EXPECT_TRUE(handle.Decode({finish.data()}, now).empty());

    EXPECT_TRUE(handle.Decode({start.data()}, now).empty());
    EXPECT_TRUE(handle.Decode({empty.data()}, now + std::chrono::microseconds(500)).empty());
    EXPECT_TRUE(handle.Decode({finish.data()}, now + std::chrono::milliseconds(1)).empty());

    EXPECT_TRUE(handle.Decode({start.data()}, now).empty());
    EXPECT_TRUE(handle.Decode({finish.data()}, now + std::chrono::milliseconds(2)).empty());
}

TEST(EthercatPayloadTests, DropsMalformedSlotsAndConflictingFragments) {
    TestEthercatBaseHandle handle;
    ASSERT_EQ(handle.ConfigureByOutputPdoSizes({420}, 420), (std::vector<int>{3}));

    FragmentedPdo pdo{};
    SetPdoSlot(pdo, 0, 0, 0, 0x00, 8, 1);
    SetPdoSlot(pdo, 0, 1, 1, 0x10, 8, 1);
    SetPdoSlot(pdo, 0, 2, 0x800, 0x00, 8, 1);
    SetPdoSlot(pdo, 0, 3, 1, 0x04, 8, 1);
    SetPdoSlot(pdo, 0, 4, 1, 0x0E, 8, 1);
    SetPdoSlot(pdo, 0, 5, 1, 0x06, 64, 1);
    SetPdoSlot(pdo, 0, 6, 1, 0x16, 65, 1);
    SetPdoSlot(pdo, 1, 0, 1, 0x16, 20, 1);
    SetPdoSlot(pdo, 1, 1, 1, 0x36, 20, 2);
    SetPdoSlot(pdo, 1, 2, 1, 0x36, 20, 3);
    SetPdoSlot(pdo, 1, 3, 1, 0x56, 20, 4);
    SetPdoSlot(pdo, 2, 0, 1, 0x16, 9, 1);
    SetPdoSlot(pdo, 2, 1, 1, 0x56, 9, 2);
    SetPdoSlot(pdo, 2, 2, 1, 0x36, 9, 3);
    EXPECT_TRUE(handle.Decode({pdo.data()}).empty());

    auto outgoing = MakeMessage(0, 1, 1);
    outgoing.data.len = 9;
    EXPECT_TRUE(handle.Pack({outgoing}).empty());
    outgoing.data.frame_flags = encos::kCanFrameFlagFdMask | encos::kCanFrameFlagRtr;
    EXPECT_TRUE(handle.Pack({outgoing}).empty());
    outgoing.data.frame_flags = encos::kCanFrameFlagRtr;
    outgoing.data.len = 8;
    const auto frames = handle.Pack({outgoing});
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_TRUE(std::all_of(frames[0][0].data() + 6, frames[0][0].data() + 14, [](uint8_t byte) {
        return byte == 0;
    }));
}

TEST(EthercatBaseHandleTests, OutputPdoClassificationRequiresAnExactSupportedSize) {
    TestEthercatBaseHandle handle;

    EXPECT_EQ(handle.ConfigureByOutputPdoSizes(
                  {sizeof(EthercatClassicCanMsg2), 87, 0, sizeof(EthercatCanFdMsg8x10)}),
              (std::vector<int>{2, 0, 0, 8}));
    EXPECT_TRUE(handle.OutputPdoSizeIsSupported(sizeof(EthercatClassicCanMsg2)));
    EXPECT_FALSE(handle.OutputPdoSizeIsSupported(87));
    EXPECT_FALSE(handle.OutputPdoSizeIsSupported(0));
}

TEST(EthercatBaseHandleTests, GloveSlotsInsertMessagesIntoTheirFingerBusPartition) {
    TestEthercatBaseHandle handle;
    handle.ConfigureGloveSlots();

    // 5 条帧内总线（每根手指一条），消息按 bus_idx 落入对应分区的首个空槽。
    const auto frames = handle.Pack({MakeMessage(0, 0x12345678u, 0x31), MakeMessage(0, 0x0u, 0x41),
                                     MakeMessage(4, 0x12345679u, 0x51)});

    ASSERT_EQ(frames.size(), 1u);
    ASSERT_EQ(frames[0].size(), 1u);
    ASSERT_EQ(frames[0][0].size(), sizeof(EthercatGloveSlots));
    const auto* packet = reinterpret_cast<const EthercatGloveSlots*>(frames[0][0].data());
    EXPECT_EQ(packet->motor[0].id, 0x12345678u);
    EXPECT_EQ(packet->motor[0].data[0], 0x31u);
    EXPECT_EQ(packet->motor[1].id, 0u);
    EXPECT_EQ(packet->motor[1].data[0], 0x41u);
    // bus 4 的消息落入手套第 4 分区（slot 40-49）的首个空槽
    EXPECT_EQ(packet->motor[40].id, 0x12345679u);
    EXPECT_EQ(packet->motor[40].data[0], 0x51u);
    // 未使用的分区槽位保持空
    EXPECT_EQ(packet->motor[10].len, 0u);
    EXPECT_EQ(packet->motor[30].len, 0u);
}

TEST(EthercatBaseHandleTests, GloveSlotsDecodeAllWireSlotsPerFingerBus) {
    TestEthercatBaseHandle handle;
    EXPECT_EQ(handle.ConfigureByOutputPdoSizes({sizeof(EthercatGloveSlots), 713u}),
              (std::vector<int>{5, 0}));
    handle.ConfigureGloveSlots();

    EthercatGloveSlots packet{};
    for (std::size_t slot = 0; slot < 50; ++slot) {
        packet.motor[slot].id = static_cast<uint32_t>(0x9000u + slot);
        packet.motor[slot].len = 1;
        packet.motor[slot].data[0] = static_cast<uint8_t>(slot);
    }
    std::vector<uint8_t> raw(sizeof(packet));
    std::memcpy(raw.data(), &packet, sizeof(packet));

    auto decoded = handle.Decode({raw.data()});
    ASSERT_EQ(decoded.size(), 50u);
    EXPECT_EQ(decoded.front().bus_idx, 0);
    EXPECT_EQ(decoded.front().data.id, 0x9000u);
    EXPECT_EQ(decoded.back().data.id, 0x9031u);
    EXPECT_EQ(decoded.back().data.data[0], 49u);
    // 每个手指分区的消息总线编号正确（slot 10-19 → bus 1，slot 40-49 → bus 4）
    EXPECT_EQ(decoded[10].bus_idx, 1);
    EXPECT_EQ(decoded[40].bus_idx, 4);

    decoded = handle.Decode({raw.data()});
    ASSERT_EQ(decoded.size(), 50u);
    EXPECT_EQ(decoded.back().data.id, 0x9031u);
}

TEST(EthercatBaseHandleTests, CanFd8BusPacksEightSlotsPerBusAndSplitsOverflow) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd8Bus();

    encos::MotorMessages messages;
    for (uint8_t i = 0; i < 9; ++i) {
        messages.push_back(MakeMessage(7, static_cast<uint32_t>(0x120 + i), i));
    }

    const auto frames = handle.Pack(messages);

    ASSERT_EQ(frames.size(), 2);
    ASSERT_EQ(frames[0].size(), 1);
    ASSERT_EQ(frames[0][0].size(), sizeof(EthercatCanFdMsg8));
    EthercatCanFdMsg8 first{};
    EthercatCanFdMsg8 second{};
    std::memcpy(&first, frames[0][0].data(), sizeof(first));
    std::memcpy(&second, frames[1][0].data(), sizeof(second));

    EXPECT_EQ(first.motor[56].id, 0x120u);
    EXPECT_EQ(first.motor[63].id, 0x127u);
    EXPECT_EQ(second.motor[56].id, 0x128u);
    EXPECT_EQ(second.motor[57].len, 0);
}

TEST(EthercatBaseHandleTests, CanFd8Bus10SlotsPacksTenSlotsPerBusAndSplitsOverflow) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd8Bus10Slots();

    encos::MotorMessages messages;
    for (uint8_t i = 0; i < 11; ++i) {
        messages.push_back(MakeMessage(7, static_cast<uint32_t>(0x220 + i), i));
    }

    const auto frames = handle.Pack(messages);

    ASSERT_EQ(frames.size(), 2);
    ASSERT_EQ(frames[0].size(), 1);
    ASSERT_EQ(frames[0][0].size(), sizeof(EthercatCanFdMsg8x10));
    EthercatCanFdMsg8x10 first{};
    EthercatCanFdMsg8x10 second{};
    std::memcpy(&first, frames[0][0].data(), sizeof(first));
    std::memcpy(&second, frames[1][0].data(), sizeof(second));

    EXPECT_EQ(first.motor[70].id, 0x220u);
    EXPECT_EQ(first.motor[79].id, 0x229u);
    EXPECT_EQ(second.motor[70].id, 0x22Au);
    EXPECT_EQ(second.motor[71].len, 0);
}

TEST(EthercatBaseHandleTests, CanFd8Bus10SlotsDecodesEightiethSlot) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd8Bus10Slots();

    EthercatCanFdMsg8x10 packet{};
    packet.motor[79].id = 0x521;
    packet.motor[79].frame_flags = encos::kCanFrameFlagEff | encos::kCanFrameFlagFdMask;
    packet.motor[79].len = 2;
    packet.motor[79].data[0] = 0x56;
    packet.motor[79].data[1] = 0x78;

    std::vector<uint8_t> raw(sizeof(packet));
    std::memcpy(raw.data(), &packet, sizeof(packet));
    const auto decoded = handle.Decode({raw.data()});

    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded.front().bus_idx, 7);
    EXPECT_EQ(decoded.front().data.id, 0x521u);
    EXPECT_EQ(decoded.front().data.frame_flags,
              encos::kCanFrameFlagEff | encos::kCanFrameFlagFdMask);
    EXPECT_EQ(decoded.front().data.data[1], 0x78);
}

TEST(EthercatBaseHandleTests, RejectsProcessDataMapAboveSafetyLimitBeforeMapping) {
    ecx_contextt context{};
    context.slavecount = 3;
    for (int slave = 1; slave <= 2; ++slave) {
        for (int sm = 0; sm < EC_MAXSM; ++sm) {
            context.slavelist[slave].SMtype[sm] = 3;
            context.slavelist[slave].SM[sm].SMlength = 65535;
        }
    }
    context.slavelist[3].SMtype[0] = 3;
    context.slavelist[3].SM[0].SMlength = 14;

    EXPECT_FALSE(ValidateEthercatIoMap(context, encos::CreateLogger("ethercat_map_test")));
}

TEST(EthercatBaseHandleTests, AcceptsKnownProcessDataMapWithinSafetyLimit) {
    ecx_contextt context{};
    context.slavecount = 1;
    context.slavelist[1].Obytes = 128;
    context.slavelist[1].Ibytes = 128;
    context.slavelist[1].SMtype[2] = 3;
    context.slavelist[1].SM[2].SMlength = 128;
    context.slavelist[1].SMtype[3] = 4;
    context.slavelist[1].SM[3].SMlength = 128;

    EXPECT_TRUE(ValidateEthercatIoMap(context, encos::CreateLogger("ethercat_map_test")));
}

TEST(EthercatBaseHandleTests, AcceptsProcessDataMapExactlyAtSafetyLimit) {
    ecx_contextt context{};
    context.slavecount = 3;
    for (int slave = 1; slave <= 2; ++slave) {
        for (int sm = 0; sm < EC_MAXSM; ++sm) {
            context.slavelist[slave].SMtype[sm] = 3;
            context.slavelist[slave].SM[sm].SMlength = 65535;
        }
    }
    context.slavelist[3].SMtype[0] = 3;
    context.slavelist[3].SM[0].SMlength = 13;

    const auto capacity =
        ComputeEthercatIoMapUpperBound(context, encos::CreateLogger("ethercat_map_test"));
    ASSERT_TRUE(capacity.has_value());
    EXPECT_EQ(*capacity, kEthercatMaxIoMapSize);
}

TEST(EthercatBaseHandleTests, AcceptsProcessDataMapAboveLegacyFourKilobyteBuffer) {
    ecx_contextt context{};
    context.slavecount = 1;
    context.slavelist[1].SMtype[2] = 3;
    context.slavelist[1].SM[2].SMlength = 8192;

    EXPECT_TRUE(ValidateEthercatIoMap(context, encos::CreateLogger("ethercat_map_test")));
}

TEST(EthercatBaseHandleTests, RejectsUnknownSyncManagerType) {
    ecx_contextt context{};
    context.slavecount = 1;
    context.slavelist[1].SMtype[2] = 5;
    context.slavelist[1].SM[2].SMlength = 64;

    EXPECT_FALSE(ValidateEthercatIoMap(context, encos::CreateLogger("ethercat_map_test")));
}

TEST(EthercatBaseHandleTests, PremapCapacityUsesSyncManagersInsteadOfFinalByteCounters) {
    ecx_contextt context{};
    context.slavecount = 1;
    context.slavelist[1].Obytes = static_cast<uint32>(kEthercatMaxIoMapSize);
    context.slavelist[1].Ibytes = static_cast<uint32>(kEthercatMaxIoMapSize);
    context.slavelist[1].SMtype[2] = 3;
    context.slavelist[1].SM[2].SMlength = 128;

    const auto capacity =
        ComputeEthercatIoMapUpperBound(context, encos::CreateLogger("ethercat_map_test"));

    ASSERT_TRUE(capacity.has_value());
    EXPECT_EQ(*capacity, 129u);
}

TEST(EthercatBaseHandleTests, PremapCapacityRejectsMissingPdoDescription) {
    ecx_contextt context{};
    context.slavecount = 1;
    context.slavelist[1].Obytes = 4096;
    context.slavelist[1].Ibytes = 4096;

    EXPECT_FALSE(ComputeEthercatIoMapUpperBound(context, encos::CreateLogger("ethercat_map_test"))
                     .has_value());
}

TEST(EthercatBaseHandleTests, RejectsEmptyOrOversizedMappedResult) {
    EXPECT_FALSE(IsEthercatMappedSizeValid(0, 128));
    EXPECT_FALSE(IsEthercatMappedSizeValid(-1, 128));
    EXPECT_TRUE(IsEthercatMappedSizeValid(128, 128));
    EXPECT_FALSE(IsEthercatMappedSizeValid(129, 128));
}

TEST(EthercatBaseHandleTests, PackPropagatesFrameFlagsToEthercatSlots) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd8Bus();

    auto message = MakeMessage(0, 0x321, 0x44);
    message.data.frame_flags = encos::kCanFrameFlagEff | encos::kCanFrameFlagFdMask;

    const auto frames = handle.Pack({message});

    ASSERT_EQ(frames.size(), 1);
    ASSERT_EQ(frames[0].size(), 1);
    const auto* packet = reinterpret_cast<const EthercatCanFdMsg8*>(frames[0][0].data());
    EXPECT_EQ(packet->motor[0].frame_flags, encos::kCanFrameFlagEff | encos::kCanFrameFlagFdMask);
}

TEST(EthercatBaseHandleTests, PrepareNextFrameAggregatesQueuedSingleMessagesIntoOneFrame) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd8Bus();

    for (uint8_t i = 0; i < 8; ++i) {
        handle.Queue(MakeMessage(0, static_cast<uint32_t>(0x520 + i), i));
    }

    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    ASSERT_EQ(frame.size(), 1);
    ASSERT_FALSE(frame[0].empty());

    const auto* packet = reinterpret_cast<const EthercatCanFdMsg8*>(frame[0].data());
    for (std::size_t slot = 0; slot < 8; ++slot) {
        EXPECT_EQ(packet->motor[slot].id, static_cast<uint32_t>(0x520 + slot));
    }

    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, AsynchronousBatchesContinueSharingOutputFrames) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();

    encos::MotorMessages first_batch;
    for (std::uint32_t id = 0x600; id < 0x604; ++id) {
        first_batch.push_back(MakeMessage(0, id, 0));
    }
    encos::MotorMessages second_batch;
    for (std::uint32_t id = 0x700; id < 0x702; ++id) {
        second_batch.push_back(MakeMessage(0, id, 0));
    }

    handle.Queue(first_batch);
    handle.Queue(second_batch);

    TestEthercatBaseHandle::Frame first_frame;
    TestEthercatBaseHandle::Frame second_frame;
    TestEthercatBaseHandle::Frame extra_frame;
    ASSERT_TRUE(handle.NextFrame(first_frame));
    ASSERT_TRUE(handle.NextFrame(second_frame));
    EXPECT_FALSE(handle.NextFrame(extra_frame));

    const auto* first = reinterpret_cast<const EthercatClassicCanMsg8*>(first_frame[0].data());
    const auto* second = reinterpret_cast<const EthercatClassicCanMsg8*>(second_frame[0].data());
    EXPECT_EQ(first->motor[0].id, 0x600U);
    EXPECT_EQ(first->motor[2].id, 0x602U);
    EXPECT_EQ(second->motor[0].id, 0x603U);
    EXPECT_EQ(second->motor[1].id, 0x700U);
    EXPECT_EQ(second->motor[2].id, 0x701U);
}

TEST(EthercatBaseHandleTests, ImmediateAfterCommitFillsRemainingSlot) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    handle.QueueSynchronized({MakeMessage(0, 1, 1), MakeMessage(0, 2, 2)});
    handle.Queue(MakeMessage(0, 3, 3));
    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 1U);
    EXPECT_EQ(packet->motor[1].id, 2U);
    EXPECT_EQ(packet->motor[2].id, 3U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, ImmediateDoesNotPreemptPreviouslyPackedControlBacklog) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    handle.QueueSynchronized({MakeMessage(0, 1, 1), MakeMessage(0, 2, 2), MakeMessage(0, 3, 3),
                              MakeMessage(0, 4, 4), MakeMessage(0, 5, 5), MakeMessage(0, 6, 6)});
    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    handle.Queue(MakeMessage(0, 7, 7));
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 4U);
    EXPECT_EQ(packet->motor[1].id, 5U);
    EXPECT_EQ(packet->motor[2].id, 6U);
    ASSERT_TRUE(handle.NextFrame(frame));
    packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 7U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, UnifiedBacklogPruningAlsoDiscardsImmediateRequests) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    handle.Queue(MakeMessage(0, 7, 7));
    for (uint32_t id = 1; id <= 4; ++id) {
        handle.QueueSynchronized({MakeMessage(0, id, 0)});
    }
    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 4U);
    EXPECT_EQ(packet->motor[1].len, 0U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, ImmediateBacklogIsPrunedDuringEnqueueWithoutWaitingForSend) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    for (uint32_t id = 1; id <= 13; ++id) {
        handle.Queue(MakeMessage(0, id, 0));
    }
    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 10U);
    EXPECT_EQ(packet->motor[1].id, 11U);
    EXPECT_EQ(packet->motor[2].id, 12U);
    ASSERT_TRUE(handle.NextFrame(frame));
    packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 13U);
    EXPECT_EQ(packet->motor[1].len, 0U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, CommitBacklogIsPrunedDuringEnqueueWithoutWaitingForSend) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    for (uint32_t id = 1; id <= 5; ++id) {
        handle.QueueSynchronized({MakeMessage(0, id, 0)});
    }
    TestEthercatBaseHandle::Frame frame;
    for (const uint32_t id : {4U, 5U}) {
        ASSERT_TRUE(handle.NextFrame(frame));
        const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
        EXPECT_EQ(packet->motor[0].id, id);
        EXPECT_EQ(packet->motor[1].len, 0U);
    }
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, ImmediateRequestsIgnoreEarlierCommitGenerationsAndKeepFifo) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    handle.QueueSynchronized({MakeMessage(0, 1, 0)});
    handle.QueueSynchronized({MakeMessage(0, 2, 0)});
    handle.Queue({MakeMessage(0, 3, 0), MakeMessage(0, 4, 0), MakeMessage(0, 5, 0)});
    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 1U);
    EXPECT_EQ(packet->motor[1].id, 3U);
    EXPECT_EQ(packet->motor[2].id, 4U);
    ASSERT_TRUE(handle.NextFrame(frame));
    packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 2U);
    EXPECT_EQ(packet->motor[1].id, 5U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, SynchronizedBatchFillsOrdinarySlotsWithoutReplacingSameMotor) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    handle.Queue(MakeMessage(0, 0x600, 1));
    handle.QueueSynchronized({MakeMessage(0, 0x600, 2), MakeMessage(0, 0x601, 3)});

    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor_num, 3);
    EXPECT_EQ(packet->motor[0].id, 0x600U);
    EXPECT_EQ(packet->motor[0].data[0], 1);
    EXPECT_EQ(packet->motor[1].id, 0x600U);
    EXPECT_EQ(packet->motor[1].data[0], 2);
    EXPECT_EQ(packet->motor[2].id, 0x601U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, SynchronizedOverflowFillsAlreadyQueuedOrdinaryTailInOrder) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    handle.Queue({MakeMessage(0, 0x600, 1), MakeMessage(0, 0x601, 2), MakeMessage(0, 0x602, 3),
                  MakeMessage(0, 0x603, 4)});
    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    handle.QueueSynchronized(
        {MakeMessage(0, 0x600, 5), MakeMessage(0, 0x604, 6), MakeMessage(0, 0x605, 7)});
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* tail = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(tail->motor[0].id, 0x603U);
    EXPECT_EQ(tail->motor[1].id, 0x600U);
    EXPECT_EQ(tail->motor[2].id, 0x604U);
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* overflow = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(overflow->motor[0].id, 0x605U);
    EXPECT_EQ(overflow->motor[1].len, 0);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, ImmediateMessagesBypassCommitBoundariesButControlsDoNot) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    handle.Queue(MakeMessage(0, 0x600, 1));
    handle.QueueSynchronized({MakeMessage(0, 0x601, 2)});
    handle.Queue(MakeMessage(0, 0x602, 3));
    handle.QueueSynchronized({MakeMessage(0, 0x603, 4)});

    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 0x600U);
    EXPECT_EQ(packet->motor[1].id, 0x601U);
    EXPECT_EQ(packet->motor[2].id, 0x602U);
    ASSERT_TRUE(handle.NextFrame(frame));
    packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 0x603U);
    EXPECT_EQ(packet->motor[1].len, 0);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, EmptySynchronizedBatchDoesNotSplitOrdinaryMessages) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();
    handle.Queue(MakeMessage(0, 0x600, 1));
    handle.QueueSynchronized({});
    handle.Queue(MakeMessage(0, 0x601, 2));
    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 0x600U);
    EXPECT_EQ(packet->motor[1].id, 0x601U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, SynchronizedBatchFillsEachBusAndSlaveIndependently) {
    TestEthercatBaseHandle handle;
    handle.ConfigureByOutputPdoSizes(
        {sizeof(EthercatClassicCanMsg8), sizeof(EthercatClassicCanMsg8)});
    constexpr int kSecondSlaveBusZero = 1 << 16;
    for (const int bus : {0, 1, kSecondSlaveBusZero}) {
        handle.Queue(MakeMessage(bus, 0x600, 1));
    }
    handle.QueueSynchronized({MakeMessage(0, 0x601, 2), MakeMessage(1, 0x601, 2),
                              MakeMessage(kSecondSlaveBusZero, 0x601, 2)});
    handle.QueueSynchronized({MakeMessage(0, 0x602, 3)});
    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    for (const int bus : {0, 1, kSecondSlaveBusZero}) {
        ASSERT_FALSE(frame[bus >> 16].empty());
        const auto* packet =
            reinterpret_cast<const EthercatClassicCanMsg8*>(frame[bus >> 16].data());
        const auto slot = (bus & 0xFFFF) * 3;
        EXPECT_EQ(packet->motor[slot].id, 0x600U);
        EXPECT_EQ(packet->motor[slot + 1].id, 0x601U);
        EXPECT_EQ(packet->motor[slot + 2].len, 0);
    }
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 0x602U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, SynchronizedBatchesUseExclusiveOutputFrames) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();

    encos::MotorMessages first_batch;
    for (std::uint32_t id = 0x600; id < 0x604; ++id) {
        first_batch.push_back(MakeMessage(0, id, 0));
    }
    encos::MotorMessages second_batch;
    for (std::uint32_t id = 0x700; id < 0x702; ++id) {
        second_batch.push_back(MakeMessage(0, id, 0));
    }

    handle.QueueSynchronized(first_batch);

    TestEthercatBaseHandle::Frame first_frame;
    TestEthercatBaseHandle::Frame second_frame;
    TestEthercatBaseHandle::Frame third_frame;
    TestEthercatBaseHandle::Frame extra_frame;
    ASSERT_TRUE(handle.NextFrame(first_frame));
    handle.QueueSynchronized(second_batch);
    ASSERT_TRUE(handle.NextFrame(second_frame));
    ASSERT_TRUE(handle.NextFrame(third_frame));
    EXPECT_FALSE(handle.NextFrame(extra_frame));

    const auto* first = reinterpret_cast<const EthercatClassicCanMsg8*>(first_frame[0].data());
    const auto* second = reinterpret_cast<const EthercatClassicCanMsg8*>(second_frame[0].data());
    const auto* third = reinterpret_cast<const EthercatClassicCanMsg8*>(third_frame[0].data());
    EXPECT_EQ(first->motor[0].id, 0x600U);
    EXPECT_EQ(first->motor[2].id, 0x602U);
    EXPECT_EQ(second->motor[0].id, 0x603U);
    EXPECT_EQ(second->motor[1].len, 0U);
    EXPECT_EQ(third->motor[0].id, 0x700U);
    EXPECT_EQ(third->motor[1].id, 0x701U);
}

TEST(EthercatBaseHandleTests, HighBacklogKeepsOnlyTheNewestQueuedFrame) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();

    handle.QueueSynchronized({MakeMessage(0, 0x600, 0)});
    handle.QueueSynchronized({MakeMessage(0, 0x601, 0)});
    handle.QueueSynchronized({MakeMessage(0, 0x602, 0)});
    handle.QueueSynchronized({MakeMessage(0, 0x603, 0)});

    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 0x603U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, HighBacklogThresholdAllowsThreeQueuedFrames) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();

    handle.QueueSynchronized({MakeMessage(0, 0x600, 0)});
    handle.QueueSynchronized({MakeMessage(0, 0x601, 0)});
    handle.QueueSynchronized({MakeMessage(0, 0x602, 0)});

    for (const uint32_t expected_id : {0x600U, 0x601U, 0x602U}) {
        TestEthercatBaseHandle::Frame frame;
        ASSERT_TRUE(handle.NextFrame(frame));
        const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
        EXPECT_EQ(packet->motor[0].id, expected_id);
    }
}

TEST(EthercatBaseHandleTests, SynchronizedBoundariesAreScopedToTheirBus) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();

    handle.QueueSynchronized({MakeMessage(0, 0x600, 0), MakeMessage(0, 0x601, 0),
                              MakeMessage(0, 0x602, 0), MakeMessage(0, 0x603, 0)});
    handle.QueueSynchronized({MakeMessage(0, 0x700, 0), MakeMessage(0, 0x701, 0)});
    handle.Queue({MakeMessage(1, 0x800, 0), MakeMessage(1, 0x801, 0), MakeMessage(1, 0x802, 0),
                  MakeMessage(1, 0x803, 0)});

    TestEthercatBaseHandle::Frame first_frame;
    TestEthercatBaseHandle::Frame second_frame;
    TestEthercatBaseHandle::Frame third_frame;
    ASSERT_TRUE(handle.NextFrame(first_frame));
    ASSERT_TRUE(handle.NextFrame(second_frame));
    ASSERT_TRUE(handle.NextFrame(third_frame));

    const auto* first = reinterpret_cast<const EthercatClassicCanMsg8*>(first_frame[0].data());
    const auto* second = reinterpret_cast<const EthercatClassicCanMsg8*>(second_frame[0].data());
    const auto* third = reinterpret_cast<const EthercatClassicCanMsg8*>(third_frame[0].data());
    EXPECT_EQ(first->motor[0].id, 0x600U);
    EXPECT_EQ(second->motor[0].id, 0x603U);
    EXPECT_EQ(second->motor[1].len, 0U);
    EXPECT_EQ(third->motor[0].id, 0x700U);
    EXPECT_EQ(third->motor[1].id, 0x701U);
    EXPECT_EQ(first->motor[3].id, 0x800U);
    EXPECT_EQ(second->motor[3].id, 0x803U);
}

TEST(EthercatBaseHandleTests, SameLocalBusOnDifferentSlavesUsesIndependentGenerations) {
    TestEthercatBaseHandle handle;
    handle.ConfigureByOutputPdoSizes(
        {sizeof(EthercatClassicCanMsg8), sizeof(EthercatClassicCanMsg8)});
    constexpr int kSecondSlaveBusZero = 1 << 16;

    handle.QueueSynchronized({MakeMessage(0, 0x600, 0), MakeMessage(0, 0x601, 0),
                              MakeMessage(0, 0x602, 0), MakeMessage(0, 0x603, 0)});
    handle.QueueSynchronized(
        {MakeMessage(kSecondSlaveBusZero, 0x700, 0), MakeMessage(kSecondSlaveBusZero, 0x701, 0)});
    handle.QueueSynchronized({MakeMessage(0, 0x800, 0), MakeMessage(0, 0x801, 0)});

    TestEthercatBaseHandle::Frame first_frame;
    TestEthercatBaseHandle::Frame second_frame;
    TestEthercatBaseHandle::Frame third_frame;
    ASSERT_TRUE(handle.NextFrame(first_frame));
    ASSERT_TRUE(handle.NextFrame(second_frame));
    ASSERT_TRUE(handle.NextFrame(third_frame));

    const auto* first_slave_first =
        reinterpret_cast<const EthercatClassicCanMsg8*>(first_frame[0].data());
    const auto* second_slave_first =
        reinterpret_cast<const EthercatClassicCanMsg8*>(first_frame[1].data());
    const auto* first_slave_second =
        reinterpret_cast<const EthercatClassicCanMsg8*>(second_frame[0].data());
    const auto* first_slave_third =
        reinterpret_cast<const EthercatClassicCanMsg8*>(third_frame[0].data());
    EXPECT_EQ(first_slave_first->motor[0].id, 0x600U);
    EXPECT_EQ(second_slave_first->motor[0].id, 0x700U);
    EXPECT_EQ(first_slave_second->motor[0].id, 0x603U);
    EXPECT_EQ(first_slave_third->motor[0].id, 0x800U);
    EXPECT_EQ(first_slave_third->motor[1].id, 0x801U);
}

TEST(EthercatBaseHandleTests, ClassicCanScanBurstKeepsOnlyNewestFrameUnderHighBacklog) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();

    encos::MotorMessages scan_messages;
    scan_messages.reserve(0x7FFU * 2U);
    for (int pass = 0; pass < 2; ++pass) {
        for (std::uint32_t candidate_id = 0; candidate_id < 0x7FFU; ++candidate_id) {
            auto message = MakeMessage(0, candidate_id, 0);
            message.data.len = 2;
            message.data.data[0] = static_cast<std::uint8_t>(0x07U << 5U);
            message.data.data[1] = static_cast<std::uint8_t>(encos::MotorParameter::Position);
            scan_messages.push_back(message);
        }
    }
    handle.Queue(scan_messages);

    TestEthercatBaseHandle::Frame frame;
    ASSERT_TRUE(handle.NextFrame(frame));
    ASSERT_EQ(frame.size(), 1U);
    ASSERT_EQ(frame[0].size(), sizeof(EthercatClassicCanMsg8));
    const auto* packet = reinterpret_cast<const EthercatClassicCanMsg8*>(frame[0].data());
    EXPECT_EQ(packet->motor[0].id, 0x7FDU);
    EXPECT_EQ(packet->motor[0].data[0], static_cast<uint8_t>(0x07U << 5U));
    EXPECT_EQ(packet->motor[1].id, 0x7FEU);
    EXPECT_EQ(packet->motor[2].len, 0U);
    EXPECT_FALSE(handle.NextFrame(frame));
}

TEST(EthercatBaseHandleTests, CanFd8BusDecodeSkipsZeroLengthBlocksAndKeepsDuplicates) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd8Bus();

    EthercatCanFdMsg8 packet{};
    packet.motor[0].id = 0;
    packet.motor[0].len = 1;
    packet.motor[1].id = 0x321;
    packet.motor[1].frame_flags = encos::kCanFrameFlagEff | encos::kCanFrameFlagFdMask;
    packet.motor[1].len = 2;
    packet.motor[1].data[0] = 0x12;
    packet.motor[1].data[1] = 0x34;

    std::vector<uint8_t> raw(sizeof(packet));
    std::memcpy(raw.data(), &packet, sizeof(packet));
    std::vector<const uint8_t*> inputs{raw.data()};

    auto decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 2);
    EXPECT_EQ(decoded[0].bus_idx, 0);
    EXPECT_EQ(decoded[0].data.id, 0u);
    EXPECT_EQ(decoded[1].bus_idx, 0);
    EXPECT_EQ(decoded[1].data.id, 0x321u);
    EXPECT_EQ(decoded[1].data.frame_flags, encos::kCanFrameFlagEff | encos::kCanFrameFlagFdMask);

    decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 2);
    EXPECT_EQ(decoded[0].data.id, 0u);
    EXPECT_EQ(decoded[1].data.id, 0x321u);

    packet.motor[1].data[1] = 0x35;
    std::memcpy(raw.data(), &packet, sizeof(packet));
    decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 2);
    EXPECT_EQ(decoded[1].data.data[1], 0x35);
}

TEST(EthercatBaseHandleTests, CanFd3BusPacksEightSlotsPerBusAndSplitsOverflow) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd3Bus();

    encos::MotorMessages messages;
    for (uint8_t i = 0; i < 9; ++i) {
        messages.push_back(MakeMessage(2, static_cast<uint32_t>(0x420 + i), i));
    }

    const auto frames = handle.Pack(messages);

    ASSERT_EQ(frames.size(), 2);
    ASSERT_EQ(frames[0].size(), 1);
    ASSERT_EQ(frames[0][0].size(), sizeof(EthercatCanFdMsg3));
    const auto* first = reinterpret_cast<const EthercatCanFdMsg3*>(frames[0][0].data());
    const auto* second = reinterpret_cast<const EthercatCanFdMsg3*>(frames[1][0].data());

    EXPECT_EQ(first->motor[16].id, 0x420u);
    EXPECT_EQ(first->motor[23].id, 0x427u);
    EXPECT_EQ(second->motor[16].id, 0x428u);
    EXPECT_EQ(second->motor[17].len, 0);
}

TEST(EthercatBaseHandleTests, CanFd3BusRejectsFourthBus) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd3Bus();

    encos::MotorMessages messages;
    messages.push_back(MakeMessage(3, 0x520, 0x10));

    const auto frames = handle.Pack(messages);

    EXPECT_TRUE(frames.empty());
}

TEST(EthercatBaseHandleTests, CanFd3BusDecodeSkipsZeroLengthBlocksAndKeepsDuplicates) {
    TestEthercatBaseHandle handle;
    handle.ConfigureCanFd3Bus();

    EthercatCanFdMsg3 packet{};
    packet.motor[8].id = 0x621;
    packet.motor[8].len = 2;
    packet.motor[8].data[0] = 0x56;
    packet.motor[8].data[1] = 0x78;

    std::vector<uint8_t> raw(sizeof(packet));
    std::memcpy(raw.data(), &packet, sizeof(packet));
    std::vector<const uint8_t*> inputs{raw.data()};

    auto decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].bus_idx, 1);
    EXPECT_EQ(decoded[0].data.id, 0x621u);

    decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].data.id, 0x621u);

    packet.motor[8].data[1] = 0x79;
    std::memcpy(raw.data(), &packet, sizeof(packet));
    decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].data.data[1], 0x79);
}

TEST(EthercatBaseHandleTests, ClassicCan2BusDecodeKeepsDuplicates) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan2Bus();

    EthercatClassicCanMsg2 packet{};
    packet.motor_num = 1;
    packet.motor[0].id = 0x421;
    packet.motor[0].len = 2;
    packet.motor[0].data[0] = 0x11;
    packet.motor[0].data[1] = 0x22;

    std::vector<uint8_t> raw(sizeof(packet));
    std::memcpy(raw.data(), &packet, sizeof(packet));
    std::vector<const uint8_t*> inputs{raw.data()};

    auto decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].data.id, 0x421u);

    decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].data.id, 0x421u);
    EXPECT_EQ(decoded[0].data.data[1], 0x22);

    packet.motor[0].data[1] = 0x23;
    std::memcpy(raw.data(), &packet, sizeof(packet));
    decoded = handle.Decode(inputs);
    ASSERT_EQ(decoded.size(), 1);
    EXPECT_EQ(decoded[0].data.data[1], 0x23);
}

TEST(EthercatBaseHandleTests, ClassicCan8BusStillUsesThreeSlotsPerBus) {
    TestEthercatBaseHandle handle;
    handle.ConfigureClassicCan8Bus();

    encos::MotorMessages messages;
    for (uint8_t i = 0; i < 4; ++i) {
        messages.push_back(MakeMessage(1, static_cast<uint32_t>(0x220 + i), i));
    }

    const auto frames = handle.Pack(messages);

    ASSERT_EQ(frames.size(), 2);
    const auto* first = reinterpret_cast<const EthercatClassicCanMsg8*>(frames[0][0].data());
    const auto* second = reinterpret_cast<const EthercatClassicCanMsg8*>(frames[1][0].data());
    EXPECT_EQ(first->motor_num, 3);
    EXPECT_EQ(first->motor[3].id, 0x220u);
    EXPECT_EQ(first->motor[5].id, 0x222u);
    EXPECT_EQ(second->motor_num, 1);
    EXPECT_EQ(second->motor[3].id, 0x223u);
}
