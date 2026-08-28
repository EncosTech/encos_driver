#include "ethercat_base_handle.h"

#include <cstring>
#include <gtest/gtest.h>
#include <vector>

#include "platform/log.h"
#include "plugins/ethercat/ethercat_handle.h"

namespace {

TEST(EthercatLoopPeriodTests, SoemAdapterDefaultsToOneKilohertz) {
    EXPECT_EQ(EthercatHandle::kDefaultLoopPeriod, std::chrono::microseconds(1000));
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

    std::vector<int> ConfigureByOutputPdoSizes(
        std::initializer_list<std::size_t> output_pdo_sizes) {
        slave_configs_.clear();
        for (const auto output_pdo_size : output_pdo_sizes) {
            slave_configs_.push_back(ClassifyOutputPdoSize(output_pdo_size));
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

    bool NextFrame(Frame& frame, std::size_t slave_count = 1) {
        return PrepareNextFrame(frame, slave_count);
    }

    std::vector<Frame> Pack(const encos::MotorMessages& messages) const {
        return PackMessages(messages, 1);
    }

    encos::MotorMessages Decode(const std::vector<const uint8_t*>& inputs) {
        return DecodeInputs(inputs, 1);
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

}  // namespace

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
    ASSERT_TRUE(handle.NextFrame(first_frame, 2));
    ASSERT_TRUE(handle.NextFrame(second_frame, 2));
    ASSERT_TRUE(handle.NextFrame(third_frame, 2));

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

TEST(EthercatBaseHandleTests, ClassicCanScanBurstKeepsOnlyTheNewestFrameUnderHighBacklog) {
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

    std::size_t frame_count = 0;
    TestEthercatBaseHandle::Frame frame;
    while (handle.NextFrame(frame)) {
        ASSERT_EQ(frame.size(), 1U);
        ASSERT_FALSE(frame[0].empty());
        EXPECT_EQ(frame[0].size(), sizeof(EthercatClassicCanMsg8));
        ++frame_count;
    }

    EXPECT_EQ(frame_count, 1U);
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
