#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace droiddeck::xr {

inline float viewZoomScale(float value) {
    return std::isfinite(value) ? std::clamp(value, 1.f, 3.f) : 1.f;
}

struct XrViewZoom {
    float scale = 1;
    std::array<float, 2> center{.5f, .5f};

    std::array<float, 4> sourceUv(int side) const {
        const float inverse = 1.f / scale;
        return {.5f * inverse, -inverse, .5f * (side + center[0] * (1.f - inverse)),
            1.f - center[1] * (1.f - inverse)};
    }

    std::array<float, 16> projection(const float source[4][4]) const {
        std::array<float, 16> result{};
        for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
            result[column*4+row] = source[row][column];
        if (scale != 1.f) for (int column = 0; column < 4; ++column) for (int row = 0; row < 2; ++row)
            result[column*4+row] = scale * source[row][column] +
                (1.f - scale) * (2.f * center[row] - 1.f) * source[3][column];
        return result;
    }
};

inline std::array<XrViewZoom, 2> viewZoomTransforms(const float projection[2][4][4], float requested) {
    const float scale = viewZoomScale(requested);
    std::array<XrViewZoom, 2> result{};
    if (scale == 1.f) return result;
    for (int eye = 0; eye < 2; ++eye) {
        const auto &source = projection[eye];
        for (const auto &row : source) for (float value : row) if (!std::isfinite(value)) return {};
        if (source[0][0] <= 0 || source[1][1] <= 0 || std::abs(source[3][2] + 1.f) > .0001f ||
            std::abs(source[3][3]) > .0001f) return {};
        for (int axis = 0; axis < 2; ++axis) {
            const float center = (1.f - source[axis][2]) * .5f;
            if (center < 0 || center > 1) return {};
            result[eye].center[axis] = center;
        }
        result[eye].scale = scale;
    }
    return result;
}

}
