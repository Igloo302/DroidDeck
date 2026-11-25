#include "../src/driver_droiddeck.cpp"
#include "../../../xr-core/src/main/cpp/XrJoyConInput.h"
#include <cassert>
#include <map>
#include <sys/syscall.h>

static int64_t parityClockNs = 0;
extern "C" int clock_gettime(clockid_t clock, timespec *time) noexcept {
    if (clock == CLOCK_MONOTONIC && parityClockNs) {
        time->tv_sec = parityClockNs / 1000000000;
        time->tv_nsec = parityClockNs % 1000000000;
        return 0;
    }
    return int(syscall(SYS_clock_gettime, clock, time));
}

void droiddeck::Presenter::AddTextures(const uint64_t[3], const int[3], uint32_t, uint32_t, uint32_t, uint32_t) {}
void droiddeck::Presenter::RemoveTexture(uint64_t) {}
void droiddeck::Presenter::Submit(const EyeFrame &) {}
void droiddeck::Presenter::Shutdown() {}

struct Host : IVRServerDriverHost {
    std::vector<DriverPose_t> poses;
    const std::map<VRInputComponentHandle_t, float> *inputs = nullptr;
    std::vector<std::map<VRInputComponentHandle_t, float>> inputsAtPose;
    bool TrackedDeviceAdded(const char *, ETrackedDeviceClass, ITrackedDeviceServerDriver *) override { return true; }
    void TrackedDevicePoseUpdated(uint32_t, const DriverPose_t &p, uint32_t) override {
        poses.push_back(p); inputsAtPose.push_back(*inputs);
    }
    void VsyncEvent(double) override {}
    void VendorSpecificEvent(uint32_t, EVREventType, const VREvent_Data_t &, double) override {}
    bool IsExiting() override { return false; }
    bool PollNextEvent(VREvent_t *, uint32_t) override { return false; }
    void GetRawTrackedDevicePoses(float, TrackedDevicePose_t *, uint32_t) override {}
    void RequestRestart(const char *, const char *, const char *, const char *) override {}
    uint32_t GetFrameTimings(Compositor_FrameTiming *, uint32_t) override { return 0; }
    void SetDisplayEyeToHead(uint32_t, const HmdMatrix34_t &, const HmdMatrix34_t &) override {}
    void SetDisplayProjectionRaw(uint32_t, const HmdRect2_t &, const HmdRect2_t &) override {}
    void SetRecommendedRenderTargetSize(uint32_t, uint32_t, uint32_t) override {}
};
struct Properties : IVRProperties {
    ETrackedPropertyError ReadPropertyBatch(PropertyContainerHandle_t, PropertyRead_t *, uint32_t) override { return TrackedProp_Success; }
    std::map<std::pair<PropertyContainerHandle_t, ETrackedDeviceProperty>, std::string> strings;
    ETrackedPropertyError WritePropertyBatch(PropertyContainerHandle_t container, PropertyWrite_t *writes, uint32_t count) override {
        for (uint32_t i = 0; i < count; ++i) {
            if (writes[i].unTag == k_unStringPropertyTag && writes[i].pvBuffer)
                strings[{container, writes[i].prop}] = static_cast<const char *>(writes[i].pvBuffer);
            writes[i].eError = TrackedProp_Success;
        }
        return TrackedProp_Success;
    }
    const char *GetPropErrorNameFromEnum(ETrackedPropertyError) override { return "success"; }
    PropertyContainerHandle_t TrackedDeviceToPropertyContainer(TrackedDeviceIndex_t id) override { return id+1; }
};
struct Input : IVRDriverInput {
    std::map<VRInputComponentHandle_t, float> values;
    std::map<std::string, VRInputComponentHandle_t> handles;
    std::map<VRInputComponentHandle_t, EVRInputError> failures;
    unsigned skeletonUpdates = 0;
    std::map<VRInputComponentHandle_t, HmdMatrix34_t> poses;
    EVRInputError CreateBooleanComponent(PropertyContainerHandle_t, const char *name, VRInputComponentHandle_t *h) override {
        *h = values.size()+1; values[*h]=0; handles[name]=*h; return VRInputError_None;
    }
    EVRInputError UpdateBooleanComponent(VRInputComponentHandle_t h, bool v, double) override {
        if (failures[h] != VRInputError_None) return failures[h];
        values[h]=v; return VRInputError_None;
    }
    EVRInputError CreateScalarComponent(PropertyContainerHandle_t c, const char *n, VRInputComponentHandle_t *h, EVRScalarType, EVRScalarUnits) override { return CreateBooleanComponent(c,n,h); }
    EVRInputError UpdateScalarComponent(VRInputComponentHandle_t h, float v, double) override {
        if (failures[h] != VRInputError_None) return failures[h];
        values[h]=v; return VRInputError_None;
    }
    EVRInputError CreateHapticComponent(PropertyContainerHandle_t c, const char *n, VRInputComponentHandle_t *h) override { return CreateBooleanComponent(c,n,h); }
    EVRInputError CreateSkeletonComponent(PropertyContainerHandle_t c, const char *name, const char *, const char *, EVRSkeletalTrackingLevel, const VRBoneTransform_t *, uint32_t, VRInputComponentHandle_t *h) override {
        return CreateBooleanComponent(c, name, h);
    }
    EVRInputError UpdateSkeletonComponent(VRInputComponentHandle_t h, EVRSkeletalMotionRange, const VRBoneTransform_t *bones, uint32_t count) override {
        assert(h && count == 31);
        for (uint32_t i = 0; i < count; ++i) {
            for (float p : bones[i].position.v) assert(std::isfinite(p));
            assert(std::isfinite(bones[i].orientation.w));
        }
        ++skeletonUpdates;
        return VRInputError_None;
    }
    EVRInputError CreatePoseComponent(PropertyContainerHandle_t c, const char *name, VRInputComponentHandle_t *h) override { return CreateBooleanComponent(c, name, h); }
    EVRInputError UpdatePoseComponent(VRInputComponentHandle_t h, const HmdMatrix34_t *pose, double) override { poses[h] = *pose; return VRInputError_None; }
    EVRInputError CreateEyeTrackingComponent(PropertyContainerHandle_t, const char *, VRInputComponentHandle_t *) override { return VRInputError_None; }
    EVRInputError UpdateEyeTrackingComponent(VRInputComponentHandle_t, const VREyeTrackingData_t *, double) override { return VRInputError_None; }
};
struct DriverLog : IVRDriverLog {
    std::vector<std::string> lines;
    void Log(const char *message) override { lines.emplace_back(message); }
};
struct Context : IVRDriverContext {
    Host host;
    Input input;
    Properties properties;
    DriverLog log;
    Context() { host.inputs = &input.values; }
    void *GetGenericInterface(const char *name, EVRInitError *) override {
        if (!strcmp(name, IVRServerDriverHost_Version)) return &host;
        if (!strcmp(name, IVRDriverInput_Version)) return &input;
        if (!strcmp(name, IVRProperties_Version)) return &properties;
        if (!strcmp(name, IVRDriverLog_Version)) return &log;
        return nullptr;
    }
    DriverHandle_t GetDriverHandle() override { return 1; }
};

static void testJoyConInputParity() {
    using namespace droiddeck::xr;
    const char *paths[]{
        "/input/system/click", "/input/system/touch", "/input/a/click", "/input/a/touch",
        "/input/b/click", "/input/b/touch", "/input/trigger/click", "/input/trigger/touch", "/input/trigger/value",
        "/input/grip/touch", "/input/grip/value", "/input/grip/force", "/input/thumbstick/x", "/input/thumbstick/y",
        "/input/thumbstick/click", "/input/thumbstick/touch", "/input/trackpad/x", "/input/trackpad/y",
        "/input/trackpad/force", "/input/trackpad/touch", "/input/finger/index", "/input/finger/middle",
        "/input/finger/ring", "/input/finger/pinky"};
    for (int side = 0; side < 2; ++side) {
        parityClockNs = 1000000000;
        Context legacy, hybrid;
        VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &legacy;
        HandController oldController(side == 1);
        assert(oldController.Activate(5) == VRInitError_None);
        VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &hybrid;
        CardboardController newController(side == 1);
        assert(newController.ConfigureAndroidXr());
        assert(newController.Activate(5) == VRInitError_None);
        auto packet = droiddeck_xr_input_v2_neutral(90, 1);
        packet.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
        DroidDeckXrTracking tracking{};
        tracking.active = DROIDDECK_ANDROID_XR_ACTIVE;
        tracking.period_ns = 13888889;
        tracking.eyes[0].width = tracking.eyes[0].height = 1024;
        DroidDeckXrSkeleton skeleton{};
        const auto update = [&](int mask, float x, float y) {
            droiddeck_joycon_hand hand{};
            hand.buttons = uint32_t(mask); hand.rotation[0] = 1;
            hand.stick[0] = x; hand.stick[1] = y; hand.time_ns = parityClockNs;
            VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &legacy;
            assert(oldController.Update(&hand, false, {1, 0, 0, 0}, parityClockNs));
            int32_t snapshot[]{64 | mask, int32_t(std::lround(x * 32767)), int32_t(std::lround(y * 32767))};
            tracking.hands[side] = joyConControls(snapshot);
            tracking.sampled_ns = packet.sample_monotonic_ns = parityClockNs;
            const auto controls = joyConHandControls(&tracking, packet, side, parityClockNs);
            VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &hybrid;
            newController.UpdateXrHand(packet, skeleton, parityClockNs, false, false, controls);
            for (const auto path : paths) {
                const float oldValue = legacy.input.values.at(legacy.input.handles.at(path));
                const float newValue = hybrid.input.values.at(hybrid.input.handles.at(path));
                assert(std::abs(oldValue - newValue) <= 1.f / 32767);
            }
        };
        update(0, 0, 0);
        const float sticks[][2]{{0, 0}, {.05f, 0}, {.2f, -.4f}, {1, -1}};
        for (const auto &stick : sticks) for (int mask = 0; mask < 64; ++mask) {
            parityClockNs += 50000000;
            update(mask, stick[0], stick[1]);
        }
        newController.Deactivate();
        for (const auto path : paths) assert(hybrid.input.values.at(hybrid.input.handles.at(path)) == 0);
        VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &legacy;
        oldController.Deactivate();
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }
    parityClockNs = 0;
    puts("PASS: all 64 Joy-Con button masks, four stick positions and smoothed Index virtual inputs match both legacy hands");
}

static void testXrJoyConHybrid() {
    using namespace droiddeck::xr;
    for (int side = 0; side < 2; ++side) {
        Context context;
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
        VRDriverContext() = &context;
        CardboardController controller(side == 1);
        assert(controller.ConfigureAndroidXr());
        assert(controller.Activate(5) == VRInitError_None);
        const auto value = [&](const char *name) { return context.input.values.at(context.input.handles.at(name)); };
        const auto model = [&] { return context.properties.strings.at({6, Prop_RenderModelName_String}); };
        const std::string controllerModel = side ? "{indexcontroller}valve_controller_knu_1_0_right" : "{indexcontroller}valve_controller_knu_1_0_left";
        const std::string handModel = side ? "{droiddeck}droiddeck_hand_right" : "{droiddeck}droiddeck_hand_left";
        const std::string localHandModel = handModel + "_xr";
        const int64_t now = 1000000000;
        auto packet = droiddeck_xr_input_v2_neutral(90, 1);
        packet.sample_monotonic_ns = now;
        packet.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
        auto &hand = packet.hands[side];
        hand.source = DROIDDECK_XR_SOURCE_RUNTIME_ACTION;
        hand.grip.flags = 10;
        hand.grip.position[0] = 1; hand.grip.position[1] = 2; hand.grip.position[2] = -3;
        hand.grip.orientation[0] = NAN;
        hand.aim.flags = 0;
        hand.status_bits = 31;
        hand.ready_bits = hand.capability_bits = DROIDDECK_XR_PINCH | DROIDDECK_XR_GRASP;
        hand.pinch = hand.grasp = 1;
        DroidDeckXrTracking tracking{};
        tracking.active = DROIDDECK_ANDROID_XR_ACTIVE;
        tracking.sampled_ns = now;
        tracking.period_ns = 13888889;
        tracking.eyes[0].width = tracking.eyes[0].height = 1024;
        auto &joyCon = tracking.hands[side];
        joyCon.active = DROIDDECK_XR_JOYCON_ACTIVE;
        joyCon.pose.flags = 5;
        joyCon.pose.position[0] = 999;
        const auto rotation = AboutY(.6) * AboutX(-.3);
        joyCon.pose.orientation[0] = float(rotation.x); joyCon.pose.orientation[1] = float(rotation.y);
        joyCon.pose.orientation[2] = float(rotation.z); joyCon.pose.orientation[3] = float(rotation.w);
        joyCon.buttons = DROIDDECK_XR_MENU_CLICK | DROIDDECK_XR_PRIMARY_CLICK | DROIDDECK_XR_STICK_CLICK;
        joyCon.stick[0] = .5f; joyCon.stick[1] = -.5f;
        DroidDeckXrSkeleton skeleton{};
        const auto update = [&] {
            const auto controls = joyConHandControls(&tracking, packet, side, now);
            controller.UpdateXrHand(packet, skeleton, now, false, false, controls);
        };
        const auto assertHybridPose = [&](const float *position) {
            const auto driverPose = controller.GetPose();
            assert(driverPose.deviceIsConnected && driverPose.poseIsValid);
            const DroidDeckXrPoseV2 raw{{float(driverPose.vecPosition[0]), float(driverPose.vecPosition[1]), float(driverPose.vecPosition[2])},
                {float(driverPose.qRotation.x), float(driverPose.qRotation.y), float(driverPose.qRotation.z), float(driverPose.qRotation.w)}, 15};
            DroidDeckXrPoseV2 wanted{};
            std::memcpy(wanted.position, position, sizeof(wanted.position));
            std::memcpy(wanted.orientation, joyCon.pose.orientation, sizeof(wanted.orientation));
            const auto expected = handTransform(wanted);
            HandTransform tip{}, grip{};
            const auto &tipOffset = context.input.poses.at(context.input.handles.at("/pose/tip"));
            const auto &gripOffset = context.input.poses.at(context.input.handles.at("/pose/grip"));
            std::memcpy(tip.m, tipOffset.m, sizeof(tip.m));
            std::memcpy(grip.m, gripOffset.m, sizeof(grip.m));
            const auto worldTip = handCompose(handTransform(raw), tip);
            const auto worldGrip = handCompose(handTransform(raw), grip);
            for (int row = 0; row < 3; ++row) {
                assert(std::abs(worldGrip.m[row][3] - position[row]) < .00001f);
                for (int col = 0; col < 4; ++col) assert(std::abs(worldTip.m[row][col] - expected.m[row][col]) < .00001f);
            }
        };
        update(); update();
        assertHybridPose(hand.grip.position);
        assert(model() == controllerModel);
        assert(value("/input/trigger/value") == 0 && value("/input/grip/value") == 0);
        assert(value("/input/a/click") == 1 && value("/input/system/click") == 1);
        assert(value("/input/thumbstick/x") == .5f && value("/input/thumbstick/y") == -.5f);
        assert(value("/input/thumbstick/click") == 1);
        hand.grip.flags = 2;
        update();
        assertHybridPose(hand.grip.position);
        assert(controller.GetPose().result == TrackingResult_Running_OutOfRange);
        skeleton.session_generation = packet.session_generation; skeleton.origin_epoch = packet.origin_epoch;
        skeleton.sample_monotonic_ns = now; skeleton.active[side] = 1;
        auto &palm = skeleton.joints[side][0];
        palm.flags = 10; palm.position[0] = 4; palm.orientation[0] = NAN;
        auto &wrist = skeleton.joints[side][1];
        wrist.flags = 10; wrist.position[0] = 5; wrist.orientation[0] = NAN;
        const auto beforeSkeleton = context.input.skeletonUpdates;
        hand.grip.flags = 0;
        update();
        assertHybridPose(palm.position);
        palm.flags = 0;
        update();
        assertHybridPose(wrist.position);
        palm.flags = 10; palm.position[0] = NAN;
        update();
        assertHybridPose(wrist.position);
        assert(context.input.skeletonUpdates == beforeSkeleton);
        joyCon.trigger = joyCon.squeeze = 1;
        skeleton.sample_monotonic_ns = now - 100000001;
        update();
        assertHybridPose(wrist.position);
        assert(controller.GetPose().result == TrackingResult_Running_OutOfRange && model() == controllerModel);
        assert(value("/input/trigger/value") == 1 && value("/input/grip/value") == 1 && value("/input/grip/force") == 1);
        assert(value("/input/system/click") == 1 && value("/input/thumbstick/x") == .5f);
        skeleton.sample_monotonic_ns = now;
        ++skeleton.origin_epoch;
        update();
        assertHybridPose(wrist.position);
        assert(controller.GetPose().result == TrackingResult_Running_OutOfRange);
        --skeleton.origin_epoch;
        joyCon.pose.orientation[0] = NAN;
        update();
        assert(!controller.GetPose().poseIsValid && value("/input/trigger/value") == 1);
        joyCon.pose.orientation[0] = float(rotation.x);
        update();
        assertHybridPose(wrist.position);
        tracking.sampled_ns = now - 100000001;
        update();
        assert(!controller.GetPose().poseIsValid && value("/input/trigger/value") == 0);
        tracking.sampled_ns = now - 100000000;
        update();
        assertHybridPose(wrist.position);
        tracking.sampled_ns = now + 1;
        update();
        assert(!controller.GetPose().poseIsValid && value("/input/trigger/value") == 0);
        tracking.sampled_ns = now;
        packet.flags &= ~DROIDDECK_XR_INPUT_FOCUSED;
        update();
        assert(!controller.GetPose().deviceIsConnected && !controller.GetPose().poseIsValid);
        assert(value("/input/trigger/value") == 0 && value("/input/grip/value") == 0 && value("/input/grip/force") == 0);
        assert(value("/input/system/click") == 0 && value("/input/thumbstick/x") == 0);
        packet.flags |= DROIDDECK_XR_INPUT_FOCUSED;
        hand.grip.flags = hand.aim.flags = 15;
        hand.grip.orientation[0] = 0;
        hand.aim.position[0] = 2;
        skeleton.active[side] = 0;
        update();
        assertHybridPose(hand.grip.position);
        assert(model() == controllerModel && value("/input/trigger/value") == 1);
        joyCon.active = 0;
        update();
        assert(model() == handModel);
        assert(controller.GetPose().poseIsValid && value("/input/trigger/value") == 0);
        assert(value("/input/system/click") == 0 && value("/input/a/click") == 0 && value("/input/thumbstick/x") == 0);
        hand.pinch = hand.grasp = 0;
        update();
        hand.pinch = hand.grasp = 1;
        update();
        assert(value("/input/trigger/value") == 1 && value("/input/grip/value") == 1);
        packet.flags |= DROIDDECK_XR_INPUT_LOCAL_HAND_MESH;
        joyCon.active = DROIDDECK_XR_JOYCON_ACTIVE;
        update();
        assert(model() == controllerModel);
        joyCon.active = 0;
        skeleton.active[side] = 1;
        for (auto &joint : skeleton.joints[side]) joint = hand.grip;
        update();
        assert(model() == localHandModel && context.input.skeletonUpdates == beforeSkeleton + 2);
        controller.Deactivate();
        assert(value("/input/trigger/value") == 0 && value("/input/grip/value") == 0 && value("/input/grip/force") == 0);
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }
    puts("PASS: Joy-Con orientation and optical position drive controller models with safe pose loss and hand restoration");
}

static void testXrJoyConPositionContinuity() {
    using namespace droiddeck::xr;
    int64_t now = 1000000000;
    auto packet = droiddeck_xr_input_v2_neutral(93, 1);
    packet.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
    JoyConHandControls controls[2];
    JoyConHandPose poses[2];
    DroidDeckXrSkeleton skeleton{};
    for (int side = 0; side < 2; ++side) {
        controls[side].active = true;
        controls[side].pose.flags = 5;
        controls[side].pose.orientation[3] = 1;
        auto &hand = packet.hands[side];
        hand.source = DROIDDECK_XR_SOURCE_RUNTIME_ACTION;
        hand.status_bits = DROIDDECK_XR_GRIP_ACTIVE;
        hand.grip.flags = 10;
        hand.grip.position[0] = side ? 1.f : -1.f;
    }
    const auto update = [&](int side) {
        packet.sample_monotonic_ns = now;
        return poses[side].update(controls[side], packet, skeleton, side, now);
    };
    for (int side = 0; side < 2; ++side) assert(update(side).flags == 15);
    now += 14000000;
    packet.hands[1].grip.flags = 0;
    const auto turned = AboutY(.6);
    controls[1].pose.orientation[1] = float(turned.y);
    controls[1].pose.orientation[3] = float(turned.w);
    auto held = update(1);
    assert(held.flags == 7 && held.position[0] == 1);
    assert(held.orientation[1] == controls[1].pose.orientation[1]);
    assert(held.orientation[3] == controls[1].pose.orientation[3]);
    assert(update(0).flags == 15 && update(0).position[0] == -1);
    now += 14000000;
    packet.hands[1].grip.flags = 10;
    packet.hands[1].grip.position[0] = 2;
    assert(update(1).flags == 15 && update(1).position[0] == 2);

    packet.hands[1].grip.flags = 0;
    skeleton.session_generation = packet.session_generation;
    skeleton.origin_epoch = packet.origin_epoch;
    skeleton.active[1] = 1;
    skeleton.sample_monotonic_ns = now;
    skeleton.joints[1][0].flags = 10;
    skeleton.joints[1][0].position[0] = 3;
    assert(update(1).flags == 15 && update(1).position[0] == 3);
    const int64_t observed = now;
    now = observed + 90000000;
    assert(update(1).flags == 15);
    now = observed + 150000000;
    assert(update(1).flags == 7);
    now = observed + 200000000;
    assert(update(1).flags == 7 && update(1).position[0] == 3);
    ++now;
    assert(!poseValid(update(1)));
    assert(!poseValid(update(1)));

    skeleton = {};
    for (int reason = 0; reason < 8; ++reason) {
        now += 1000000000;
        packet.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
        packet.hands[1].grip.flags = 10;
        controls[1].active = true;
        controls[1].pose.flags = 5;
        controls[1].pose.orientation[0] = 0;
        assert(poseValid(update(1)));
        packet.hands[1].grip.flags = 0;
        now += 1000000;
        switch (reason) {
            case 0: packet.flags &= ~DROIDDECK_XR_INPUT_FOCUSED; break;
            case 1: packet.flags &= ~DROIDDECK_XR_INPUT_HAND_MODE; break;
            case 2: controls[1].active = false; break;
            case 3: controls[1].pose.flags = 0; break;
            case 4: controls[1].pose.orientation[0] = NAN; break;
            case 5: ++packet.origin_epoch; break;
            case 6: ++packet.session_generation; break;
            case 7: now -= 2000000; break;
        }
        assert(!poseValid(update(1)));
        packet.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
        controls[1].active = true;
        controls[1].pose.flags = 5;
        controls[1].pose.orientation[0] = 0;
        now += 2000000;
        assert(!poseValid(update(1)));
    }
    packet.hands[1].grip.flags = 10;
    assert(poseValid(update(1)));
    packet.hands[1].grip.flags = 0;
    packet.sample_monotonic_ns = now - 100000001;
    assert(!poseValid(poses[1].update(controls[1], packet, skeleton, 1, now)));
    assert(!poseValid(update(1)));
    packet.hands[1].grip.flags = 10;
    assert(poseValid(update(1)));
    packet.hands[1].grip.flags = 0;
    poses[1].reset();
    assert(!poseValid(update(1)));
    puts("PASS: hybrid optical position holds at most 200ms with live IMU, independent sides and epoch/focus resets");
}

static void testXrJoyConAimParity() {
    using namespace droiddeck::xr;
    const auto roll = [](double angle) { return Quat{std::cos(angle/2), 0, 0, std::sin(angle/2)}; };
    const auto rotation = [](float *out, const Quat &q) {
        out[0] = float(q.x); out[1] = float(q.y); out[2] = float(q.z); out[3] = float(q.w);
    };
    const auto transform = [&](const DriverPose_t &pose) {
        DroidDeckXrPoseV2 raw{};
        rotation(raw.orientation, {pose.qRotation.w, pose.qRotation.x, pose.qRotation.y, pose.qRotation.z});
        for (int axis = 0; axis < 3; ++axis) raw.position[axis] = float(pose.vecPosition[axis]);
        return handTransform(raw);
    };
    for (int side = 0; side < 2; ++side) {
        parityClockNs = 1000000000;
        Context legacy, hybrid;
        VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &legacy;
        HandController oldController(side == 1);
        assert(oldController.Activate(5) == VRInitError_None);
        VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &hybrid;
        CardboardController newController(side == 1);
        assert(newController.ConfigureAndroidXr());
        assert(newController.Activate(5) == VRInitError_None);
        auto packet = droiddeck_xr_input_v2_neutral(96, 1);
        packet.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
        DroidDeckXrTracking tracking{};
        tracking.active = DROIDDECK_ANDROID_XR_ACTIVE;
        tracking.period_ns = 13888889;
        tracking.eyes[0].width = tracking.eyes[0].height = 1024;
        DroidDeckXrSkeleton skeleton{};
        skeleton.session_generation = packet.session_generation; skeleton.origin_epoch = packet.origin_epoch;
        droiddeck_spatialpose spatial{};
        spatial.flags = DD_SPATIAL_ACTIVE | DD_SPATIAL_JOYCONS;
        spatial.connection = spatial.origin = 1;
        JoyConXrOrientation nativeOrientation;
        const auto update = [&](const Quat &imu, const Quat &head, const Quat &optical, const Vec3 &position, int source) {
            parityClockNs += 20000000;
            spatial.head = {parityClockNs, {0, 1.6f, 0}, {}, DD_SPATIAL_VALID};
            rotation(spatial.head.rotation, head);
            auto &observation = spatial.hands[side].pose;
            observation = {parityClockNs, {float(position.x), float(position.y), float(position.z)}, {}, DD_SPATIAL_VALID};
            rotation(observation.rotation, optical);
            droiddeck_joycon_hand joy{};
            joy.time_ns = parityClockNs;
            joy.rotation[0] = float(imu.w); joy.rotation[1] = float(imu.x);
            joy.rotation[2] = float(imu.y); joy.rotation[3] = float(imu.z);
            VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &legacy;
            assert(oldController.Update(&joy, false, {head.w, head.x, head.y, head.z}, parityClockNs, &spatial));
            const auto oldPose = oldController.GetPose();
            assert(oldPose.poseIsValid);
            tracking.sampled_ns = packet.sample_monotonic_ns = skeleton.sample_monotonic_ns = parityClockNs;
            rotation(tracking.head.orientation, head); tracking.head.flags = 15;
            int32_t snapshot[kJoyConSnapshotStride]{64};
            float imuXyzw[4]; rotation(imuXyzw, imu);
            std::memcpy(snapshot + 3, imuXyzw, sizeof(imuXyzw)); snapshot[7] = 1;
            tracking.hands[side] = nativeOrientation.update(snapshot, tracking.head, 96, 1);
            auto &hand = packet.hands[side];
            hand.source = DROIDDECK_XR_SOURCE_RUNTIME_ACTION;
            hand.status_bits = DROIDDECK_XR_GRIP_ACTIVE | DROIDDECK_XR_AIM_ACTIVE;
            hand.grip = {{float(position.x), float(position.y), float(position.z)}, {}, 15};
            rotation(hand.grip.orientation, optical);
            hand.aim = hand.grip; hand.aim.position[0] += .5f;
            rotation(hand.aim.orientation, optical * AboutY(.9));
            skeleton.active[side] = source == 3 ? 0 : 1;
            for (auto &joint : skeleton.joints[side]) {
                joint = hand.grip;
                rotation(joint.orientation, optical * AboutX(-.8));
            }
            if (source != 0) hand.grip.flags = 0;
            if (source == 2) skeleton.joints[side][0].flags = 0;
            const auto effective = xrHandInputForTracking(&tracking, packet, parityClockNs);
            const auto controls = joyConHandControls(&tracking, effective, side, parityClockNs);
            VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &hybrid;
            newController.UpdateXrHand(effective, skeleton, parityClockNs, false, false, controls);
            const auto newPose = newController.GetPose();
            assert(newPose.poseIsValid);
            const auto oldRaw = transform(oldPose), newRaw = transform(newPose);
            HandTransform tip{}, grip{};
            std::memcpy(tip.m, hybrid.input.poses.at(hybrid.input.handles.at("/pose/tip")).m, sizeof(tip.m));
            std::memcpy(grip.m, hybrid.input.poses.at(hybrid.input.handles.at("/pose/grip")).m, sizeof(grip.m));
            const auto worldTip = handCompose(newRaw, tip), worldGrip = handCompose(newRaw, grip);
            const Quat oldRawRotation{oldPose.qRotation.w, oldPose.qRotation.x, oldPose.qRotation.y, oldPose.qRotation.z};
            const auto oldTipRotation = oldRawRotation * IndexTipRotation(side == 1);
            for (int axis = 0; axis < 3; ++axis) {
                Vec3 basis{};
                if (axis == 0) basis.x = 1; else if (axis == 1) basis.y = 1; else basis.z = 1;
                const auto expected = Rotate(imu, basis), oldTip = Rotate(oldTipRotation, basis);
                const double wanted[]{expected.x, expected.y, expected.z}, legacyAim[]{oldTip.x, oldTip.y, oldTip.z};
                for (int row = 0; row < 3; ++row) {
                    assert(std::abs(newRaw.m[row][axis] - oldRaw.m[row][axis]) < .00001f);
                    assert(std::abs(worldTip.m[row][axis] - wanted[row]) < .00001);
                    assert(std::abs(worldTip.m[row][axis] - legacyAim[row]) < .00001);
                    assert(std::abs(worldGrip.m[row][axis] - oldRaw.m[row][axis]) < .00001f);
                }
            }
            const double expectedPosition[]{position.x, position.y, position.z};
            for (int row = 0; row < 3; ++row) {
                assert(std::abs(worldGrip.m[row][3] - expectedPosition[row]) < .00001);
                assert(std::abs(worldTip.m[row][3] - expectedPosition[row]) < .00001);
            }
        };
        const Vec3 position{side ? .3 : -.3, 1.3, -.5};
        update({}, {}, AboutY(1.2), position, 0);
        const Quat opticalRotations[]{AboutY(-1.1) * AboutX(.7), roll(1.2), AboutX(-.8) * AboutY(.4)};
        for (int source = 0; source < 3; ++source)
            for (const auto &imu : {AboutX(-.6), AboutX(.6), AboutY(-.8), AboutY(.8), roll(-.7), roll(.7)})
                update(imu, {}, opticalRotations[source], position, source);
        const auto fixedImu = AboutY(.5) * AboutX(-.3) * roll(.4);
        for (const auto &optical : opticalRotations) update(fixedImu, {}, optical, position, 0);
        for (const auto &head : {AboutY(1.2), AboutX(-.7), roll(.8), AboutY(-.9) * AboutX(.4)})
            update(fixedImu, head, opticalRotations[0], position, 0);
        for (int source = 0; source < 3; ++source)
            update(fixedImu, AboutY(-.9), opticalRotations[source], position + Vec3{.1, -.2, .15}, source);
        for (const auto &imu : {AboutY(-.7), AboutX(.6), roll(-.8)})
            update(imu, AboutY(.8), opticalRotations[1], position + Vec3{.1, -.2, .15}, 3);
        newController.Deactivate();
        VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &legacy;
        oldController.Deactivate();
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }
    parityClockNs = 0;
    puts("PASS: both hybrid aim poses match legacy Joy-Con yaw/pitch/roll and ignore optical/head rotation");
}

static void testXrJoyConUnknownTracking() {
    using namespace droiddeck::xr;
    for (int side = 0; side < 2; ++side) {
        Context context;
        VR_CLEANUP_SERVER_DRIVER_CONTEXT(); VRDriverContext() = &context;
        CardboardController controller(side == 1);
        assert(controller.ConfigureAndroidXr() && controller.Activate(5) == VRInitError_None);
        auto packet = droiddeck_xr_input_v2_neutral(98, 1);
        packet.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
        packet.sample_monotonic_ns = 1000000000;
        for (auto &hand : packet.hands) {
            hand.source = DROIDDECK_XR_SOURCE_RUNTIME_ACTION;
            hand.status_bits = DROIDDECK_XR_GRIP_ACTIVE | DROIDDECK_XR_AIM_ACTIVE | DROIDDECK_XR_PINCH_ACTIVE;
            hand.capability_bits = hand.ready_bits = DROIDDECK_XR_PINCH;
            const auto optical = AboutY(1.4) * AboutX(-.5);
            hand.grip = hand.aim = {{.3f, 1.3f, -.5f},
                {float(optical.x), float(optical.y), float(optical.z), float(optical.w)}, 15};
        }
        DroidDeckXrTracking tracking{};
        tracking.active = DROIDDECK_ANDROID_XR_ACTIVE;
        tracking.period_ns = 13888889;
        tracking.eyes[0].width = tracking.eyes[0].height = 1024;
        tracking.sampled_ns = packet.sample_monotonic_ns;
        for (auto &hand : tracking.hands) {
            hand.active = DROIDDECK_XR_JOYCON_ACTIVE;
            hand.pose.flags = 5; hand.pose.orientation[3] = 1;
            hand.trigger = 1;
        }
        DroidDeckXrSkeleton skeleton{};
        HandDashboardChord dashboard;
        const auto model = [&] { return context.properties.strings.at({6, Prop_RenderModelName_String}); };
        const auto value = [&](const char *path) { return context.input.values.at(context.input.handles.at(path)); };
        const auto update = [&](const DroidDeckXrTracking *source) {
            const auto effective = xrHandInputForTracking(source, packet, packet.sample_monotonic_ns);
            const auto left = joyConHandControls(source, effective, 0, packet.sample_monotonic_ns);
            const auto right = joyConHandControls(source, effective, 1, packet.sample_monotonic_ns);
            assert(!dashboard.update(effective, packet.sample_monotonic_ns, uint32_t(left.active) | (uint32_t(right.active) << 1)));
            controller.UpdateXrHand(effective, skeleton, packet.sample_monotonic_ns, false, false, side ? right : left);
        };
        update(&tracking); update(&tracking);
        const auto controllerModel = model();
        assert(controller.GetPose().poseIsValid && value("/input/trigger/value") == 1);
        for (auto &hand : packet.hands) hand.pinch = 1;
        for (int reason = 0; reason < 4; ++reason) {
            packet.sample_monotonic_ns += 1100000000;
            tracking.sampled_ns = packet.sample_monotonic_ns;
            if (reason == 1) tracking.sampled_ns -= 100000001;
            if (reason == 2) ++tracking.sampled_ns;
            if (reason == 3) tracking.active = 0;
            update(reason == 0 ? nullptr : &tracking);
            assert(!controller.GetPose().poseIsValid && model() == controllerModel);
            assert(value("/input/trigger/value") == 0 && value("/input/grip/value") == 0);
            tracking.active = DROIDDECK_ANDROID_XR_ACTIVE;
            tracking.sampled_ns = packet.sample_monotonic_ns;
            update(&tracking);
            assert(controller.GetPose().poseIsValid && value("/input/trigger/value") == 1);
        }
        tracking.hands[side].pose.flags = 0;
        update(&tracking);
        assert(!controller.GetPose().poseIsValid && model() == controllerModel);
        assert(value("/input/trigger/value") == 1);
        tracking.hands[side].active = 0;
        update(&tracking); update(&tracking);
        assert(controller.GetPose().poseIsValid && model() != controllerModel);
        assert(value("/input/trigger/value") == 0);
        packet.hands[side].pinch = 0; update(&tracking);
        packet.hands[side].pinch = 1; update(&tracking);
        assert(value("/input/trigger/value") == 1);
        controller.Deactivate();
        VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    }
    puts("PASS: unavailable tracking neutralizes hybrid pose/input without optical aim or model fallback");
}

int main() {
    Context context;
    VRDriverContext() = &context;
    CardboardController controller(false);
    controller.SetProfile(PadProfile::Vive);
    assert(controller.Activate(1) == VRInitError_None);
    const auto gripHandle=context.input.handles.at("/input/grip/click");
    const auto clickHandle=context.input.handles.at("/input/trigger/click");
    const auto triggerHandle=context.input.handles.at("/input/trigger/value");
    const auto assertReleasedAtPose = [&] {
        const auto &inputs=context.host.inputsAtPose.back();
        assert(inputs.at(gripHandle)==0 && inputs.at(clickHandle)==0 && inputs.at(triggerHandle)==0);
    };
    dd_spatial_hand hand{};
    for (int i=0; i<500; ++i) controller.UpdateOptical(hand, false, nullptr, 1000000000+i, 1, 1);
    assert(context.host.poses.size() == 500);
    assert(context.log.lines.size()==2);
    for (const auto &pose : context.host.poses) {
        assert(pose.deviceIsConnected && !pose.poseIsValid);
        assert(pose.result == TrackingResult_Running_OutOfRange);
        assert(pose.qRotation.w == 1);
    }
    hand.pose = {1000000000, {1,2,3}, {0,0,0,1}, DD_SPATIAL_VALID | DD_SPATIAL_TRIGGER_VALID | DD_SPATIAL_GRIP_VALID};
    int64_t now=1000000000;
    uint64_t connection=1,origin=1;
    uint16_t epochs=0;
    const auto observe = [&](float trigger,uint32_t grip) {
        now+=80000000;hand.pose.time_ns=now;hand.trigger=trigger;hand.grip=grip;
        controller.UpdateOptical(hand,true,nullptr,now,connection,origin,epochs);
    };
    const auto missing = [&](bool headValid,int64_t elapsed) {
        now+=elapsed;controller.UpdateOptical({},headValid,nullptr,now,connection,origin,epochs);
    };
    const auto assertPressed = [&] {
        assert(context.host.poses.back().poseIsValid);
        assert(context.input.values.at(gripHandle)==1 && context.input.values.at(clickHandle)==1 &&
            context.input.values.at(triggerHandle)==1);
    };
    observe(0,0);observe(1,1);assertPressed();
    now+=200000000;controller.UpdateOptical(hand,true,nullptr,now,connection,origin,epochs);
    assertPressed();
    ++now;controller.UpdateOptical(hand,true,nullptr,now,connection,origin,epochs);
    assert(context.host.poses.back().deviceIsConnected && context.host.poses.back().poseIsValid);
    assertReleasedAtPose();
    now=hand.pose.time_ns+350000000;controller.UpdateOptical(hand,true,nullptr,now,connection,origin,epochs);
    assert(context.host.poses.back().poseIsValid);
    assert(std::abs(context.host.poses.back().poseTimeOffset+.35)<1e-9);
    assertReleasedAtPose();
    ++now;controller.UpdateOptical(hand,true,nullptr,now,connection,origin,epochs);
    assert(context.host.poses.back().deviceIsConnected && !context.host.poses.back().poseIsValid);
    assertReleasedAtPose();

    observe(0,0);observe(1,1);assertPressed();
    missing(true,50000000);
    assert(context.host.poses.back().poseIsValid && context.host.poses.back().vecPosition[0]==1);
    assert(std::abs(context.host.poses.back().poseTimeOffset+.05)<1e-9);
    assertReleasedAtPose();
    missing(true,150000000);
    assert(context.host.poses.back().poseIsValid);
    assert(std::abs(context.host.poses.back().poseTimeOffset+.2)<1e-9);
    missing(true,150000000);
    assert(context.host.poses.back().poseIsValid);
    assert(std::abs(context.host.poses.back().poseTimeOffset+.35)<1e-9);
    assertReleasedAtPose();
    missing(true,1);assert(!context.host.poses.back().poseIsValid);
    observe(1,1);assertReleasedAtPose();
    observe(0,0);observe(1,1);assertPressed();
    missing(false,10000000);
    assert(!context.host.poses.back().poseIsValid);assertReleasedAtPose();
    missing(true,10000000);assert(!context.host.poses.back().poseIsValid);

    observe(0,0);observe(1,1);assertPressed();
    epochs=1;observe(1,1);
    assert(context.host.poses.back().poseIsValid);
    assert(context.input.values.at(gripHandle)==1 && context.input.values.at(clickHandle)==0 && context.input.values.at(triggerHandle)==0);
    epochs=0x101;observe(1,1);assertReleasedAtPose();
    hand.trigger=0;hand.grip=0;
    controller.UpdateOptical(hand,true,nullptr,now+1,connection,origin,epochs);
    observe(1,1);assertReleasedAtPose();
    observe(0,0);observe(1,1);assertPressed();

    ++origin;missing(true,10000000);
    assert(!context.host.poses.back().poseIsValid);assertReleasedAtPose();
    observe(1,1);assertReleasedAtPose();
    observe(0,0);observe(1,1);assertPressed();
    ++connection;missing(true,10000000);
    assert(!context.host.poses.back().poseIsValid);assertReleasedAtPose();

    observe(0,0);observe(1,1);assertPressed();
    controller.Disconnect();
    assert(!context.host.poses.back().deviceIsConnected);assertReleasedAtPose();
    missing(true,10000000);assert(!context.host.poses.back().poseIsValid);
    observe(1,1);assertReleasedAtPose();
    controller.Update(controller.GetPose(),nullptr,false,0);
    missing(true,10000000);assert(!context.host.poses.back().poseIsValid);
    PadState pad{};pad.axes[0]=16384;pad.buttons=PAD_SELECT;
    controller.UpdateOptical({},true,&pad,++now,connection,origin,epochs);
    assert(!context.host.poses.back().poseIsValid);
    assert(std::any_of(context.input.values.begin(),context.input.values.end(),[](const auto &v){ return v.second!=0; }));

    observe(0,0);observe(1,1);assertPressed();
    context.input.failures[gripHandle]=VRInputError_IPCError;
    missing(false,10000000);
    assert(context.input.values.at(gripHandle)==1 && context.input.values.at(triggerHandle)==0);
    assert(context.log.lines.back().find("error=7/0/0")!=std::string::npos);
    const auto failedLogCount=context.log.lines.size();
    for(int i=0;i<10;++i) missing(false,1);
    assert(context.log.lines.size()==failedLogCount);
    context.input.failures[gripHandle]=VRInputError_None;
    missing(false,10000000);
    assertReleasedAtPose();
    assert(context.log.lines.back().find("error=0/0/0")!=std::string::npos);
    assert(context.log.lines.size()==failedLogCount+1);
    observe(1,1);assertReleasedAtPose();
    observe(0,0);observe(1,1);assertPressed();
    const dd_spatial_pose aimHead{now, {0,1.6f,0}, {0,0,0,1}, DD_SPATIAL_VALID};
    controller.UpdateOptical(hand,true,nullptr,now,connection,origin,epochs,&aimHead);
    assertPressed();
    const auto aimed = controller.GetPose();
    const auto expectedAim = ShoulderAim({0,1.6f,0}, {}, {1,2,3}, false, {});
    assert(std::abs(aimed.qRotation.w - expectedAim.w) < 1e-6);
    assert(std::abs(aimed.qRotation.y - expectedAim.y) < 1e-6);
    assert(aimed.poseTimeOffset == 0);
    controller.UpdateOptical(hand,true,nullptr,now,connection,origin,epochs);
    assertPressed();
    assert(controller.GetPose().qRotation.w == 1);
    const auto palmRotation=Normalize(AboutX(.4)*AboutY(-.6));
    hand.pose.rotation[0]=palmRotation.x;hand.pose.rotation[1]=palmRotation.y;
    hand.pose.rotation[2]=palmRotation.z;hand.pose.rotation[3]=palmRotation.w;
    controller.UpdateOptical(hand,true,nullptr,now,connection,++origin,epochs);
    const auto normal=controller.GetPose();
    assert(std::abs(normal.qRotation.w-palmRotation.w)<1e-6);
    assert(std::abs(normal.qRotation.x-palmRotation.x)<1e-6);
    assert(std::abs(normal.qRotation.y-palmRotation.y)<1e-6);
    assert(std::abs(normal.qRotation.z-palmRotation.z)<1e-6);
    assert(normal.vecPosition[0]==hand.pose.position[0] && normal.poseTimeOffset==0);
    controller.UpdateOptical({},true,nullptr,now+1,connection,origin,epochs,&aimHead);
    assertReleasedAtPose();
    assert(!controller.GetPose().poseIsValid);
    controller.Deactivate();
    for(const auto &value:context.input.values) assert(value.second==0);
    const auto poseCount=context.host.poses.size();
    controller.UpdateOptical(hand,true,nullptr,now+1,connection,origin,epochs);
    assert(context.host.poses.size()==poseCount);
    assert(controller.Activate(1) == VRInitError_None);
    const auto xrClick = context.input.handles.at("/input/trigger/click");
    const auto xrTrigger = context.input.handles.at("/input/trigger/value");
    DroidDeckXrSkeleton xrSkeleton{};
    auto xrPacket = droiddeck_xr_input_v2_neutral(101, 1);
    xrPacket.sample_monotonic_ns = now;
    xrPacket.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
    auto &xrHand = xrPacket.hands[0];
    xrHand.source = DROIDDECK_XR_SOURCE_RUNTIME_ACTION;
    xrHand.grip.flags = xrHand.aim.flags = 15;
    xrHand.status_bits = 31; xrHand.ready_bits = xrHand.capability_bits = DROIDDECK_XR_PINCH | DROIDDECK_XR_GRASP;
    xrHand.grip.position[0] = .3f;
    xrHand.aim.position[0] = 2;
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    xrHand.pinch = 1;
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(xrClick) == 1 && context.input.values.at(xrTrigger) == 1);
    assert(controller.GetPose().vecPosition[0] == xrHand.grip.position[0]);
    const auto systemHandle = context.input.handles.at("/input/system/click");
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, true, true);
    assert(context.input.values.at(xrClick) == 0 && context.input.values.at(xrTrigger) == 0);
    assert(context.input.values.at(systemHandle) == 1);
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false, true);
    assert(context.input.values.at(systemHandle) == 0 && context.input.values.at(xrTrigger) == 0);
    xrHand.select = 1;
    xrHand.pinch = 0;
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(xrClick) == 0);
    xrHand.pinch = 1;
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(xrClick) == 1);
    xrPacket.flags &= ~DROIDDECK_XR_INPUT_FOCUSED;
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.host.inputsAtPose.back().at(xrClick) == 0 && !controller.GetPose().poseIsValid);
    xrPacket.flags |= DROIDDECK_XR_INPUT_FOCUSED;
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(xrClick) == 0 && context.input.values.at(xrTrigger) == 0);
    xrHand.pinch = 0; controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    xrHand.pinch = 1; controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(xrClick) == 1);
    controller.UpdateXrHand(xrPacket, xrSkeleton, now+100000001, false);
    assert(context.input.values.at(xrClick) == 0 && !controller.GetPose().poseIsValid);
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(xrClick) == 0);
    xrHand.pinch = 0; controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    xrHand.pinch = 1; controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(xrClick) == 1);
    xrHand.grip.flags = 7;
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(xrClick) == 0 && !controller.GetPose().poseIsValid);
    xrHand = {};
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, true, true);
    assert(controller.GetPose().deviceIsConnected && !controller.GetPose().poseIsValid);
    assert(context.input.values.at(systemHandle) == 1);
    controller.UpdateXrHand(xrPacket, xrSkeleton, now, false);
    assert(context.input.values.at(systemHandle) == 0);
    controller.UpdateXrHand(xrPacket, xrSkeleton, now + 100000001, true);
    assert(context.input.values.at(systemHandle) == 0 && !controller.GetPose().deviceIsConnected);
    controller.Deactivate();
    HandController joyCon(false);
    assert(joyCon.Activate(2) == VRInitError_None);
    const auto joyTrigger = context.input.handles.at("/input/trigger/click");
    droiddeck_joycon_hand joy{};
    joy.rotation[0] = 1;
    joy.time_ns = now;
    joy.buttons = DROIDDECK_JOYCON_TRIGGER;
    droiddeck_spatialpose spatial{};
    spatial.flags = DD_SPATIAL_ACTIVE | DD_SPATIAL_JOYCONS;
    spatial.connection = spatial.origin = 1;
    spatial.head = {now, {2, 1.6f, 3}, {0,0,0,1}, DD_SPATIAL_VALID};
    spatial.hands[0].pose = {now, {1.8f, 1.2f, 2.6f}, {0,0,0,1}, DD_SPATIAL_VALID};
    assert(joyCon.Update(&joy, false, {1,0,0,0}, now, &spatial));
    auto pose = joyCon.GetPose();
    assert(pose.poseIsValid && context.input.values.at(joyTrigger) == 1);
    Quat expectedRotation; Vec3 expectedPosition;
    IndexRawPose(false, {}, {1.8f, 1.2f, 2.6f}, &expectedRotation, &expectedPosition);
    assert(std::abs(pose.vecPosition[0] - expectedPosition.x) < 1e-5);
    assert(std::abs(pose.qRotation.w - expectedRotation.w) < 1e-5);
    spatial.hands[0].pose.flags = 0;
    joyCon.Update(&joy, false, {1,0,0,0}, now, &spatial);
    assert(joyCon.GetPose().poseIsValid && context.input.values.at(joyTrigger) == 1);
    const Quat turned = AboutY(.7);
    joy.rotation[0] = turned.w; joy.rotation[2] = turned.y;
    joyCon.Update(&joy, false, {1,0,0,0}, now, &spatial);
    const auto beforeLoss = joyCon.GetPose().qRotation;
    spatial.head.flags = 0;
    joyCon.Update(&joy, false, {1,0,0,0}, now, &spatial);
    assert(!joyCon.GetPose().poseIsValid && context.input.values.at(joyTrigger) == 0);
    assert(context.host.inputsAtPose.back().at(joyTrigger) == 0);
    spatial.head.flags = DD_SPATIAL_VALID;
    joyCon.Update(&joy, false, {1,0,0,0}, now, &spatial);
    assert(std::abs(joyCon.GetPose().qRotation.w - beforeLoss.w) < 1e-5);
    assert(std::abs(joyCon.GetPose().qRotation.y - beforeLoss.y) < 1e-5);
    spatial.origin++;
    spatial.head.position[0] = -2;
    joyCon.Update(&joy, false, {1,0,0,0}, now, &spatial);
    assert(joyCon.GetPose().poseIsValid && joyCon.GetPose().vecPosition[0] < -2);
    joyCon.Update(nullptr, false, {1,0,0,0}, now, &spatial);
    assert(!joyCon.GetPose().deviceIsConnected && context.input.values.at(joyTrigger) == 0);
    puts("PASS: optical controller recovery and Luma Joy-Con camera, fallback, head loss and origin changes");

    for (const PadProfile profile : {PadProfile::Index, PadProfile::Touch}) {
        auto &padContext = context;
        padContext.input.handles.clear();
        CardboardController pad(true);
        pad.SetProfile(profile);
        assert(pad.Activate(2) == VRInitError_None);
        const auto &h = padContext.input.handles;
        const std::string stick = profile == PadProfile::Touch ? "/input/joystick" : "/input/thumbstick";
        assert(!h.count("/input/trackpad/x") && h.count("/input/b/click") && h.count("/input/grip/value"));
        assert(h.count("/input/application_menu/click") == (profile == PadProfile::Touch));
        DriverPose_t head{};
        head.qRotation.w = 1;
        PadState state{};
        state.buttons = PAD_A | PAD_RB;
        state.axes[2] = 32767;
        state.axes[4] = 255;
        pad.Update(head, &state, false, 0);
        const auto value = [&](const std::string &name) { return padContext.input.values.at(h.at(name)); };
        assert(value("/input/a/click") == 1 && value("/input/b/click") == 0);
        assert(value(stick + "/x") == 1 && value(stick + "/touch") == 1 && value(stick + "/click") == 0);
        assert(value("/input/grip/value") == 1 && value("/input/trigger/value") == 1);
        pad.Update(head, &state, true, 0);
        assert(value(stick + "/x") == 0);
        state = {};
        state.buttons = PAD_R3;
        pad.Update(head, &state, false, 0);
        assert(value(stick + "/click") == 1);
        state.buttons = PAD_L3 | PAD_R3;
        pad.Update(head, &state, false, 0);
        assert(value(stick + "/click") == 0);
        dd_spatial_hand optical{};
        state = {};
        state.buttons = PAD_A;
        pad.UpdateOptical(optical, false, &state, 1000000000, 1, 1);
        assert(value("/input/a/click") == 1 && value(stick + "/click") == 0 && value(stick + "/touch") == 0);
        state.buttons = PAD_B | PAD_R3;
        pad.UpdateOptical(optical, false, &state, 1000000001, 1, 1);
        assert(value("/input/b/click") == 1 && value("/input/a/click") == 0 && value(stick + "/click") == 1);
        assert(!padContext.input.values.count(0));
    }
    puts("PASS: game pad hands as Index and Quest Touch thumbstick controllers");
    Context xrContext;
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
    VRDriverContext() = &xrContext;
    CardboardController trackedHand(false);
    assert(trackedHand.ConfigureAndroidXr());
    assert(trackedHand.Activate(3) == VRInitError_None);
    assert(xrContext.input.skeletonUpdates == 2);
    const auto handTrigger = xrContext.input.handles.at("/input/trigger/value");
    const auto handProfile = xrContext.properties.strings.at({4, Prop_InputProfilePath_String});
    assert(handProfile == "{indexcontroller}/input/index_controller_profile.json");
    assert(xrContext.input.handles.count("/input/thumbstick/x"));
    assert(!trackedHand.ConfigureAndroidXr());
    auto handPacket = droiddeck_xr_input_v2_neutral(77, 1);
    handPacket.sample_monotonic_ns = 2000000000;
    handPacket.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
    auto &sample = handPacket.hands[0];
    sample.source = DROIDDECK_XR_SOURCE_RUNTIME_ACTION;
    sample.grip.flags = sample.aim.flags = 15;
    sample.status_bits = DROIDDECK_XR_GRIP_ACTIVE | DROIDDECK_XR_AIM_ACTIVE | DROIDDECK_XR_PINCH_ACTIVE;
    sample.capability_bits = sample.ready_bits = DROIDDECK_XR_PINCH;
    DroidDeckXrSkeleton noSkeleton{};
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    sample.pinch = 1;
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    assert(xrContext.input.values.at(handTrigger) == 1);
    noSkeleton.session_generation = 77;
    noSkeleton.origin_epoch = 1;
    noSkeleton.sample_monotonic_ns = 2000000000;
    noSkeleton.active[0] = 1;
    for (int joint = 0; joint < 26; ++joint) {
        noSkeleton.joints[0][joint] = sample.grip;
        noSkeleton.joints[0][joint].position[0] = 0.01f * joint;
    }
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    assert(xrContext.input.skeletonUpdates == 4);
    assert(xrContext.properties.strings.at({4, Prop_RenderModelName_String}) == "{droiddeck}droiddeck_hand_left");
    handPacket.flags |= DROIDDECK_XR_INPUT_LOCAL_HAND_MESH;
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    assert(xrContext.properties.strings.at({4, Prop_RenderModelName_String}) == "{droiddeck}droiddeck_hand_left_xr");
    handPacket.flags &= ~DROIDDECK_XR_INPUT_LOCAL_HAND_MESH;
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    assert(xrContext.properties.strings.at({4, Prop_RenderModelName_String}) == "{droiddeck}droiddeck_hand_left");
    assert(!xrContext.input.poses.empty());
    trackedHand.Disconnect();
    assert(xrContext.input.values.at(handTrigger) == 0);
    assert(xrContext.properties.strings.at({4, Prop_InputProfilePath_String}) == handProfile);
    puts("PASS: Android XR hand profile stays immutable and disconnect releases held input");

    xrContext.input.handles.clear();
    CardboardController galaxyController(false);
    assert(galaxyController.ConfigureAndroidXr(false));
    assert(galaxyController.Activate(4) == VRInitError_None);
    assert(!galaxyController.ConfigureAndroidXr());
    const auto controllerProfile = xrContext.properties.strings.at({5, Prop_InputProfilePath_String});
    assert(controllerProfile == "{oculus}/input/touch_profile.json");
    assert(xrContext.properties.strings.at({5, Prop_ControllerType_String}) == "oculus_touch");
    assert(xrContext.properties.strings.at({4, Prop_ControllerType_String}) == "knuckles");
    const auto &controllerHandles = xrContext.input.handles;
    assert(controllerHandles.count("/input/joystick/x") && controllerHandles.count("/input/x/click"));
    assert(!controllerHandles.count("/input/thumbstick/x") && !controllerHandles.count("/input/skeleton/left"));
    assert(galaxyController.Haptic() && !trackedHand.Haptic());
    const auto controllerTrigger = controllerHandles.at("/input/trigger/value");
    const auto controllerThumbrest = controllerHandles.at("/input/thumbrest/touch");
    const auto controllerX = controllerHandles.at("/input/x/click");
    DroidDeckXrHand controllerState{};
    controllerState.active = 1;
    controllerState.pose.flags = 15;
    controllerState.pose.orientation[3] = 1;
    controllerState.trigger = 1;
    controllerState.buttons = DROIDDECK_XR_PRIMARY_CLICK | DROIDDECK_XR_THUMBREST_TOUCH;

    CardboardHmd routing;
    routing.SetHands(&trackedHand, nullptr);
    assert(routing.ActiveHand(0) == &trackedHand);
    sample.pinch = 0;
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    sample.pinch = 1;
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    assert(xrContext.input.values.at(handTrigger) == 1);
    routing.SetHands(&galaxyController, nullptr);
    assert(routing.ActiveHand(0) == &galaxyController);
    assert(!trackedHand.GetPose().deviceIsConnected && xrContext.input.values.at(handTrigger) == 0);
    galaxyController.UpdateXr(controllerState, 0);
    assert(galaxyController.GetPose().poseIsValid);
    assert(xrContext.input.values.at(controllerTrigger) == 1);
    assert(xrContext.input.values.at(controllerX) == 1 && xrContext.input.values.at(controllerThumbrest) == 1);
    routing.SetHands(&trackedHand, nullptr);
    assert(routing.ActiveHand(0) == &trackedHand);
    assert(!galaxyController.GetPose().deviceIsConnected && xrContext.input.values.at(controllerTrigger) == 0);
    assert(xrContext.input.values.at(controllerX) == 0 && xrContext.input.values.at(controllerThumbrest) == 0);
    trackedHand.UpdateXrHand(handPacket, noSkeleton, 2000000000, false);
    assert(xrContext.input.values.at(handTrigger) == 0);
    assert(xrContext.properties.strings.at({4, Prop_InputProfilePath_String}) == handProfile);
    assert(xrContext.properties.strings.at({5, Prop_InputProfilePath_String}) == controllerProfile);
    assert(!xrContext.input.values.count(0));
    puts("PASS: Galaxy XR Touch and tracked hands keep separate profiles across switches and release held input");
    testXrJoyConHybrid();
    testXrJoyConPositionContinuity();
    testXrJoyConAimParity();
    testXrJoyConUnknownTracking();
    testJoyConInputParity();
}
