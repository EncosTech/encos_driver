#include "serial_port.h"

#include <emscripten.h>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
struct WebSerialState {
    encos::SerialPort::ReceiveCallback receive;
    encos::SerialPort::EventCallback disconnected;
    bool closed = false;
    bool attached = false;

    void Receive(const std::byte* data, std::size_t size) const {
        if (!closed && receive) {
            auto callback = receive;
            callback(data, size);
        }
    }

    void Disconnected() {
        if (closed) {
            return;
        }
        closed = true;
        attached = false;
        auto callback = std::move(disconnected);
        receive = {};
        if (callback) {
            callback();
        }
    }
};

EM_JS(int, PortAvailable, (const char* name), {
    const status = Module['webSerial']['status'](UTF8ToString(name));
    return status.open && !status.attached ? 1 : 0;
});
EM_JS(int, AttachPort, (const char* name, void* port),
      { return Module['webSerial']['_attach'](UTF8ToString(name), port) ? 1 : 0; });
EM_JS(int, WritePort, (const char* name, const void* data, int size), {
    return Module['webSerial']['_write'](UTF8ToString(name), HEAPU8.slice(data, data + size)) ? 1
                                                                                              : 0;
});
EM_JS(void, DetachPort, (const char* name, void* port),
      { Module['webSerial']['_detach'](UTF8ToString(name), port); });
}  // namespace

namespace encos {
struct SerialPort::Impl {
    explicit Impl(const std::string& name) : path(name) {}

    std::string path;
    WebSerialState bridge;
    bool started = false;
};

SerialPort::SerialPort(const std::string& path) : impl_(std::make_unique<Impl>(path)) {
    if (!PortAvailable(path.c_str())) {
        throw std::runtime_error(
            "Web Serial port is not open or already attached; await webSerial.requestPort first");
    }
}

SerialPort::~SerialPort() {
    Close();
}

void SerialPort::Start(ReceiveCallback receive, EventCallback disconnected,
                       EventCallback /*priority_failed*/) {
    if (impl_->started || impl_->bridge.closed) {
        throw std::logic_error("Serial port receive loop cannot be restarted");
    }
    if (!receive) {
        throw std::invalid_argument("Serial port receive callback is required");
    }
    impl_->started = true;
    impl_->bridge.receive = std::move(receive);
    impl_->bridge.disconnected = std::move(disconnected);
    if (!AttachPort(impl_->path.c_str(), &impl_->bridge)) {
        impl_->bridge.closed = true;
        impl_->bridge.receive = {};
        impl_->bridge.disconnected = {};
        throw std::runtime_error("Web Serial port is not open or already attached");
    }
    impl_->bridge.attached = true;
}

bool SerialPort::Write(const void* data, std::size_t size) {
    return Ok() && impl_->bridge.attached &&
           size <= static_cast<std::size_t>(std::numeric_limits<int>::max()) &&
           WritePort(impl_->path.c_str(), data, static_cast<int>(size));
}

bool SerialPort::Ok() const {
    return !impl_->bridge.closed;
}

void SerialPort::Close() {
    impl_->bridge.closed = true;
    if (impl_->bridge.attached) {
        DetachPort(impl_->path.c_str(), &impl_->bridge);
        impl_->bridge.attached = false;
    }
    impl_->bridge.receive = {};
    impl_->bridge.disconnected = {};
}
}  // namespace encos

extern "C" {
EMSCRIPTEN_KEEPALIVE int encos_serial_port_receive(void* context, const std::byte* data,
                                                   std::size_t size) {
    try {
        if (context) {
            static_cast<WebSerialState*>(context)->Receive(data, size);
        }
        return 1;
    } catch (...) {
        return 0;
    }
}

EMSCRIPTEN_KEEPALIVE void encos_serial_port_closed(void* context) {
    try {
        if (context) {
            static_cast<WebSerialState*>(context)->Disconnected();
        }
    } catch (...) {}
}
}
