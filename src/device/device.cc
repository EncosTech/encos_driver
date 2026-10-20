// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#include "device/device.h"

#include <utility>

namespace encos {

struct Device::Impl {
    std::string type_name;
    std::string config;
    Bus* bus = nullptr;
    LoggerPtr logger;
    std::function<void(const MotorPackMsg&)> writer;
};

Device::Device(const DeviceCreateContext& context) : impl_(std::make_unique<Impl>()) {
    impl_->type_name = context.type_name;
    impl_->config = context.config;
    impl_->bus = context.bus;
    impl_->logger = context.logger;
    impl_->writer = context.writer;
}

Device::~Device() = default;

const std::string& Device::Type() const {
    return impl_->type_name;
}

const std::string& Device::Config() const {
    return impl_->config;
}

Bus* Device::GetBus() const {
    return impl_->bus;
}

void Device::Send(const MotorPackMsg& message) {
    if (impl_->writer) {
        impl_->writer(message);
    }
}

LoggerPtr Device::Logger() const {
    return impl_->logger;
}

}  // namespace encos
