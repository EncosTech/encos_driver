// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>

#include "ethercat_handle.h"
#ifndef ENCOS_STATIC_MODE
#include "fd_broker_client.h"
#include "platform/os.h"
namespace {
bool SetupPortFromFd(ecx_portt* port, int socket_fd) {
    if (port == nullptr || socket_fd < 0) {
        return false;
    }

#ifdef __linux__
    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setprotocol(&mutexattr, PTHREAD_PRIO_INHERIT);
    pthread_mutex_init(&(port->getindex_mutex), &mutexattr);
    pthread_mutex_init(&(port->tx_mutex), &mutexattr);
    pthread_mutex_init(&(port->rx_mutex), &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);
#endif

    port->sockhandle = socket_fd;
    port->lastidx = 0;
    // ECT_RED_NONE is internal to SOEM nicdrv.c; 0 is the non-redundant state.
    port->redstate = 0;
    port->stack.sock = &(port->sockhandle);
    port->stack.txbuf = &(port->txbuf);
    port->stack.txbuflength = &(port->txbuflength);
    port->stack.tempbuf = &(port->tempinbuf);
    port->stack.rxbuf = &(port->rxbuf);
    port->stack.rxbufstat = &(port->rxbufstat);
    port->stack.rxsa = &(port->rxsa);

    for (int i = 0; i < EC_MAXBUF; ++i) {
        port->rxbufstat[i] = EC_BUF_EMPTY;
        ec_setupheader(&(port->txbuf[i]));
    }
    ec_setupheader(&(port->txbuf2));

    return true;
}

}  // namespace
#endif

bool EthercatHandle::OpenPort() {
#ifdef ENCOS_STATIC_MODE
    return ecx_init(&ctx_, ifname_.c_str()) > 0;
#else
    const auto broker = encos::platform::PluginDir() / "EthercatFdBrokerExecutable";
    const int fd = encos::fd_broker::FdBrokerClient::RequestFd(broker.string(), ifname_,
                                                               "encos_motor_driver", logger_);
    if (fd < 0) {
        logger_->error("Failed to obtain raw socket for interface '{}'.", ifname_);
        return false;
    }
    ecx_initmbxpool(&ctx_);
    if (!SetupPortFromFd(&ctx_.port, fd)) {
        close(fd);
        return false;
    }
    return true;
#endif
}
