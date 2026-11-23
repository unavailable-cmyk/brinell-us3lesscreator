#pragma once
#include "TriggerTransformTypes.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace vulkanizer {
struct TransformBounds {
    double left = std::numeric_limits<double>::infinity();
    double bottom = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double top = -std::numeric_limits<double>::infinity();

    bool valid() const {
        return std::isfinite(left) && std::isfinite(bottom) && std::isfinite(right) &&
               std::isfinite(top) && left <= right && bottom <= top;
    }
    void include(double x, double y) {
        left = std::min(left, x); right = std::max(right, x);
        bottom = std::min(bottom, y); top = std::max(top, y);
    }
    void include(TransformBounds const& other) {
        include(other.left, other.bottom); include(other.right, other.top);
    }
    bool contains(double x, double y) const {
        return x >= left && x <= right && y >= bottom && y <= top;
    }
    bool intersects(TransformBounds const& other, double margin = 0) const {
        return right + margin >= other.left && left - margin <= other.right &&
               top + margin >= other.bottom && bottom - margin <= other.top;
    }
};

inline TransformBounds advanceTransformBounds(TransformBounds bounds, gpu::Action const& action,
        double minOffsetX, double maxOffsetX, double minOffsetY, double maxOffsetY) {
    TransformBounds result;
    if (action.kind == 0) {
        // Including both locked and unlocked X motion remains conservative.
        result.left = bounds.left + std::min(0.0, action.dx);
        result.right = bounds.right + std::max(0.0, action.dx);
        result.bottom = bounds.bottom + action.dy;
        result.top = bounds.top + action.dy;
    } else {
        const double rx[2]{bounds.left - maxOffsetX - action.centerX, bounds.right - minOffsetX - action.centerX};
        const double ry[2]{bounds.bottom - maxOffsetY - action.centerY, bounds.top - minOffsetY - action.centerY};
        for (auto x : rx) for (auto y : ry) {
            const double px = action.matrixA * x + action.matrixC * y + action.matrixTx + action.dx;
            const double py = action.matrixB * x + action.matrixD * y + action.matrixTy + action.dy;
            result.include(px + minOffsetX, py + minOffsetY);
            result.include(px + maxOffsetX, py + maxOffsetY);
        }
    }
    // Enclose native float narrowing and the double delta/cancellation sequence.
    const double products = (std::abs(action.matrixA) + std::abs(action.matrixB) + std::abs(action.matrixC) + std::abs(action.matrixD)) *
        (std::max(std::abs(bounds.left), std::abs(bounds.right)) + std::max(std::abs(bounds.bottom), std::abs(bounds.top)) +
         std::abs(action.centerX) + std::abs(action.centerY) + std::abs(minOffsetX) + std::abs(maxOffsetX) +
         std::abs(minOffsetY) + std::abs(maxOffsetY));
    const double magnitude = std::max({products, std::abs(bounds.left), std::abs(bounds.right),
        std::abs(bounds.bottom), std::abs(bounds.top), std::abs(result.left), std::abs(result.right),
        std::abs(result.bottom), std::abs(result.top), std::abs(action.centerX), std::abs(action.centerY), 1.0});
    const double error = magnitude * std::numeric_limits<float>::epsilon() * 64.0 + 0.001;
    result.left -= error; result.right += error; result.bottom -= error; result.top += error;
    return result;
}
}
