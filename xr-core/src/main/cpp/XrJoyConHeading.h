#pragma once
#include "arm_model.h"
#include "xr_input_v2.h"
#include <cstdint>
#include <deque>

namespace droiddeck::xr {

struct JoyConOpticalReference {
    DroidDeckXrPoseV2 palm{}, wrist{};
    int64_t sampledNs = 0;
};

enum class JoyConHeadingStatus {
    Uncalibrated, Calibrating, CalibrationExpired, Paused, StaleInput, InvalidInput,
    Untracked, Moving, Qualifying, WaitingForSamples, Ready, Correcting,
    Disagreement, RecalibrationRequired
};

class JoyConHeadingCorrection {
public:
    void reset() {
        calibrated_ = locked_ = false;
        deadline_ = lastNow_ = 0;
        error_ = correction_ = 0;
        clearWindow();
        status_ = JoyConHeadingStatus::Uncalibrated;
    }

    void recenter(int64_t now) {
        reset();
        if (now <= 0) return;
        deadline_ = now + kCalibrationNs;
        status_ = JoyConHeadingStatus::Calibrating;
    }

    void pause() {
        clearWindow();
        lastNow_ = 0;
        error_ = 0;
        if (!locked_) status_ = JoyConHeadingStatus::Paused;
    }

    bool calibrated() const { return calibrated_; }
    JoyConHeadingStatus status() const { return status_; }
    double errorRadians() const { return error_; }
    double correctionRadians() const { return correction_; }

    const char *statusName() const {
        switch (status_) {
            case JoyConHeadingStatus::Uncalibrated: return "uncalibrated";
            case JoyConHeadingStatus::Calibrating: return "calibrating";
            case JoyConHeadingStatus::CalibrationExpired: return "calibration-expired";
            case JoyConHeadingStatus::Paused: return "paused";
            case JoyConHeadingStatus::StaleInput: return "stale-input";
            case JoyConHeadingStatus::InvalidInput: return "invalid-input";
            case JoyConHeadingStatus::Untracked: return "untracked";
            case JoyConHeadingStatus::Moving: return "moving";
            case JoyConHeadingStatus::Qualifying: return "qualifying";
            case JoyConHeadingStatus::WaitingForSamples: return "waiting-for-samples";
            case JoyConHeadingStatus::Ready: return "ready";
            case JoyConHeadingStatus::Correcting: return "correcting";
            case JoyConHeadingStatus::Disagreement: return "disagreement";
            case JoyConHeadingStatus::RecalibrationRequired: return "recalibration-required";
        }
        return "invalid-input";
    }

    double update(const Quat &worldImu, int64_t imuSampleNs, double angularSpeed,
                  const JoyConOpticalReference &optical, int64_t now) {
        if (locked_) return 0;
        if (!calibrated_ && !deadline_) return 0;
        if (lastNow_ && now < lastNow_) {
            pause();
            if (!calibrated_) deadline_ = 0;
            return 0;
        }
        lastNow_ = now;
        if (!calibrated_ && now > deadline_) {
            deadline_ = 0;
            return reject(JoyConHeadingStatus::CalibrationExpired);
        }
        if (!fresh(imuSampleNs, now) || !fresh(optical.sampledNs, now))
            return reject(JoyConHeadingStatus::StaleInput);
        if ((optical.palm.flags & 15u) != 15u || (optical.wrist.flags & 15u) != 15u)
            return reject(JoyConHeadingStatus::Untracked);
        Quat imu, palm, wrist;
        if (!decode(worldImu, imu) || !decode(optical.palm, palm) || !decode(optical.wrist, wrist)
                || !std::isfinite(angularSpeed) || angularSpeed < 0)
            return reject(JoyConHeadingStatus::InvalidInput);
        if (angularSpeed >= radians(10)) return reject(JoyConHeadingStatus::Moving);
        if (lastImu_ && (imuSampleNs < lastImu_ || optical.sampledNs < lastOptical_))
            return reject(JoyConHeadingStatus::StaleInput);
        if (lastImu_ && (imuSampleNs == lastImu_ || optical.sampledNs == lastOptical_)) {
            status_ = JoyConHeadingStatus::WaitingForSamples;
            return 0;
        }
        double dt = 0;
        if (lastImu_) {
            const int64_t gap = std::max({imuSampleNs - lastImu_, optical.sampledNs - lastOptical_, now - lastAccepted_});
            if (gap > kFreshNs) clearWindow();
            else dt = std::min({imuSampleNs - lastImu_, optical.sampledNs - lastOptical_, now - lastAccepted_}) * 1e-9;
        }
        lastImu_ = imuSampleNs; lastOptical_ = optical.sampledNs; lastAccepted_ = now;
        const bool steady = qualify(palm, wrist, imuSampleNs, optical.sampledNs);
        if (!calibrated_) {
            if (steady) {
                mount_ = Normalize(Conjugate(palm) * imu);
                wristFromPalm_ = Normalize(Conjugate(palm) * wrist);
                calibrated_ = true;
                deadline_ = 0;
                status_ = JoyConHeadingStatus::Ready;
            } else status_ = JoyConHeadingStatus::Calibrating;
            return 0;
        }
        const Quat target = Normalize(palm * mount_);
        const Quat difference = Normalize(target * Conjugate(imu));
        const double twistLength = std::hypot(difference.w, difference.y);
        const Quat twist = twistLength > 1e-8
            ? Quat{difference.w / twistLength, 0, difference.y / twistLength, 0} : Quat{};
        error_ = std::remainder(2 * std::atan2(twist.y, twist.w), 2 * M_PI);
        const bool disagrees = twistLength <= 1e-8 || std::abs(error_) > radians(30)
            || distance(difference, twist) > radians(10)
            || distance(Conjugate(palm) * wrist, wristFromPalm_) > radians(10);
        if (disagrees) {
            if (!badImu_) { badImu_ = imuSampleNs; badOptical_ = optical.sampledNs; }
            if (steady && std::min(imuSampleNs - badImu_, optical.sampledNs - badOptical_) >= kSteadyNs) locked_ = true;
            status_ = locked_ ? JoyConHeadingStatus::RecalibrationRequired : JoyConHeadingStatus::Disagreement;
            return 0;
        }
        badImu_ = badOptical_ = 0;
        if (!steady) { status_ = JoyConHeadingStatus::Qualifying; return 0; }
        const double magnitude = std::max(0., std::abs(error_) - radians(1));
        const double correction = std::copysign(std::min(magnitude * -std::expm1(-dt / 5.), radians(1) * dt), error_);
        correction_ += correction;
        status_ = correction == 0 ? JoyConHeadingStatus::Ready : JoyConHeadingStatus::Correcting;
        return correction;
    }

private:
    static constexpr int64_t kFreshNs = 50000000, kSteadyNs = 500000000, kCalibrationNs = 3000000000;
    struct Observation { Quat palm, wrist; int64_t imu, optical; };
    std::deque<Observation> window_;
    Quat mount_, wristFromPalm_;
    bool calibrated_ = false, locked_ = false;
    int64_t deadline_ = 0, lastNow_ = 0, lastImu_ = 0, lastOptical_ = 0, lastAccepted_ = 0;
    int64_t badImu_ = 0, badOptical_ = 0;
    double error_ = 0, correction_ = 0;
    JoyConHeadingStatus status_ = JoyConHeadingStatus::Uncalibrated;

    static constexpr double radians(double degrees) { return degrees * M_PI / 180.; }
    static bool fresh(int64_t sampled, int64_t now) { return sampled > 0 && now >= sampled && now - sampled <= kFreshNs; }
    static bool decode(const Quat &q, Quat &out) {
        const double norm = q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z;
        if (!std::isfinite(norm) || std::abs(norm - 1) > .01) return false;
        out = Normalize(q);
        return true;
    }
    static bool decode(const DroidDeckXrPoseV2 &pose, Quat &out) {
        for (float value : pose.position) if (!std::isfinite(value)) return false;
        return decode(Quat{pose.orientation[3], pose.orientation[0], pose.orientation[1], pose.orientation[2]}, out);
    }
    static double distance(const Quat &a, const Quat &b) {
        return 2 * std::acos(std::clamp(std::abs(a.w*b.w + a.x*b.x + a.y*b.y + a.z*b.z), 0., 1.));
    }
    void clearWindow() {
        window_.clear();
        lastImu_ = lastOptical_ = lastAccepted_ = badImu_ = badOptical_ = 0;
    }
    double reject(JoyConHeadingStatus status) {
        clearWindow();
        error_ = 0;
        status_ = status;
        return 0;
    }
    bool qualify(const Quat &palm, const Quat &wrist, int64_t imu, int64_t optical) {
        while (window_.size() > 1 && std::min(imu - window_[1].imu, optical - window_[1].optical) >= kSteadyNs)
            window_.pop_front();
        for (const auto &sample : window_)
            if (distance(palm, sample.palm) > radians(2) || distance(wrist, sample.wrist) > radians(2)) {
                window_.clear();
                badImu_ = badOptical_ = 0;
                break;
            }
        window_.push_back({palm, wrist, imu, optical});
        return std::min(imu - window_.front().imu, optical - window_.front().optical) >= kSteadyNs;
    }
};

}
