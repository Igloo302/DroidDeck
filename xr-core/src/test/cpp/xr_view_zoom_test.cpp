#include "XrViewZoom.h"
#include "xr_frame_math.h"
#include <cassert>
#include <cstring>
#include <limits>

using namespace droiddeck::xr;

static void near(float actual, float expected) {
    assert(std::abs(actual - expected) < .000002f);
}

static DroidDeckXrFrameHeader sourceFrame() {
    DroidDeckXrFrameHeader frame{};
    frame.magic = DROIDDECK_XR_FRAME_MAGIC; frame.version = DROIDDECK_XR_FRAME_VERSION;
    frame.width = 2560; frame.height = 1440; frame.bytes = frame.width * frame.height * 4;
    frame.color_space = DROIDDECK_XR_COLOR_SRGB_RGBA8; frame.frame_id = 17; frame.captured_ns = 1000000000;
    for (int eye = 0; eye < 2; ++eye) {
        for (int axis = 0; axis < 3; ++axis) frame.eye_pose[eye][axis][axis] = 1;
        frame.eye_pose[eye][0][3] = eye ? .032f : -.032f;
        auto &p = frame.projection[eye];
        p[0][0] = 1.2f; p[1][1] = 1.4f;
        p[0][2] = eye ? -.24f : .32f; p[1][2] = eye ? .18f : -.26f;
        p[2][2] = -1; p[2][3] = -.2f; p[3][2] = -1;
    }
    assert(xr_frame_valid(frame));
    return frame;
}

static void testIdentityAndMetadata() {
    auto frame = sourceFrame();
    const auto original = frame;
    for (float requested : {1.f, 1.5f, 3.f}) {
        const auto transforms = viewZoomTransforms(frame.projection, requested);
        for (int eye = 0; eye < 2; ++eye) {
            const auto &zoom = transforms[eye];
            const auto matrix = zoom.projection(frame.projection[eye]);
            const auto uv = zoom.sourceUv(eye);
            if (requested == 1.f) {
                for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
                    assert(matrix[column*4+row] == frame.projection[eye][row][column]);
                assert(uv[0] == .5f && uv[1] == -1.f && uv[2] == eye*.5f && uv[3] == 1.f);
            }
            const auto view = xr_frame_view(frame, eye), before = xr_frame_view(original, eye);
            assert(std::memcmp(&view, &before, sizeof(view)) == 0);
        }
    }
    assert(std::memcmp(&frame, &original, sizeof(frame)) == 0);
}

static void testAsymmetricImageAndGloveAlignment() {
    const auto frame = sourceFrame();
    for (float scale : {1.f, 1.25f, 2.f, 3.f}) {
        const auto transforms = viewZoomTransforms(frame.projection, scale);
        for (int eye = 0; eye < 2; ++eye) {
            const auto &p = frame.projection[eye];
            const auto &zoom = transforms[eye];
            const auto glove = zoom.projection(p);
            const auto uv = zoom.sourceUv(eye);
            near(glove[0], scale*p[0][0]); near(glove[5], scale*p[1][1]);
            near(glove[8], p[0][2]); near(glove[9], p[1][2]);
            for (float depth : {-.7f, -2.f, -5.f}) for (float x : {-.12f, 0.f, .15f}) for (float y : {-.11f, 0.f, .16f}) {
                const std::array<float, 4> point{x, y, depth, 1};
                std::array<float, 4> source{}, target{};
                for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column) {
                    source[row] += p[row][column] * point[column];
                    target[row] += glove[column*4+row] * point[column];
                }
                const float sourceX = (source[0]/source[3] + 1)*.5f, sourceY = (source[1]/source[3] + 1)*.5f;
                const float targetX = (target[0]/target[3] + 1)*.5f, targetY = (target[1]/target[3] + 1)*.5f;
                near(targetX*uv[0] + uv[2], (sourceX + eye)*.5f);
                near(targetY*uv[1] + uv[3], 1-sourceY);
                near(target[2], source[2]); near(target[3], source[3]);
                if (x == 0 && y == 0) {
                    near(targetX, sourceX); near(targetY, sourceY);
                }
            }
        }
    }
}

static void testBoundsAndFallback() {
    auto frame = sourceFrame();
    for (float requested : {1.f, 1.1f, 2.f, 3.f, 8.f}) {
        const auto transforms = viewZoomTransforms(frame.projection, requested);
        for (int eye = 0; eye < 2; ++eye) {
            const auto uv = transforms[eye].sourceUv(eye);
            const float edge = .5f/frame.width;
            for (float x : {0.f, .1f, .5f, .9f, 1.f}) for (float y : {0.f, .1f, .5f, .9f, 1.f}) {
                const float u = x*uv[0] + uv[2], v = y*uv[1] + uv[3];
                assert(u >= eye*.5f && u <= (eye+1)*.5f && v >= 0 && v <= 1);
                const float clamped = std::clamp(u, eye*.5f+edge, (eye+1)*.5f-edge);
                assert(clamped > eye*.5f && clamped < (eye+1)*.5f);
            }
        }
    }
    const auto identity = [&](float requested) {
        const auto transforms = viewZoomTransforms(frame.projection, requested);
        for (const auto &zoom : transforms) assert(zoom.scale == 1.f);
    };
    for (float invalid : {0.f, -2.f, std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) identity(invalid);
    assert(viewZoomTransforms(frame.projection, 8.f)[0].scale == 3.f);
    for (int eye = 0; eye < 2; ++eye) for (int axis = 0; axis < 2; ++axis) {
        const float original = frame.projection[eye][axis][2];
        for (float invalid : {-1.1f, 1.1f, std::numeric_limits<float>::quiet_NaN()}) {
            frame.projection[eye][axis][2] = invalid;
            identity(2.f);
        }
        frame.projection[eye][axis][2] = original;
    }
    frame.projection[1][3][2] = 0;
    identity(2.f);
}

int main() {
    testIdentityAndMetadata();
    testAsymmetricImageAndGloveAlignment();
    testBoundsAndFallback();
}
