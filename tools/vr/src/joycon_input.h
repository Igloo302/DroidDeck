#pragma once
#include "joycon_shm.h"
#include <cmath>

namespace droiddeck {

inline constexpr float kJoyConStickTouch = 0.1f;
inline constexpr double kJoyConCurlSeconds = 0.05;

inline bool JoyConStickTouched(uint32_t buttons, float x, float y) {
    return (buttons & DROIDDECK_JOYCON_STICK) || x * x + y * y > kJoyConStickTouch * kJoyConStickTouch;
}

inline double JoyConCurlFollow(double seconds) {
    return 1.0 - std::exp(-seconds / kJoyConCurlSeconds);
}

}
