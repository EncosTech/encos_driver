#include <atomic>
#include <cstdint>

#include "encos/encos_driver.h"

int main() {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    encos::Port<3> port;
    port.Push({});
    return port.Pop().has_value() ? 0 : 1;
}
