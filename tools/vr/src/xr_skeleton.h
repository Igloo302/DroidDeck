#pragma once
#include "xr_hand_input.h"

#define DROIDDECK_XR_SKELETON_FILE "droiddeck-xr-skeleton-v1"

struct DroidDeckXrSkeleton {
    uint32_t magic = 0x31535844;
    uint16_t version = 1, header_bytes = 64;
    uint32_t byte_size = sizeof(DroidDeckXrSkeleton), sequence = 0;
    uint64_t session_generation = 0, origin_epoch = 0;
    int64_t sample_monotonic_ns = 0;
    uint32_t active[2]{};
    uint32_t reserved[4]{};
    DroidDeckXrPoseV2 joints[2][26]{};
};
static_assert(offsetof(DroidDeckXrSkeleton, joints) == 64 && sizeof(DroidDeckXrSkeleton) == 1728, "skeleton ABI");

namespace droiddeck::xr {
inline bool skeletonPacketFresh(const DroidDeckXrSkeleton &skeleton, const DroidDeckXrInputV2 &input, int hand, int64_t now) {
    if (hand < 0 || hand > 1 || skeleton.magic != 0x31535844 || skeleton.version != 1 ||
        skeleton.header_bytes != 64 || skeleton.byte_size != sizeof(skeleton) || !skeleton.active[hand] ||
        skeleton.session_generation != input.session_generation || skeleton.origin_epoch != input.origin_epoch ||
        !inputFresh(input, now) || !(input.flags & DROIDDECK_XR_INPUT_HAND_MODE) ||
        !(input.flags & DROIDDECK_XR_INPUT_FOCUSED) || skeleton.sample_monotonic_ns <= 0 ||
        now < skeleton.sample_monotonic_ns || now - skeleton.sample_monotonic_ns > 100000000LL) return false;
    return true;
}

inline bool skeletonFresh(const DroidDeckXrSkeleton &skeleton, const DroidDeckXrInputV2 &input, int hand, int64_t now) {
    if (!skeletonPacketFresh(skeleton, input, hand, now)) return false;
    for (const auto &joint : skeleton.joints[hand]) if (!poseValid(joint)) return false;
    return true;
}

inline DroidDeckXrPoseV2 joyConHandPose(const JoyConHandControls &controls, const DroidDeckXrInputV2 &input,
        const DroidDeckXrSkeleton &skeleton, int hand, int64_t now, int64_t *positionSample = nullptr) {
    if (positionSample) *positionSample = 0;
    DroidDeckXrPoseV2 pose{};
    if (!controls.active || hand < 0 || hand > 1 || !inputFresh(input, now)
            || !(input.flags & DROIDDECK_XR_INPUT_FOCUSED) || !(input.flags & DROIDDECK_XR_INPUT_HAND_MODE)
            || (controls.pose.flags & 5u) != 5u) return pose;
    float length = 0;
    for (float value : controls.pose.orientation) {
        if (!std::isfinite(value)) return pose;
        length += value * value;
    }
    if (std::abs(length - 1.f) > .01f) return pose;
    std::memcpy(pose.orientation, controls.pose.orientation, sizeof(pose.orientation));
    pose.flags = 5u;
    const auto positionValid = [](const DroidDeckXrPoseV2 &candidate) {
        if (!(candidate.flags & DROIDDECK_XR_POSITION_VALID)) return false;
        for (float value : candidate.position) if (!std::isfinite(value)) return false;
        return true;
    };
    const DroidDeckXrPoseV2 *position = nullptr;
    int64_t sampled = input.sample_monotonic_ns;
    const auto &sample = input.hands[hand];
    if (sample.source == DROIDDECK_XR_SOURCE_RUNTIME_ACTION && (sample.status_bits & DROIDDECK_XR_GRIP_ACTIVE)
            && positionValid(sample.grip)) position = &sample.grip;
    if (!position && skeletonPacketFresh(skeleton, input, hand, now)) {
        for (int joint : {0, 1}) if (positionValid(skeleton.joints[hand][joint])) {
            position = &skeleton.joints[hand][joint];
            sampled = skeleton.sample_monotonic_ns;
            break;
        }
    }
    if (!position) return pose;
    std::memcpy(pose.position, position->position, sizeof(pose.position));
    pose.flags = 5u | (position->flags & 10u);
    if (positionSample) *positionSample = sampled;
    return pose;
}

class JoyConHandPose {
public:
    void reset() { sampled_ = lastNow_ = 0; }

    DroidDeckXrPoseV2 update(const JoyConHandControls &controls, const DroidDeckXrInputV2 &input,
            const DroidDeckXrSkeleton &skeleton, int hand, int64_t now) {
        if (generation_ != input.session_generation || epoch_ != input.origin_epoch || now < lastNow_) reset();
        generation_ = input.session_generation;
        epoch_ = input.origin_epoch;
        lastNow_ = now;
        int64_t sampled = 0;
        auto pose = joyConHandPose(controls, input, skeleton, hand, now, &sampled);
        if ((pose.flags & 5u) != 5u) { reset(); return {}; }
        if (poseValid(pose) && sampled >= sampled_) {
            sampled_ = sampled;
            std::memcpy(position_, pose.position, sizeof(position_));
            return pose;
        }
        // A held optical position is usable briefly, but is no longer tracked.
        if (!sampled_ || now < sampled_ || now - sampled_ > 200000000LL) { reset(); return {}; }
        std::memcpy(pose.position, position_, sizeof(position_));
        pose.flags = DROIDDECK_XR_ORIENTATION_VALID | DROIDDECK_XR_ORIENTATION_TRACKED | DROIDDECK_XR_POSITION_VALID;
        return pose;
    }

private:
    float position_[3]{};
    int64_t sampled_ = 0, lastNow_ = 0;
    uint64_t generation_ = 0, epoch_ = 0;
};
}
