#pragma once
#include "joycon_input.h"
#include "xr_tracking.h"
#include "xr_input_v2.h"
#include "arm_model.h"
#include "XrJoyConHeading.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace droiddeck::xr {

constexpr int kJoyConSnapshotStride = 13;
constexpr int32_t kJoyConPositionalTracking = 1 << 7, kJoyConOpticalHeading = 1 << 8;
using JoyConSnapshot = std::array<int32_t, 2*kJoyConSnapshotStride>;

class XrHandMode {
public:
    bool update(std::string_view policy, bool acquired, bool touchProfile, const JoyConSnapshot &joyCons) {
        optical_ = policy == "hands" || (policy == "auto" && (acquired || (optical_ && !touchProfile)));
        const bool connected = (joyCons[0] | joyCons[kJoyConSnapshotStride]) & (1 << 6);
        return optical_ || (policy == "auto" && connected);
    }

private:
    bool optical_ = false;
};

inline bool joyConOpticalHeadingEnabled(int32_t flags) {
    constexpr auto required = (1 << 6) | kJoyConPositionalTracking | kJoyConOpticalHeading;
    return (flags & required) == required;
}

inline DroidDeckXrHand joyConControls(const int32_t values[3]) {
    DroidDeckXrHand hand{};
    if (!(values[0] & (1 << 6))) return hand;
    hand.active = DROIDDECK_XR_JOYCON_ACTIVE;
    const auto buttons = values[0];
    if (buttons & kJoyConPositionalTracking) hand.buttons |= DROIDDECK_XR_JOYCON_6DOF;
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
                          uint64_t session, uint64_t origin, const JoyConOpticalReference &optical = {}, int64_t now = 0) {
        auto hand = joyConControls(values);
        if (!hand.active) {
            aligned_ = connected_ = false;
            opticalPosition_ = opticalHeading_ = false;
            sampleNs_ = 0;
            correction_.reset();
            return hand;
        }
        opticalPosition_ = (hand.buttons & DROIDDECK_XR_JOYCON_6DOF) != 0;
        const bool opticalHeading = joyConOpticalHeadingEnabled(values[0]);
        if (opticalHeading_ != opticalHeading) correction_.reset();
        opticalHeading_ = opticalHeading;
        const bool recentered = connected_ && session_ == session && origin_ == origin && recenter_ != values[12];
        if (!connected_ || epoch_ != values[8] || session_ != session || origin_ != origin || recentered) {
            alignmentReason_ = !connected_ ? "connected" : session_ != session ? "session" :
                origin_ != origin ? "origin" : recentered ? "recenter" : "motion-reset";
            aligned_ = false;
            correction_.reset();
            epoch_ = values[8]; session_ = session; origin_ = origin;
            if (recentered && now > 0 && opticalHeading_) correction_.recenter(now);
        }
        recenter_ = values[12];
        connected_ = true;
        const auto sampleBits = uint64_t(uint32_t(values[9])) | (uint64_t(uint32_t(values[10])) << 32);
        sampleNs_ = sampleBits <= INT64_MAX ? int64_t(sampleBits) : 0;
        float angularSpeed;
        std::memcpy(&angularSpeed, values + 11, sizeof(angularSpeed));
        if (!values[7] || (now > 0 && (sampleNs_ <= 0 || now < sampleNs_ || now - sampleNs_ > 100000000LL))) {
            correction_.pause();
            return hand;
        }
        float orientation[4];
        std::memcpy(orientation, values + 3, sizeof(orientation));
        Quat imu;
        if (!decode(orientation, imu)) { correction_.reset(); return hand; }
        if (!aligned_) {
            Quat headRotation;
            double headYaw = 0, imuYaw = 0;
            if (!(head.flags & DROIDDECK_XR_ORIENTATION_VALID) || !decode(head.orientation, headRotation) ||
                !Heading(headRotation, &headYaw) || !Heading(imu, &imuYaw)) return hand;
            yaw_ = headYaw - imuYaw;
            aligned_ = true;
            ++alignments_;
        }
        if (now > 0 && opticalHeading_)
            yaw_ += correction_.update(Normalize(AboutY(yaw_) * imu), sampleNs_, angularSpeed, optical, now);
        const auto world = Normalize(AboutY(yaw_) * imu);
        hand.pose.orientation[0] = float(world.x); hand.pose.orientation[1] = float(world.y);
        hand.pose.orientation[2] = float(world.z); hand.pose.orientation[3] = float(world.w);
        hand.pose.flags = DROIDDECK_XR_ORIENTATION_VALID | DROIDDECK_XR_ORIENTATION_TRACKED;
        return hand;
    }

    void pause() { correction_.pause(); }
    const JoyConHeadingCorrection &correction() const { return correction_; }
    int32_t epoch() const { return epoch_; }
    int32_t recenterSequence() const { return recenter_; }
    int64_t sampleTimeNs() const { return sampleNs_; }
    uint64_t alignmentCount() const { return alignments_; }
    const char *alignmentReason() const { return alignmentReason_; }
    double headingOffset() const { return yaw_; }
    bool opticalPositionEnabled() const { return connected_ && opticalPosition_; }
    bool opticalHeadingEnabled() const { return connected_ && opticalHeading_; }

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
    bool opticalPosition_ = false, opticalHeading_ = false;
    int32_t epoch_ = 0, recenter_ = 0;
    int64_t sampleNs_ = 0;
    uint64_t session_ = 0, origin_ = 0, alignments_ = 0;
    const char *alignmentReason_ = "none";
    JoyConHeadingCorrection correction_;
    double yaw_ = 0;
};

}
