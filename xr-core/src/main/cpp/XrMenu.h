#pragma once
#include "arm_model.h"
#include "xr_hand_input.h"
#include <array>

namespace droiddeck::xr {

inline constexpr int kMenuWidth = 1280, kMenuHeight = 960;
inline constexpr float kMenuWidthMeters = .9f, kMenuHeightMeters = .675f, kMenuDistanceMeters = 1.4f;

inline DroidDeckXrPoseV2 menuPlacement(const DroidDeckXrPose &head) {
    DroidDeckXrPoseV2 input{{head.position[0], head.position[1], head.position[2]},
        {head.orientation[0], head.orientation[1], head.orientation[2], head.orientation[3]}, head.flags};
    if (!poseValid(input)) return {};
    const auto rotation = AboutY(HeadHeading({head.orientation[3], head.orientation[0], head.orientation[1], head.orientation[2]}));
    const auto position = Vec3{head.position[0], head.position[1], head.position[2]} + Rotate(rotation, {0, 0, -kMenuDistanceMeters});
    return {{float(position.x), float(position.y), float(position.z)},
        {float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w)}, 3};
}

struct MenuHit { bool valid = false; float x = 0, y = 0; };

inline MenuHit menuHit(const DroidDeckXrPoseV2 &panel, const DroidDeckXrPoseV2 &ray) {
    if (!poseValid(panel) || !poseValid(ray)) return {};
    const Quat inverse = Conjugate({panel.orientation[3], panel.orientation[0], panel.orientation[1], panel.orientation[2]});
    const Quat rotation{ray.orientation[3], ray.orientation[0], ray.orientation[1], ray.orientation[2]};
    const auto origin = Rotate(inverse, Vec3{ray.position[0]-panel.position[0], ray.position[1]-panel.position[1], ray.position[2]-panel.position[2]});
    const auto direction = Rotate(inverse, Rotate(rotation, {0, 0, -1}));
    if (origin.z <= 0 || direction.z >= -1e-6) return {};
    const auto point = origin + direction * (-origin.z / direction.z);
    const double x = .5 + point.x / kMenuWidthMeters, y = .5 - point.y / kMenuHeightMeters;
    if (x < 0 || x > 1 || y < 0 || y > 1) return {};
    return {true, float(x), float(y)};
}

class MenuPointer {
public:
    template<class Emit> void cancel(Emit emit) {
        if (hover_ || down_ >= 0) emit(3, last_.x, last_.y);
        hover_ = false; down_ = -1; armed_ = {};
    }

    template<class Emit> void update(const std::array<MenuHit, 2> &hits, const std::array<bool, 2> &pressed, Emit emit) {
        for (int hand = 0; hand < 2; ++hand) if (!pressed[hand]) armed_[hand] = true;
        if (down_ >= 0) {
            if (!hits[down_].valid) { armed_[down_] = false; cancel(emit); return; }
            last_ = hits[down_];
            if (pressed[down_]) { emit(2, last_.x, last_.y); return; }
            emit(1, last_.x, last_.y); down_ = -1;
        }
        int selected = -1;
        for (int hand : {1, 0}) if (hits[hand].valid && pressed[hand] && armed_[hand]) { selected = hand; break; }
        if (selected < 0) for (int hand : {1, 0}) if (hits[hand].valid) { selected = hand; break; }
        if (selected < 0) { cancel(emit); return; }
        hover_ = true; last_ = hits[selected];
        if (pressed[selected] && armed_[selected]) {
            down_ = selected; armed_[selected] = false; emit(0, last_.x, last_.y);
        } else emit(7, last_.x, last_.y);
    }

private:
    int down_ = -1;
    bool hover_ = false;
    std::array<bool, 2> armed_{};
    MenuHit last_{};
};

inline void menuSuppressInput(DroidDeckXrInputV2 &input) {
    input.flags &= ~DROIDDECK_XR_INPUT_GAMEPAD_MENU;
    input.gamepad_menu_sequence = 0;
    for (auto &hand : input.hands) {
        hand.select = hand.pinch = hand.grasp = 0;
        hand.ready_bits = 0;
    }
}

inline void menuSuppressInput(DroidDeckXrHand &hand) {
    hand.trigger = hand.squeeze = hand.stick[0] = hand.stick[1] = 0;
    hand.buttons &= DROIDDECK_XR_JOYCON_6DOF;
}

class MenuInputSuppression {
public:
    void filter(DroidDeckXrInputV2 &input, DroidDeckXrTracking &tracking, bool open) {
        if (generation_ != input.session_generation || epoch_ != input.origin_epoch) {
            held_ = buttons_ = {}; controller_ = {};
            generation_ = input.session_generation; epoch_ = input.origin_epoch;
        }
        for (int side = 0; side < 2; ++side) {
            auto &hand = tracking.hands[side];
            auto &optical = input.hands[side];
            const bool controller = hand.active != 0;
            if (controller != controller_[side]) { held_[side] = buttons_[side] = 0; controller_[side] = controller; }
            const auto suppress = [&](float &value, uint32_t bit, float threshold) {
                if (std::isfinite(value) && std::abs(value) <= threshold) held_[side] &= ~bit;
                else if (open) held_[side] |= bit;
                if (open || (held_[side] & bit)) { value = 0; return true; }
                return false;
            };
            if (controller) {
                suppress(hand.trigger, 1, .25f); suppress(hand.squeeze, 2, .25f);
                suppress(hand.stick[0], 4, .1f); suppress(hand.stick[1], 8, .1f);
                buttons_[side] &= hand.buttons;
                if (open) buttons_[side] |= hand.buttons & ~DROIDDECK_XR_JOYCON_6DOF;
                hand.buttons &= ~buttons_[side];
            } else {
                if (suppress(optical.select, 1, .25f)) optical.ready_bits &= ~DROIDDECK_XR_SELECT;
                if (suppress(optical.pinch, 2, .25f)) optical.ready_bits &= ~DROIDDECK_XR_PINCH;
                if (suppress(optical.grasp, 4, .25f)) optical.ready_bits &= ~DROIDDECK_XR_GRASP;
            }
        }
        if (open) {
            menuSuppressInput(input);
            for (auto &hand : tracking.hands) menuSuppressInput(hand);
        }
    }

private:
    std::array<uint32_t, 2> held_{}, buttons_{};
    std::array<bool, 2> controller_{};
    uint64_t generation_ = 0, epoch_ = 0;
};

}
