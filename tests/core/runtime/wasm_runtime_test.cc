#include "wasm/wasm_runtime.h"

#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <type_traits>

#include "driver_manager_test_access.h"
#include "encos/driver_manager.h"
#include "platform/os.h"
#include "test_adapter.h"

namespace encos::wasm {

static_assert(std::is_same_v<decltype(AdapterEntry::adapter), BaseAdapter*>);
static_assert(std::is_same_v<decltype(AdapterEntry::fake_adapter), FakeAdapter*>);
static_assert(std::is_same_v<decltype(BusEntry::bus), Bus*>);
static_assert(std::is_same_v<decltype(MotorEntry::motor), Motor*>);
static_assert(std::is_same_v<decltype(BatteryEntry::battery), Battery*>);
static_assert(std::is_same_v<decltype(ImuEntry::imu), Imu*>);
static_assert(static_cast<int>(ErrorCode::Ok) == 0);
static_assert(static_cast<int>(ErrorCode::InvalidArgument) == 1);
static_assert(static_cast<int>(ErrorCode::InvalidHandle) == 2);
static_assert(static_cast<int>(ErrorCode::Disposed) == 3);
static_assert(static_cast<int>(ErrorCode::WrongAdapterType) == 4);
static_assert(static_cast<int>(ErrorCode::OperationFailed) == 5);
static_assert(static_cast<int>(ErrorCode::NoResponse) == 6);
static_assert(static_cast<int>(ErrorCode::Unsupported) == 7);
static_assert(static_cast<int>(ErrorCode::ResourceExhausted) == 8);
static_assert(static_cast<int>(ErrorCode::InternalError) == 9);

struct RuntimeStoreTestAccess {
    static bool BeginPendingAdapterDeletion(RuntimeStore& store, std::uint32_t adapter_handle) {
        return store.BeginPendingAdapterDeletion(adapter_handle, {});
    }

    static void PollPendingAdapterDeletionsOnce(RuntimeStore& store) {
        store.PollPendingAdapterDeletionsOnce();
    }

    static bool PendingDeletionExhausted(const RuntimeStore& store, BaseAdapter* adapter) {
        const auto found = store.pending_adapter_deletions_.find(adapter);
        return found != store.pending_adapter_deletions_.end() && found->second.exhausted;
    }

    static bool HasPendingInterface(const RuntimeStore& store, const std::string& interface_name) {
        return store.pending_adapter_interfaces_.count(interface_name) != 0;
    }

    static std::size_t LeaseCount(const RuntimeStore& store, BaseAdapter* adapter) {
        const auto found = store.adapter_handle_leases_.find(adapter);
        return found == store.adapter_handle_leases_.end() ? 0 : found->second;
    }

    static bool HasAdapterTombstone(const RuntimeStore& store, std::uint32_t adapter_handle) {
        const auto found = store.adapters_.find(adapter_handle);
        return found != store.adapters_.end() && !found->second.valid && found->second.disposing;
    }
};

namespace {

class RuntimeStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        DriverManagerTestAccess::Reset(EncosDriverManager::Instance());
    }

    void TearDown() override {
        DriverManagerTestAccess::SetDeletionHook(EncosDriverManager::Instance(), {});
        DriverManagerTestAccess::Reset(EncosDriverManager::Instance());
    }
};

TEST_F(RuntimeStoreTest, FakeAdapterHandlesShareManagerObjectUntilLastLeaseDisposes) {
    RuntimeStore store;
    auto& manager = EncosDriverManager::Instance();
    int destroy_count = 0;
    DriverManagerTestAccess::SetDeletionHook(
        manager, [&destroy_count](EncosDriverManager::DeletionStage stage) {
            if (stage == EncosDriverManager::DeletionStage::BeforeAdapterDestroy) {
                ++destroy_count;
            }
        });

    const auto first = store.CreateFakeAdapter("wasm-runtime-leases", "", LogLevel::Info);
    const auto second = store.CreateFakeAdapter("wasm-runtime-leases", "", LogLevel::Info);
    ASSERT_TRUE(first.Ok());
    ASSERT_TRUE(second.Ok());
    ASSERT_NE(first.value, second.value);

    auto first_adapter = store.ResolveAdapter(first.value);
    auto second_adapter = store.ResolveAdapter(second.value);
    ASSERT_TRUE(first_adapter.Ok());
    ASSERT_TRUE(second_adapter.Ok());
    EXPECT_EQ(first_adapter.value->adapter, second_adapter.value->adapter);
    EXPECT_EQ(first_adapter.value->fake_adapter, second_adapter.value->fake_adapter);

    const auto first_bus = store.GetBus(first.value, -1, 0);
    ASSERT_TRUE(first_bus.Ok());
    ASSERT_TRUE(store.GetMotorWithModel(first_bus.value, 1, MotorModel::EC_A4310_P2).Ok());
    ASSERT_TRUE(store.GetBattery(first_bus.value, 0).Ok());
    ASSERT_TRUE(store.GetImu(first_bus.value, 0).Ok());

    EXPECT_EQ(store.DisposeAdapter(first.value), ErrorCode::Ok);
    EXPECT_EQ(destroy_count, 0);
    EXPECT_FALSE(store.ResolveBus(first_bus.value).Ok());
    EXPECT_TRUE(store.ResolveAdapter(second.value).Ok());
    EXPECT_TRUE(store.GetBus(second.value, -1, 0).Ok());

    EXPECT_EQ(store.DisposeAdapter(second.value), ErrorCode::Ok);
    EXPECT_EQ(destroy_count, 1);
    EXPECT_FALSE(store.ResolveAdapter(second.value).Ok());
}

TEST_F(RuntimeStoreTest, LastLeaseDestroysSharedFakeAdapterRegardlessOfDisposalOrder) {
    RuntimeStore store;
    auto& manager = EncosDriverManager::Instance();
    int destroy_count = 0;
    DriverManagerTestAccess::SetDeletionHook(
        manager, [&destroy_count](EncosDriverManager::DeletionStage stage) {
            if (stage == EncosDriverManager::DeletionStage::BeforeAdapterDestroy) {
                ++destroy_count;
            }
        });

    const auto first = store.CreateFakeAdapter("wasm-runtime-reverse-leases", "", LogLevel::Info);
    const auto second = store.CreateFakeAdapter("wasm-runtime-reverse-leases", "", LogLevel::Info);
    ASSERT_TRUE(first.Ok());
    ASSERT_TRUE(second.Ok());

    EXPECT_EQ(store.DisposeAdapter(second.value), ErrorCode::Ok);
    EXPECT_EQ(destroy_count, 0);
    EXPECT_TRUE(store.ResolveAdapter(first.value).Ok());

    EXPECT_EQ(store.DisposeAdapter(first.value), ErrorCode::Ok);
    EXPECT_EQ(destroy_count, 1);
}

TEST_F(RuntimeStoreTest, FinalDisposalInvalidatesDescendantsBeforeManagerDeletion) {
    RuntimeStore store;
    const auto adapter = store.CreateFakeAdapter("wasm-runtime-dispose-order", "", LogLevel::Info);
    ASSERT_TRUE(adapter.Ok());

    const auto bus = store.GetBus(adapter.value, -1, 0);
    ASSERT_TRUE(bus.Ok());
    const auto motor = store.GetMotorWithModel(bus.value, 1, MotorModel::EC_A4310_P2);
    ASSERT_TRUE(motor.Ok());
    const auto battery = store.GetBattery(bus.value, 0);
    ASSERT_TRUE(battery.Ok());
    const auto imu = store.GetImu(bus.value, 0);
    ASSERT_TRUE(imu.Ok());

    bool descendants_invalid_before_delete = false;
    DriverManagerTestAccess::SetDeletionHook(
        EncosDriverManager::Instance(), [&store, &descendants_invalid_before_delete, bus, motor,
                                         battery, imu](EncosDriverManager::DeletionStage stage) {
            if (stage != EncosDriverManager::DeletionStage::BeforeAdapterDestroy) {
                return;
            }
            descendants_invalid_before_delete =
                !store.ResolveBus(bus.value).Ok() && !store.ResolveMotor(motor.value).Ok() &&
                !store.ResolveBattery(battery.value).Ok() && !store.ResolveImu(imu.value).Ok();
        });

    EXPECT_EQ(store.DisposeAdapter(adapter.value), ErrorCode::Ok);
    EXPECT_TRUE(descendants_invalid_before_delete);
}

TEST_F(RuntimeStoreTest, NormalAdapterHandlesReuseTheManagerOwnedAdapter) {
    auto plugin_dir = std::filesystem::absolute("./plugins");
    if (!std::filesystem::exists(platform::PluginLibraryPath(plugin_dir, "Fake"))) {
        plugin_dir = std::filesystem::absolute("./build/plugins");
    }
    setenv("ENCOS_PLUGIN_PATH", plugin_dir.c_str(), 1);
    RuntimeStore store;
    auto& manager = EncosDriverManager::Instance();
    int destroy_count = 0;
    DriverManagerTestAccess::SetDeletionHook(
        manager, [&destroy_count](EncosDriverManager::DeletionStage stage) {
            if (stage == EncosDriverManager::DeletionStage::BeforeAdapterDestroy) {
                ++destroy_count;
            }
        });

    const auto first = store.CreateAdapter("Fake", "wasm-runtime-normal", "", LogLevel::Info);
    const auto second = store.CreateAdapter("Fake", "wasm-runtime-normal", "", LogLevel::Info);
    ASSERT_TRUE(first.Ok());
    ASSERT_TRUE(second.Ok());

    auto first_adapter = store.ResolveAdapter(first.value);
    auto second_adapter = store.ResolveAdapter(second.value);
    ASSERT_TRUE(first_adapter.Ok());
    ASSERT_TRUE(second_adapter.Ok());
    EXPECT_EQ(first_adapter.value->adapter, second_adapter.value->adapter);
    EXPECT_EQ(store.DisposeAdapter(second.value), ErrorCode::Ok);
    EXPECT_EQ(destroy_count, 0);
    EXPECT_EQ(store.DisposeAdapter(second.value), ErrorCode::InvalidHandle);
    EXPECT_TRUE(store.ResolveAdapter(first.value).Ok());

    EXPECT_EQ(store.DisposeAdapter(first.value), ErrorCode::Ok);
    EXPECT_EQ(destroy_count, 1);

    const auto next = store.CreateFakeAdapter("wasm-runtime-after-normal", "", LogLevel::Info);
    ASSERT_TRUE(next.Ok());
    EXPECT_GT(next.value, second.value);
    EXPECT_EQ(store.DisposeAdapter(next.value), ErrorCode::Ok);
}

TEST_F(RuntimeStoreTest, FakeCreationRejectsAnExistingNonFakeManagerAdapter) {
    constexpr char kInterfaceName[] = "wasm-runtime-non-fake";
    auto& manager = EncosDriverManager::Instance();
    auto* existing = manager.CreateAdapterWithFactory(kInterfaceName, [kInterfaceName] {
        return new TestAdapter(kInterfaceName);
    });
    ASSERT_NE(existing, nullptr);

    RuntimeStore store;
    const auto fake = store.CreateFakeAdapter(kInterfaceName, "", LogLevel::Info);
    EXPECT_FALSE(fake.Ok());
    EXPECT_EQ(fake.code, ErrorCode::WrongAdapterType);

    EXPECT_EQ(manager.CreateAdapterWithFactory(kInterfaceName,
                                               [kInterfaceName] {
                                                   return new TestAdapter(kInterfaceName);
                                               }),
              existing);
    ASSERT_TRUE(manager.DestroyAdapter(existing));
    const auto retry = store.CreateFakeAdapter(kInterfaceName, "", LogLevel::Info);
    ASSERT_TRUE(retry.Ok());
    EXPECT_EQ(store.DisposeAdapter(retry.value), ErrorCode::Ok);
}

TEST_F(RuntimeStoreTest, FailedFinalDeletionKeepsAdapterHandleRetriable) {
    constexpr char kInterfaceName[] = "wasm-runtime-retry-delete";
    auto& manager = EncosDriverManager::Instance();
    auto* adapter = static_cast<TestAdapter*>(
        manager.CreateAdapterWithFactory(kInterfaceName, [kInterfaceName] {
            return new TestAdapter(kInterfaceName);
        }));
    ASSERT_NE(adapter, nullptr);

    RuntimeStore store;
    const auto handle = store.CreateAdapter("unused", kInterfaceName, "", LogLevel::Info);
    ASSERT_TRUE(handle.Ok());
    const auto bus = store.GetBus(handle.value, -1, 0);
    ASSERT_TRUE(bus.Ok());
    const auto motor = store.GetMotorWithModel(bus.value, 1, MotorModel::EC_A4310_P2);
    ASSERT_TRUE(motor.Ok());
    const auto battery = store.GetBattery(bus.value, 0);
    ASSERT_TRUE(battery.Ok());
    const auto imu = store.GetImu(bus.value, 0);
    ASSERT_TRUE(imu.Ok());
    ErrorCode callback_result = ErrorCode::Ok;
    adapter->SetRawMessageCallbackForTests([&](const MotorMessages&) {
        callback_result = store.DisposeAdapter(handle.value);
    });

    adapter->SimulateOnMessage({MotorMessage{}});
    EXPECT_EQ(callback_result, ErrorCode::OperationFailed);
    EXPECT_TRUE(store.ResolveAdapter(handle.value).Ok());
    EXPECT_TRUE(store.ResolveBus(bus.value).Ok());
    EXPECT_TRUE(store.ResolveMotor(motor.value).Ok());
    EXPECT_TRUE(store.ResolveBattery(battery.value).Ok());
    EXPECT_TRUE(store.ResolveImu(imu.value).Ok());

    EXPECT_EQ(store.DisposeAdapter(handle.value), ErrorCode::Ok);
    EXPECT_FALSE(store.ResolveAdapter(handle.value).Ok());
}

TEST_F(RuntimeStoreTest, ExhaustedDeferredDeletionRetainsLeaseAndCanBeRetried) {
    constexpr char kInterfaceName[] = "wasm-runtime-exhausted-deletion";
    auto& manager = EncosDriverManager::Instance();
    auto* adapter = static_cast<TestAdapter*>(
        manager.CreateAdapterWithFactory(kInterfaceName, [kInterfaceName] {
            return new TestAdapter(kInterfaceName);
        }));
    ASSERT_NE(adapter, nullptr);

    RuntimeStore store;
    const auto handle = store.CreateAdapter("unused", kInterfaceName, "", LogLevel::Info);
    ASSERT_TRUE(handle.Ok());
    ASSERT_TRUE(RuntimeStoreTestAccess::BeginPendingAdapterDeletion(store, handle.value));
    store.SetLastError(ErrorCode::Unsupported, "sentinel error");

    adapter->SetRawMessageCallbackForTests([&](const MotorMessages&) {
        for (int retry = 0; retry < 64; ++retry) {
            RuntimeStoreTestAccess::PollPendingAdapterDeletionsOnce(store);
        }
    });
    adapter->SimulateOnMessage({MotorMessage{}});

    EXPECT_TRUE(RuntimeStoreTestAccess::PendingDeletionExhausted(store, adapter));
    EXPECT_TRUE(RuntimeStoreTestAccess::HasPendingInterface(store, kInterfaceName));
    EXPECT_EQ(RuntimeStoreTestAccess::LeaseCount(store, adapter), 1u);
    EXPECT_TRUE(RuntimeStoreTestAccess::HasAdapterTombstone(store, handle.value));
    EXPECT_EQ(store.LastError().code, ErrorCode::Unsupported);
    EXPECT_EQ(store.LastError().message, "sentinel error");

    const auto blocked = store.CreateAdapter("unused", kInterfaceName, "", LogLevel::Info);
    EXPECT_FALSE(blocked.Ok());
    EXPECT_EQ(blocked.code, ErrorCode::Disposed);
    EXPECT_EQ(manager.CreateAdapterWithFactory(kInterfaceName,
                                               [kInterfaceName] {
                                                   return new TestAdapter(kInterfaceName);
                                               }),
              adapter);

    EXPECT_EQ(store.DisposeAdapter(handle.value), ErrorCode::Ok);
    EXPECT_FALSE(RuntimeStoreTestAccess::HasPendingInterface(store, kInterfaceName));
    EXPECT_EQ(RuntimeStoreTestAccess::LeaseCount(store, adapter), 0u);
    EXPECT_FALSE(store.ResolveAdapter(handle.value).Ok());
}

}  // namespace
}  // namespace encos::wasm
