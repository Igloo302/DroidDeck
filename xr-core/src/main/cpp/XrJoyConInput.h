#pragma once
#include "joycon_input.h"
#include "xr_tracking.h"
#include "xr_input_v2.h"
#include "arm_model.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace droiddeck::xr {

constexpr int kJoyConSnapshotStride = 9;
using JoyConSnapshot = std::array<int32_t, 2*kJoyConSnapshotStride>;

inline DroidDeckXrHand joyConControls(const int32_t values[3]) {
    DroidDeckXrHand hand{};
    if (!(values[0] & (1 << 6))) return hand;
    hand.active = DROIDDECK_XR_JOYCON_ACTIVE;
    const auto buttons = values[0];
    hand.trigger = (buttons & DROIDDECK_JOYCON_TRIGGER) ? 1.f : 0.f;
    hand.squeeze = (buttons & DROIDDECK_JOYCON_GRIP) ? 1.f : 0.f;
    hand.stick[0] = std::clamp(values[1] / 32767.f, -1.f, 1.f);
    hand.stick[1] = std::clamp(values[2] / 32767.f, -1.f, 1.f);
    if (buttons & DROIDDECK_JOYCON_TRIGGER) hand.buttons |= DROIDDECK_XR_TRIGGER_TOUCH;
    if (buttons & DROIDDECK_JOYCON_A) hand.buttons |= DROIDDECK_XR_PRIMARY_CLICK | DROIDDECK_XR_PRIMARY_TOUCH;
    if (buttons & DROIDDECK_JOYCON_B) hand.buttons |= DROIDDECK_XR_SECONDARY_CLICK | DROIDDECK_XR_SECONDARY_TOUCH;
    if (buttons & DROIDDECK_JOYCON_STICK) hand.buttons |= DROIDDECK_XR_STICK_CLICK;
    if (JoyConStickTouched(buttons, hand.stick[0], hand.stick[1]))
        hand.buttons |= DROIDDECK_XR_STICK_TOUCH;
    if (buttons & DROIDDECK_JOYCON_SYSTEM) hand.buttons |= DROIDDECK_XR_MENU_CLICK;
    return hand;
}

class JoyConXrOrientation {
public:
    DroidDeckXrHand update(const int32_t values[kJoyConSnapshotStride], const DroidDeckXrPose &head,
                          uint64_t session, uint64_t origin) {
        auto hand = joyConControls(values);
        if (!hand.active) { aligned_ = false; connected_ = false; return hand; }
        if (!connected_ || epoch_ != values[8] || session_ != session || origin_ != origin) {
            aligned_ = false;
            epoch_ = values[8]; session_ = session; origin_ = origin;
        }
        connected_ = true;
        if (!values[7]) return hand;
        float orientation[4];
        std::memcpy(orientation, values + 3, sizeof(orientation));
        Quat imu;
        if (!decode(orientation, imu)) return hand;
        if (!aligned_) {
            Quat headRotation;
            double headYaw = 0, imuYaw = 0;
            if (!(head.flags & DROIDDECK_XR_ORIENTATION_VALID) || !decode(head.orientation, headRotation) ||
                !Heading(headRotation, &headYaw) || !Heading(imu, &imuYaw)) return hand;
            yaw_ = headYaw - imuYaw;
            aligned_ = true;
        }
        const auto world = Normalize(AboutY(yaw_) * imu);
        hand.pose.orientation[0] = float(world.x); hand.pose.orientation[1] = float(world.y);
        hand.pose.orientation[2] = float(world.z); hand.pose.orientation[3] = float(world.w);
        hand.pose.flags = DROIDDECK_XR_ORIENTATION_VALID | DROIDDECK_XR_ORIENTATION_TRACKED;
        return hand;
    }

private:
    static bool decode(const float values[4], Quat &result) {
        double norm = 0;
        for (float value : std::array<float, 4>{values[0], values[1], values[2], values[3]}) {
            if (!std::isfinite(value)) return false;
            norm += double(value)*value;
        }
        if (norm < .5 || norm > 1.5) return false;
        result = Normalize({values[3], values[0], values[1], values[2]});
        return true;
    }
    bool connected_ = false, aligned_ = false;
    int32_t epoch_ = 0;
    uint64_t session_ = 0, origin_ = 0;
    double yaw_ = 0;
};

}
