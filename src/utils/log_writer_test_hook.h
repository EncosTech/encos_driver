#pragma once

#include <string>

#include "encos/export.h"

namespace encos::detail {

ENCOS_BASE_API void FailNextLogWriterTaskForTesting(const std::string& file_name);
ENCOS_BASE_API void FailNextLogWriterInteractionForTesting();

}  // namespace encos::detail
