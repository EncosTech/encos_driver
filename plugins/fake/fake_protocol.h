#pragma once

#include <optional>

#include "plugins/fake/fake_adapter.h"

namespace encos {

std::optional<MotorParameter> FakeWriteParameterFromRawId(uint8_t raw_id);
uint8_t FakeRawIdFromWriteParameter(MotorParameter parameter);
std::optional<FakeCommandRecord> DecodeFakeCommand(const MotorMessage& message,
                                                   const FakeMotorSnapshot* snapshot = nullptr);

}  // namespace encos
