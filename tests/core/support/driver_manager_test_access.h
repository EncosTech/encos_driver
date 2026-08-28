#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "encos/driver_manager.h"

namespace encos {

class DriverManagerTestAccess {
public:
    static void Reset(EncosDriverManager& manager) {
        manager.ResetForTests();
    }

    static void SetCreationHook(EncosDriverManager& manager,
                                EncosDriverManager::CreationHook hook) {
        manager.SetCreationHookForTests(std::move(hook));
    }

    static void SetDeviceInitializerHook(EncosDriverManager& manager,
                                         EncosDriverManager::DeviceInitializerHook hook) {
        manager.SetDeviceInitializerHookForTests(std::move(hook));
    }

    static void SetDeletionHook(EncosDriverManager& manager,
                                EncosDriverManager::DeletionHook hook) {
        manager.SetDeletionHookForTests(std::move(hook));
    }

    static void SetMigrationHook(EncosDriverManager& manager,
                                 EncosDriverManager::MigrationHook hook) {
        manager.SetMigrationHookForTests(std::move(hook));
    }

    static void SetWaitHook(EncosDriverManager& manager, std::function<void()> hook) {
        manager.SetWaitHookForTests(std::move(hook));
    }

    static void RunWithSlowPathLocks(EncosDriverManager& manager,
                                     const std::function<void()>& callback) {
        manager.RunWithSlowPathLocksForTests(callback);
    }

    static bool RegisterReceiveRoutes(
        EncosDriverManager& manager, void* device, BaseAdapter* adapter, Bus* bus,
        const std::vector<std::uint32_t>& can_ids, EncosDriverManager::ReceiveCallback callback,
        EncosDriverManager::CancellationCallback cancel_waiters = {}) {
        return manager.RegisterReceiveRoutes(device, adapter, bus, can_ids, std::move(callback),
                                             std::move(cancel_waiters));
    }
};

}  // namespace encos
