#include "XrJoyConInput.h"
#include "xr_touch_input.h"
#include <cassert>
#include <limits>

using namespace droiddeck::xr;
using namespace droiddeck;

static void rotation(int32_t *values, const Quat &q) {
    const float xyzw[]{float(q.x), float(q.y), float(q.z), float(q.w)};
    std::memcpy(values + 3, xyzw, sizeof(xyzw));
    values[7] = 1;
}

static DroidDeckXrPose headPose(const Quat &q) {
    return {{float(q.x),float(q.y),float(q.z),float(q.w)}, {}, 5};
}

static void expectRotation(const DroidDeckXrHand &hand, const Quat &expected) {
    assert(hand.active == DROIDDECK_XR_JOYCON_ACTIVE);
    assert(hand.pose.flags == 5);
    const auto &q = hand.pose.orientation;
    const auto dot = q[0]*expected.x + q[1]*expected.y + q[2]*expected.z + q[3]*expected.w;
    assert(std::abs(std::abs(dot) - 1) < 1e-5);
    for (float position : hand.pose.position) assert(position == 0);
}

int main() {
    int32_t values[3]{(1 << 6) | DROIDDECK_JOYCON_TRIGGER | DROIDDECK_JOYCON_GRIP |
        DROIDDECK_JOYCON_A | DROIDDECK_JOYCON_B | DROIDDECK_JOYCON_STICK | DROIDDECK_JOYCON_SYSTEM,
        -32767, 32767};
    auto hand = joyConControls(values);
    auto input = xr_touch_input(hand, false);
    assert(hand.active == DROIDDECK_XR_JOYCON_ACTIVE);
    assert(!hand.pose.flags);
    assert(input.trigger == 1 && input.grip == 1 && input.triggerClick && input.gripClick);
    assert(input.primaryClick && input.secondaryClick && input.stickClick && input.systemClick);
    assert(input.triggerTouch && input.primaryTouch && input.secondaryTouch && input.stickTouch);
    assert(input.stickX == -1 && input.stickY == 1);
    values[0] = 1 << 6;
    values[1] = values[2] = 0;
    hand = joyConControls(values);
    input = xr_touch_input(hand, false);
    assert(hand.active == DROIDDECK_XR_JOYCON_ACTIVE && !hand.pose.flags);
    assert(!input.triggerClick && !input.gripClick && !input.primaryClick && !input.secondaryClick && !input.systemClick);
    assert(!input.triggerTouch && !input.primaryTouch && !input.secondaryTouch && !input.stickTouch);
    values[1] = 4000;
    assert(xr_touch_input(joyConControls(values), false).stickTouch);
    values[1] = std::numeric_limits<int32_t>::min(); values[2] = std::numeric_limits<int32_t>::max();
    input = xr_touch_input(joyConControls(values), false);
    assert(input.stickX == -1 && input.stickY == 1);
    values[0] = DROIDDECK_JOYCON_TRIGGER | DROIDDECK_JOYCON_SYSTEM;
    hand = joyConControls(values);
    input = xr_touch_input(hand, false);
    assert(!hand.active && !hand.pose.flags);
    assert(!input.triggerClick && !input.systemClick && input.stickX == 0 && input.stickY == 0);

    JoyConXrOrientation tracker;
    int32_t sample[kJoyConSnapshotStride]{(1 << 6) | DROIDDECK_JOYCON_TRIGGER};
    auto head = headPose(AboutY(.8));
    rotation(sample, AboutY(-.3) * AboutX(.2));
    expectRotation(tracker.update(sample, head, 1, 1), AboutY(.8) * AboutX(.2));
    // Head turns after alignment must not rotate the controller.
    head = headPose(AboutY(-.6));
    rotation(sample, AboutY(-.1) * AboutX(.5));
    expectRotation(tracker.update(sample, head, 1, 1), AboutY(1.0) * AboutX(.5));
    sample[7] = 0;
    hand = tracker.update(sample, head, 1, 1);
    assert(hand.active == DROIDDECK_XR_JOYCON_ACTIVE && hand.trigger == 1 && !hand.pose.flags);
    sample[7] = 1;
    expectRotation(tracker.update(sample, head, 1, 1), AboutY(1.0) * AboutX(.5));
    ++sample[8];
    expectRotation(tracker.update(sample, head, 1, 1), AboutY(-.6) * AboutX(.5));
    head = headPose(AboutY(.4));
    expectRotation(tracker.update(sample, head, 1, 2), AboutY(.4) * AboutX(.5));
    head = headPose(AboutY(.7));
    expectRotation(tracker.update(sample, head, 2, 2), AboutY(.7) * AboutX(.5));

    sample[0] = 0;
    assert(!tracker.update(sample, head, 2, 2).active);
    sample[0] = (1 << 6) | DROIDDECK_JOYCON_TRIGGER;
    rotation(sample, AboutX(M_PI/2));
    assert(!tracker.update(sample, head, 2, 2).pose.flags);
    rotation(sample, {});
    head.flags = 0;
    assert(!tracker.update(sample, head, 2, 2).pose.flags);
    head = headPose(AboutY(.9));
    expectRotation(tracker.update(sample, head, 2, 2), AboutY(.9));
    sample[3] = int32_t(0x7fc00000);
    hand = tracker.update(sample, head, 2, 2);
    assert(!hand.pose.flags && hand.trigger == 1);
    rotation(sample, {0,0,0,0});
    assert(!tracker.update(sample, head, 2, 2).pose.flags);

    for (int side = 0; side < 2; ++side) {
        JoyConXrOrientation aligned;
        int32_t controller[kJoyConSnapshotStride]{1 << 6};
        rotation(controller, {});
        expectRotation(aligned.update(controller, headPose({}), 3, 1), {});
        const Quat roll{std::cos(.3), 0, 0, std::sin(.3)};
        const auto fixed = AboutY(side ? .7 : -.7) * AboutX(-.4) * roll;
        rotation(controller, fixed);
        for (const auto &look : {AboutY(-1.1), AboutY(1.1), AboutX(.6), roll})
            expectRotation(aligned.update(controller, headPose(look), 3, 1), fixed);
        for (const auto &imu : {AboutY(-.8), AboutY(.8), AboutX(-.6), AboutX(.6), roll, Conjugate(roll)}) {
            rotation(controller, imu);
            expectRotation(aligned.update(controller, headPose(AboutY(.9)), 3, 1), imu);
        }
    }
}
