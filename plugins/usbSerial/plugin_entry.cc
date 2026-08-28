#include "usb_serial_adapter.h"

extern "C" encos::BaseAdapter* MakeAdapter(const char* interface_name, const char* logger_name,
                                           int log_level) {
    return encos::UsbSerialAdapter::Create(interface_name, logger_name,
                                           encos::LogLevelFromInt(log_level));
}
