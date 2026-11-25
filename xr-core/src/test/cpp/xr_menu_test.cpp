#include "XrMenu.h"
#include "xr_skeleton.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

using namespace droiddeck;
using namespace droiddeck::xr;

static void near(float value, float expected) { assert(std::abs(value - expected) < .00001f); }

static DroidDeckXrPoseV2 pose(Vec3 position, Quat rotation = {}) {
    return {{float(position.x), float(position.y), float(position.z)},
        {float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w)}, 15};
}

static DroidDeckXrPose head(Vec3 position, Quat rotation = {}) {
    return {{float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w)},
        {float(position.x), float(position.y), float(position.z)}, 15};
}

static void testGeometry() {
    const auto panel = menuPlacement(head({0, 1.6, 0}));
    assert(poseValid(panel));
    near(panel.position[0], 0); near(panel.position[1], 1.6f); near(panel.position[2], -1.4f);
    for (float x : {-.4f, 0.f, .4f}) for (float y : {-.3f, 0.f, .3f}) {
        const auto hit = menuHit(panel, pose({x, 1.6+y, 0}));
        assert(hit.valid); near(hit.x, .5f+x/kMenuWidthMeters); near(hit.y, .5f-y/kMenuHeightMeters);
    }
    assert(!menuHit(panel, pose({.5, 1.6, 0})).valid);
    assert(!menuHit(panel, pose({0, 2.1, 0})).valid);
    assert(!menuHit(panel, pose({0, 1.6, -2}, AboutY(M_PI))).valid);
    assert(!menuHit(panel, pose({0, 1.6, 0}, AboutY(M_PI/2))).valid);
    assert(!menuHit(panel, pose({0, 1.6, 0}, AboutY(M_PI))).valid);
    for (double yaw : {-1.2, .4, 2.5}) {
        const Vec3 origin{2, 1.7, 3};
        const auto rotation = AboutY(yaw);
        const auto placed = menuPlacement(head(origin, rotation * AboutX(.7)));
        const auto ray = pose(origin + Rotate(rotation, {.2, -.1, 0}), rotation);
        const auto hit = menuHit(placed, ray);
        assert(hit.valid); near(hit.x, .5f+.2f/kMenuWidthMeters); near(hit.y, .5f+.1f/kMenuHeightMeters);
    }
    auto invalid = head({0, 1.6, 0}); invalid.flags = 1;
    assert(!poseValid(menuPlacement(invalid)));
    invalid.flags = 15; invalid.position[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!poseValid(menuPlacement(invalid)));
    auto invalidRay = pose({0, 1.6, 0}); invalidRay.orientation[3] = 0;
    assert(!menuHit(panel, invalidRay).valid);
}

static void testPointer() {
    MenuPointer pointer;
    std::vector<int> events;
    const auto emit = [&](int action, float x, float y) {
        assert(x >= 0 && x <= 1 && y >= 0 && y <= 1); events.push_back(action);
    };
    std::array<MenuHit, 2> hits{{{true, .2f, .4f}, {true, .7f, .6f}}};
    pointer.update(hits, {true, true}, emit);
    assert(events == std::vector<int>{7});
    pointer.update(hits, {false, false}, emit);
    pointer.update(hits, {true, false}, emit);
    assert(events.back() == 0);
    pointer.update(hits, {true, true}, emit);
    assert(events.back() == 2);
    hits[0].valid = false;
    pointer.update(hits, {true, true}, emit);
    assert(events.back() == 3);
    hits[0].valid = true;
    pointer.update(hits, {true, true}, emit);
    assert(events.back() == 7);
    pointer.update(hits, {false, false}, emit);
    pointer.update(hits, {false, true}, emit);
    assert(events.back() == 0);
    const auto before = events.size();
    pointer.update(hits, {false, false}, emit);
    assert(events.size() == before+2 && events[before] == 1 && events.back() == 7);
    pointer.cancel(emit);
    assert(events.back() == 3);
    const auto after = events.size();
    pointer.cancel(emit);
    assert(events.size() == after);
}

static void testInputSuppression() {
    auto raw = droiddeck_xr_input_v2_neutral(1, 1);
    raw.sample_monotonic_ns = 1000000000;
    raw.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE | DROIDDECK_XR_INPUT_GAMEPAD_MENU;
    raw.gamepad_menu_sequence = 7;
    for (auto &hand : raw.hands) {
        hand.aim = hand.grip = pose({.2, 1.2, -.3});
        hand.source = DROIDDECK_XR_SOURCE_RUNTIME_ACTION;
        hand.capability_bits = hand.ready_bits = DROIDDECK_XR_SELECT | DROIDDECK_XR_PINCH | DROIDDECK_XR_GRASP;
        hand.status_bits = DROIDDECK_XR_GRIP_ACTIVE | DROIDDECK_XR_AIM_ACTIVE | DROIDDECK_XR_SELECT_ACTIVE |
            DROIDDECK_XR_PINCH_ACTIVE | DROIDDECK_XR_GRASP_ACTIVE;
    }
    raw.hands[0].pinch = raw.hands[0].select = raw.hands[0].grasp = 1;
    raw.hands[1].grasp = 1;
    DroidDeckXrTracking tracking{};
    tracking.hands[1].active = DROIDDECK_XR_JOYCON_ACTIVE;
    tracking.hands[1].pose = head({.2, 1.2, -.3});
    tracking.hands[1].buttons = DROIDDECK_XR_JOYCON_6DOF | DROIDDECK_XR_PRIMARY_CLICK;
    tracking.hands[1].trigger = 1; tracking.hands[1].stick[0] = .8f;
    MenuInputSuppression suppression;
    auto input = raw; auto packet = tracking;
    suppression.filter(input, packet, true);
    assert(input.hands[0].pinch == 0 && input.hands[0].grasp == 0 && !input.hands[0].ready_bits);
    assert(!(input.flags & DROIDDECK_XR_INPUT_GAMEPAD_MENU) && input.gamepad_menu_sequence == 0);
    assert(packet.hands[1].trigger == 0 && packet.hands[1].stick[0] == 0);
    assert(packet.hands[1].buttons == DROIDDECK_XR_JOYCON_6DOF);
    assert(packet.hands[1].active == DROIDDECK_XR_JOYCON_ACTIVE);
    assert(std::memcmp(&packet.hands[1].pose, &tracking.hands[1].pose, sizeof(DroidDeckXrPose)) == 0);
    assert(std::memcmp(&input.hands[0].aim, &raw.hands[0].aim, sizeof(DroidDeckXrPoseV2)) == 0);
    input = raw; packet = tracking;
    packet.hands[1].buttons |= DROIDDECK_XR_SECONDARY_CLICK;
    suppression.filter(input, packet, false);
    assert(input.hands[0].pinch == 0 && input.hands[0].grasp == 0);
    assert(packet.hands[1].trigger == 0 && packet.hands[1].stick[0] == 0);
    assert(packet.hands[1].buttons == (DROIDDECK_XR_JOYCON_6DOF | DROIDDECK_XR_SECONDARY_CLICK));
    raw.hands[0].pinch = raw.hands[0].select = 0;
    tracking.hands[1].trigger = tracking.hands[1].stick[0] = 0;
    tracking.hands[1].buttons = DROIDDECK_XR_JOYCON_6DOF;
    input = raw; packet = tracking; suppression.filter(input, packet, false);
    raw.hands[0].pinch = raw.hands[0].select = 1;
    tracking.hands[1].trigger = 1; tracking.hands[1].buttons |= DROIDDECK_XR_PRIMARY_CLICK;
    input = raw; packet = tracking; suppression.filter(input, packet, false);
    assert(input.hands[0].pinch == 1 && input.hands[0].grasp == 0);
    assert(packet.hands[1].trigger == 1 && (packet.hands[1].buttons & DROIDDECK_XR_PRIMARY_CLICK));
    assert(input.hands[1].grasp == 1);
    ++raw.origin_epoch;
    input = raw; packet = tracking; suppression.filter(input, packet, false);
    assert(input.hands[0].grasp == 1);
}

static void testJoyConRayUsesExistingPose() {
    auto input = droiddeck_xr_input_v2_neutral(1, 1);
    input.sample_monotonic_ns = 1000000000;
    input.flags = DROIDDECK_XR_INPUT_FOCUSED | DROIDDECK_XR_INPUT_HAND_MODE;
    const auto headPose = head({0, 1.6, 0});
    const auto panel = menuPlacement(headPose);
    for (int side = 0; side < 2; ++side) for (bool optical : {false, true}) {
        JoyConHandPose estimate;
        JoyConHandControls controls;
        controls.active = true; controls.head = headPose; controls.pose = head({}, {});
        controls.pose.flags = 5; controls.opticalPosition = optical;
        controls.sampledNs = input.sample_monotonic_ns;
        input.hands[side].grip = pose({side ? .15 : -.15, 1.4, -.2}, AboutY(1.2));
        input.hands[side].source = DROIDDECK_XR_SOURCE_RUNTIME_ACTION;
        input.hands[side].status_bits = DROIDDECK_XR_GRIP_ACTIVE;
        const auto ray = estimate.update(controls, input, {}, side, input.sample_monotonic_ns);
        assert(poseValid(ray));
        near(ray.orientation[0], 0); near(ray.orientation[1], 0); near(ray.orientation[2], 0); near(ray.orientation[3], 1);
        const auto hit = menuHit(panel, ray);
        if (optical) {
            assert(hit.valid);
            near(hit.x, .5f + (side ? .15f : -.15f)/kMenuWidthMeters);
            near(hit.y, .5f + .2f/kMenuHeightMeters);
        }
    }
}

int main() {
    testGeometry(); testPointer(); testInputSuppression(); testJoyConRayUsesExistingPose();
}
