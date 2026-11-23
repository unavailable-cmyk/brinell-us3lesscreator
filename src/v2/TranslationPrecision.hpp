#pragma once
#include "Scene.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vulkanizer::v2 {
struct TranslationPrecision {
    double coordinateMagnitude = 0;
    double accumulatedError = 0;
    double nextError(Translation offset, Translation delta) const {
        const auto magnitude = coordinateMagnitude + std::abs(offset.x) + std::abs(offset.y) +
            std::abs(delta.x) + std::abs(delta.y);
        return accumulatedError + 8 * std::numeric_limits<double>::epsilon() * magnitude;
    }
    double pixelBound(Translation offset, double cameraScale, double error) const {
        const auto magnitude = coordinateMagnitude + std::abs(offset.x) + std::abs(offset.y) + 1;
        return cameraScale * (error + 8 * std::numeric_limits<float>::epsilon() * magnitude);
    }
};
}
