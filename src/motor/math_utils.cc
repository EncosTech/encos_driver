#include "math_utils.h"

#include <algorithm>

extern "C" {

int FloatToUint(float x, float x_min, float x_max, int bits) {
    const float span = x_max - x_min;
    const float offset = x_min;
    const float clamped = std::clamp(x, x_min, x_max);
    return static_cast<int>((clamped - offset) * (static_cast<float>((1 << bits) - 1)) / span);
}

float UintToFloat(int x_int, float x_min, float x_max, int bits) {
    const float span = x_max - x_min;
    const float offset = x_min;
    return static_cast<float>(x_int) * span / static_cast<float>((1 << bits) - 1) + offset;
}

}  // extern "C"
