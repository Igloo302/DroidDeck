#include "XrJoyConHeading.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace droiddeck;
using namespace droiddeck::xr;

static constexpr double rad(double degrees) { return degrees * M_PI / 180.; }
static Quat roll(double angle) { return {std::cos(angle/2), 0, 0, std::sin(angle/2)}; }
static Quat opposite(const Quat &q) { return {-q.w, -q.x, -q.y, -q.z}; }
static void close(double actual, double expected, double tolerance = 1e-6) { assert(std::abs(actual - expected) <= tolerance); }
static DroidDeckXrPoseV2 pose(const Quat &q) {
    return {{.2f, 1.2f, -.4f}, {float(q.x), float(q.y), float(q.z), float(q.w)}, 15};
}

struct Fixture {
    JoyConHeadingCorrection correction;
    int64_t now = 1000000000;
    Quat physical = AboutY(.4) * AboutX(-.3) * roll(.2);
    Quat mount = AboutX(.7) * AboutY(-.2) * roll(-.4);
    Quat wristOffset = AboutX(.2) * AboutY(.1);
    double drift = 0, applied = 0, speed = 0;

    JoyConOpticalReference optical() const {
        const auto palm = physical * Conjugate(mount);
        return {pose(palm), pose(palm * wristOffset), now};
    }
    Quat imu() const { return AboutY(drift + applied) * physical; }
    double update() {
        const double delta = correction.update(imu(), now, speed, optical(), now);
        applied += delta;
        return delta;
    }
    double tick(int64_t elapsed = 20000000) { now += elapsed; return update(); }
    void calibrate() {
        correction.recenter(now);
        close(update(), 0);
        for (int i = 0; i < 24; ++i) { close(tick(), 0); assert(!correction.calibrated()); }
        close(tick(), 0);
        assert(correction.calibrated());
        assert(correction.status() == JoyConHeadingStatus::Ready);
    }
};

static void manualCalibration() {
    Fixture f;
    for (int i = 0; i < 200; ++i) close(f.tick(), 0);
    assert(!f.correction.calibrated());
    f.calibrate();
    f.correction.reset();
    assert(!f.correction.calibrated());
    close(f.correction.correctionRadians(), 0);
    f.correction.recenter(f.now);
    for (int i = 0; i < 151; ++i) {
        f.now += 20000000;
        auto hidden = f.optical(); hidden.palm.flags = 3;
        close(f.correction.update(f.imu(), f.now, 0, hidden, f.now), 0);
    }
    assert(f.correction.status() == JoyConHeadingStatus::CalibrationExpired);
    for (int i = 0; i < 100; ++i) close(f.tick(), 0);
    assert(!f.correction.calibrated());
    f.calibrate();
}

static void correctionRateAndDeadband() {
    Fixture f;
    f.calibrate();
    f.drift = rad(.9);
    close(f.tick(), 0);
    f.drift = rad(2);
    const auto first = f.tick();
    close(first, -rad(1) * -std::expm1(-.02 / 5.), 1e-7);
    assert(f.correction.status() == JoyConHeadingStatus::Correcting);
    f.drift = rad(20);
    const double start = f.applied;
    for (int i = 0; i < 100; ++i) {
        const double delta = f.tick();
        assert(delta < 0 && std::abs(delta) <= rad(.02) + 1e-12);
    }
    close(f.applied - start, -rad(2));
    close(f.correction.correctionRadians(), f.applied);
    for (int i = 0; i < 2000; ++i) f.tick();
    assert(std::abs(f.drift + f.applied) < rad(1.1));
    assert(std::abs(f.drift + f.applied) >= rad(.999));
}

static void arbitraryGripAndPitchRoll() {
    for (const auto &orientation : {AboutX(1.55), AboutX(-1.55), AboutY(-2.) * roll(1.2), AboutY(.8)*AboutX(.6)*roll(-.9)}) {
        Fixture f;
        f.physical = orientation;
        f.calibrate();
        f.drift = rad(-8);
        const double before = f.drift + f.applied;
        const auto previous = f.imu();
        const double delta = f.tick();
        assert(delta > 0);
        close(f.correction.errorRadians(), -before, 2e-7);
        const auto after = AboutY(delta) * previous;
        const auto beforeUp = Rotate(Conjugate(previous), {0, 1, 0});
        const auto afterUp = Rotate(Conjugate(after), {0, 1, 0});
        close(Length(afterUp - beforeUp), 0);
        for (int i = 0; i < 30; ++i) {
            f.now += 20000000;
            auto optical = f.optical();
            for (auto *p : {&optical.palm, &optical.wrist})
                for (auto &value : p->orientation) value = -value;
            const double change = f.correction.update(opposite(f.imu()), f.now, 0, optical, f.now);
            assert(change > 0 && change <= rad(.02) + 1e-12);
            f.applied += change;
        }
    }
    Fixture f;
    f.calibrate();
    f.physical = AboutY(-1.1) * AboutX(1.4) * roll(.7);
    for (int i = 0; i < 30; ++i) close(f.tick(), 0, 1e-8);
    assert(f.correction.calibrated());
    close(f.correction.errorRadians(), 0, 1e-7);
}

static void noiseAndMotionGating() {
    Fixture f;
    f.calibrate();
    for (int i = 0; i < 100; ++i) {
        f.now += 20000000;
        auto optical = f.optical();
        const auto noise = AboutY(rad(i % 2 ? .4 : -.4));
        const auto palm = noise * f.physical * Conjugate(f.mount);
        optical.palm = pose(palm); optical.wrist = pose(palm * f.wristOffset);
        close(f.correction.update(f.imu(), f.now, 0, optical, f.now), 0);
    }
    f.drift = rad(8);
    f.speed = rad(10);
    close(f.tick(), 0);
    assert(f.correction.status() == JoyConHeadingStatus::Moving);
    assert(f.correction.calibrated());
    f.speed = 0;
    for (int i = 0; i < 25; ++i) close(f.tick(), 0);
    assert(f.tick() < 0);

    f.correction.pause();
    for (int i = 0; i < 100; ++i) {
        f.physical = AboutY(rad(i * .2));
        close(f.tick(), 0);
    }
    assert(f.correction.calibrated());
    for (int i = 0; i < 30; ++i) f.tick();
    assert(f.correction.status() == JoyConHeadingStatus::Correcting);

    Fixture noisy;
    noisy.correction.recenter(noisy.now);
    for (int i = 0; i < 100; ++i) {
        noisy.physical = AboutY(rad(i % 2 ? 1.1 : -1.1));
        close(noisy.tick(), 0);
    }
    assert(!noisy.correction.calibrated());
}

static void invalidInputAndReacquisition() {
    for (int reason = 0; reason < 8; ++reason) {
        Fixture f;
        f.calibrate();
        f.drift = rad(8);
        f.now += 20000000;
        auto optical = f.optical();
        auto imu = f.imu();
        int64_t sampled = f.now;
        double speed = 0;
        switch (reason) {
            case 0: optical.palm.flags = 3; break;
            case 1: optical.wrist.flags = 7; break;
            case 2: optical.palm.position[0] = std::numeric_limits<float>::quiet_NaN(); break;
            case 3: optical.wrist.orientation[3] = 4; break;
            case 4: imu.w = std::numeric_limits<double>::infinity(); break;
            case 5: sampled -= 50000001; break;
            case 6: optical.sampledNs += 1; break;
            case 7: speed = std::numeric_limits<double>::quiet_NaN(); break;
        }
        close(f.correction.update(imu, sampled, speed, optical, f.now), 0);
        assert(f.correction.calibrated());
        for (int i = 0; i < 25; ++i) close(f.tick(), 0);
        assert(f.tick() < 0);
    }
    Fixture f;
    f.calibrate();
    f.drift = rad(8);
    f.correction.pause();
    assert(f.correction.status() == JoyConHeadingStatus::Paused);
    close(f.tick(5000000000), 0);
    for (int i = 0; i < 24; ++i) close(f.tick(), 0);
    assert(f.tick() < 0);
}

static void timestampsCannotAmplifyCorrection() {
    Fixture f;
    f.calibrate();
    f.drift = rad(8);
    assert(f.tick() < 0);
    const auto optical = f.optical();
    const double applied = f.correction.correctionRadians();
    for (int i = 0; i < 100; ++i)
        close(f.correction.update(f.imu(), f.now, 0, optical, f.now), 0);
    close(f.correction.correctionRadians(), applied);
    auto newerOptical = optical; newerOptical.sampledNs += 10000000;
    close(f.correction.update(f.imu(), f.now, 0, newerOptical, f.now + 10000000), 0);
    auto older = optical; older.sampledNs -= 1;
    close(f.correction.update(f.imu(), f.now + 10000000, 0, older, f.now + 10000000), 0);
    assert(f.correction.status() == JoyConHeadingStatus::StaleInput);
    f.now += 20000000;
    close(f.update(), 0);
    f.now += 1000000000;
    close(f.update(), 0);
    for (int i = 0; i < 24; ++i) close(f.tick(), 0);
    const double delta = f.tick();
    assert(delta < 0 && std::abs(delta) <= rad(.02) + 1e-12);
    f.now -= 10000000;
    close(f.update(), 0);
    assert(f.correction.calibrated());

    Fixture repeated;
    repeated.correction.recenter(repeated.now);
    const auto first = repeated.optical();
    for (int i = 0; i < 100; ++i) {
        repeated.now += 10000000;
        close(repeated.correction.update(repeated.imu(), repeated.now, 0, first, repeated.now), 0);
    }
    assert(!repeated.correction.calibrated());

    Fixture boundary;
    boundary.correction.recenter(boundary.now);
    for (int i = 0; i <= 25; ++i) {
        auto sample = boundary.optical(); sample.sampledNs -= 50000000;
        close(boundary.correction.update(boundary.imu(), sample.sampledNs, 0, sample, boundary.now), 0);
        boundary.now += 20000000;
    }
    assert(boundary.correction.calibrated());
}

static void disagreementRequiresManualRecovery() {
    for (int reason = 0; reason < 3; ++reason) {
        Fixture f;
        f.calibrate();
        for (int i = 0; i < 30; ++i) {
            f.now += 20000000;
            auto optical = f.optical();
            auto imu = f.imu();
            if (reason == 0) imu = AboutY(rad(31)) * imu;
            if (reason == 1) imu = AboutX(rad(11)) * imu;
            if (reason == 2) {
                const auto palm = f.physical * Conjugate(f.mount);
                optical.wrist = pose(palm * AboutY(rad(11)) * f.wristOffset);
            }
            close(f.correction.update(imu, f.now, 0, optical, f.now), 0);
        }
        assert(f.correction.status() == JoyConHeadingStatus::RecalibrationRequired);
        f.correction.pause();
        assert(f.correction.status() == JoyConHeadingStatus::RecalibrationRequired);
        for (int i = 0; i < 40; ++i) close(f.tick(), 0);
        assert(f.correction.status() == JoyConHeadingStatus::RecalibrationRequired);
        f.calibrate();
        f.drift = rad(8);
        assert(f.tick() < 0);
    }
    Fixture brief;
    brief.calibrate();
    brief.drift = rad(31);
    for (int i = 0; i < 10; ++i) close(brief.tick(), 0);
    assert(brief.correction.status() == JoyConHeadingStatus::Disagreement);
    brief.drift = 0;
    for (int i = 0; i < 30; ++i) close(brief.tick(), 0);
    assert(brief.correction.status() == JoyConHeadingStatus::Ready);
}

static void independentHands() {
    Fixture left, right;
    left.calibrate();
    right.calibrate();
    left.drift = rad(8); right.drift = rad(-8);
    assert(left.tick() < 0 && right.tick() > 0);
    left.correction.reset();
    close(left.tick(), 0);
    assert(right.tick() > 0 && right.correction.calibrated());
}

int main() {
    manualCalibration();
    correctionRateAndDeadband();
    arbitraryGripAndPitchRoll();
    noiseAndMotionGating();
    invalidInputAndReacquisition();
    timestampsCannotAmplifyCorrection();
    disagreementRequiresManualRecovery();
    independentHands();
    puts("PASS: optical Joy-Con heading calibration, bounded yaw correction, motion/tracking/timing gates and independent hands");
}
