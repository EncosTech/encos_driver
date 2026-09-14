// clang-format off
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on
#include <stdexcept>
#include <vector>

#include "ethercat_handle.h"

namespace {
std::string Utf8(const wchar_t* value) {
    if (!value)
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}
std::string ResolveDevice(const std::string& name) {
    if (name.rfind("\\Device\\NPF_", 0) == 0)
        return name;
    ULONG size = 16384;
    std::vector<unsigned char> buffer(size);
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        const ULONG status = GetAdaptersAddresses(AF_UNSPEC, 0, nullptr, adapters, &size);
        if (status == ERROR_BUFFER_OVERFLOW) {
            buffer.resize(size);
            continue;
        }
        if (status != NO_ERROR)
            throw std::runtime_error("Cannot enumerate EtherCAT interfaces");
        for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
            if (adapter->AdapterName &&
                (name == adapter->AdapterName || name == Utf8(adapter->FriendlyName)))
                return std::string("\\Device\\NPF_") + adapter->AdapterName;
        }
        break;
    }
    throw std::invalid_argument("Unknown EtherCAT interface: " + name);
}
}  // namespace

bool EthercatHandle::OpenPort() {
    const auto device = ResolveDevice(ifname_);
    if (ecx_init(&ctx_, device.c_str()) > 0)
        return true;
    logger_->error("Failed to open EtherCAT capture device '{}'.", device);
    return false;
}
