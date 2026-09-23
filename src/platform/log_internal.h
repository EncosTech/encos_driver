// SPDX-FileCopyrightText: 2026 Encos
// SPDX-License-Identifier: MIT

#pragma once

#include "platform/log.h"

namespace encos {
namespace detail {

inline bool ShouldLog(LogLevel message_level, LogLevel logger_level) {
    return logger_level != LogLevel::Off && message_level >= logger_level;
}

}  // namespace detail
}  // namespace encos
