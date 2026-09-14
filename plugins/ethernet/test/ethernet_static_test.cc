#include <algorithm>
#include <bpf/libbpf.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <sys/capability.h>
#include <unistd.h>

#include "encos/encos_driver.h"
#include "ethernet/transport/transport.h"
#include "ethernet_xdp_embedded.generated.h"

namespace encos::ethernet {
TEST(EthernetStatic, RegistersFactoryWithoutPluginDirectory) {
    const auto types = GetAvailableAdapterTypes();
    EXPECT_NE(std::find(types.begin(), types.end(), "Ethernet"), types.end());
    EXPECT_THROW(MakeAdapter("Ethernet", "__invalid_interface_name__"), std::invalid_argument);
}

TEST(EthernetStatic, MissingProcessCapabilitiesFailsBeforeNetworkChanges) {
    ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
    ASSERT_EXIT(
        {
            cap_t empty = cap_init();
            if (!empty || cap_set_proc(empty) != 0)
                _exit(2);
            cap_free(empty);
            try {
                ConfigureHostInterface("lo");
            } catch (const std::runtime_error& error) {
                const std::string message = error.what();
                _exit(message.find("Static Ethernet requires effective") != std::string::npos &&
                              message.find("cap_bpf") != std::string::npos
                          ? 0
                          : 3);
            }
            _exit(4);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST(EthernetStatic, EmbeddedFilterContainsProgramAndMaps) {
    bpf_object* object =
        bpf_object__open_mem(kEmbeddedXdpFilter, sizeof(kEmbeddedXdpFilter), nullptr);
    ASSERT_NE(object, nullptr);
    ASSERT_EQ(libbpf_get_error(object), 0);
    std::unique_ptr<bpf_object, decltype(&bpf_object__close)> owner(object, bpf_object__close);
    EXPECT_NE(bpf_object__find_program_by_name(object, "ethernet_redirect"), nullptr);
    auto* sockets = bpf_object__find_map_by_name(object, "sockets");
    ASSERT_NE(sockets, nullptr);
    EXPECT_EQ(bpf_map__type(sockets), BPF_MAP_TYPE_XSKMAP);
    EXPECT_NE(bpf_object__find_map_by_name(object, "filters"), nullptr);
}
}  // namespace encos::ethernet
