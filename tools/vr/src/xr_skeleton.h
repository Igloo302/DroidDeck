#pragma once
#include "arm_model.h"
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
    if (!controls.opticalPosition) return pose;
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
    void reset() {
        sampled_ = lastNow_ = previousImuSample_ = 0;
        angularSpeed_ = 0;
        headingSet_ = false;
        for (auto &arm : arms_) arm.Reset();
    }

    DroidDeckXrPoseV2 update(const JoyConHandControls &controls, const DroidDeckXrInputV2 &input,
            const DroidDeckXrSkeleton &skeleton, int hand, int64_t now) {
        if (generation_ != input.session_generation || epoch_ != input.origin_epoch || now < lastNow_
                || opticalPosition_ != controls.opticalPosition || hand_ != hand
                || (!controls.opticalPosition && controls.sampledNs > 0 && controls.sampledNs < previousImuSample_)
                || (!controls.opticalPosition && lastNow_ && now - lastNow_ > 100000000LL)) reset();
        generation_ = input.session_generation;
        epoch_ = input.origin_epoch;
        opticalPosition_ = controls.opticalPosition;
        hand_ = hand;
        const auto previousNow = lastNow_;
        lastNow_ = now;
        int64_t sampled = 0;
        auto pose = joyConHandPose(controls, input, skeleton, hand, now, &sampled);
        if ((pose.flags & 5u) != 5u) { reset(); return {}; }
        const auto &head = controls.head;
        const bool haveHead = XrTrackingFile::Tracked(head);
        if (!controls.opticalPosition) {
            if (!haveHead) { reset(); return {}; }
            const Quat imu = Normalize({pose.orientation[3], pose.orientation[0], pose.orientation[1], pose.orientation[2]});
            const double elapsed = previousNow ? double(now - previousNow) * 1e-9 : 0;
            const auto sampled = controls.sampledNs > 0 ? controls.sampledNs : now;
            if (sampled != previousImuSample_) {
                const double sampleElapsed = previousImuSample_ ? double(sampled - previousImuSample_) * 1e-9 : 0;
                angularSpeed_ = sampleElapsed > 0
                    ? AngleDegrees(Conjugate(previousImu_) * imu) * M_PI / 180. / sampleElapsed : 0;
                previousImu_ = imu;
                previousImuSample_ = sampled;
            }
            const auto position = arms_[hand].Update({head.position[0], head.position[1], head.position[2]},
                {head.orientation[3], head.orientation[0], head.orientation[1], head.orientation[2]}, imu,
                angularSpeed_, std::clamp(elapsed, 0., .1));
            pose.position[0] = float(position.x); pose.position[1] = float(position.y); pose.position[2] = float(position.z);
            pose.flags |= DROIDDECK_XR_POSITION_VALID;
            return pose;
        }
        if (haveHead) {
            const Quat rotation{head.orientation[3], head.orientation[0], head.orientation[1], head.orientation[2]};
            if (headingSet_) Heading(rotation, &heading_);
            else { heading_ = HeadHeading(rotation); headingSet_ = true; }
        }
        if (poseValid(pose) && sampled >= sampled_) {
            sampled_ = sampled;
            std::memcpy(position_, pose.position, sizeof(position_));
            return pose;
        }
        const int64_t age = sampled_ ? now - sampled_ : kPositionHoldNs + kPositionBlendNs;
        if (age <= kPositionHoldNs) {
            std::memcpy(pose.position, position_, sizeof(position_));
        } else {
            if (!haveHead) { reset(); return {}; }
            const Vec3 anchor = ArmModel::RestPosition(hand == 1, {head.position[0], head.position[1], head.position[2]}, heading_);
            const double blend = std::clamp(double(age - kPositionHoldNs) / kPositionBlendNs, 0., 1.);
            const Vec3 held{position_[0], position_[1], position_[2]};
            const Vec3 position = sampled_ ? held + (anchor - held) * blend : anchor;
            pose.position[0] = float(position.x); pose.position[1] = float(position.y); pose.position[2] = float(position.z);
        }
        pose.flags = DROIDDECK_XR_ORIENTATION_VALID | DROIDDECK_XR_ORIENTATION_TRACKED | DROIDDECK_XR_POSITION_VALID;
        return pose;
    }

private:
    static constexpr int64_t kPositionHoldNs = 200000000LL, kPositionBlendNs = 200000000LL;
    float position_[3]{};
    double heading_ = 0;
    bool headingSet_ = false;
    bool opticalPosition_ = false;
    int hand_ = -1;
    ArmModel arms_[2]{ArmModel(false), ArmModel(true)};
    Quat previousImu_;
    int64_t previousImuSample_ = 0;
    double angularSpeed_ = 0;
    int64_t sampled_ = 0, lastNow_ = 0;
    uint64_t generation_ = 0, epoch_ = 0;
};
}
