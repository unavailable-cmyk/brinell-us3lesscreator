#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vulkanizer {
// Observes native inputs only. It never supplies, clamps or rescales a delta.
struct GameplaySpeedWindow {
    std::uint64_t frames = 0, ticks = 0, invalidInputs = 0;
    double frameSeconds = 0, physicsSeconds = 0;
    double minPhysicsDelta = std::numeric_limits<double>::infinity(), maxPhysicsDelta = 0;
    void frame(double dt) {
        ++frames;
        if (!std::isfinite(dt) || dt < 0) { ++invalidInputs; return; }
        frameSeconds += dt;
    }
    void tick(double dt) {
        ++ticks;
        if (!std::isfinite(dt) || dt < 0) { ++invalidInputs; return; }
        physicsSeconds += dt;
        minPhysicsDelta = std::min(minPhysicsDelta, dt);
        maxPhysicsDelta = std::max(maxPhysicsDelta, dt);
    }
    bool fullSpeed(double wallSeconds, double levelSeconds, double timeWarp) const {
        if (invalidInputs || !ticks || !frames || wallSeconds < 1 || !std::isfinite(levelSeconds) ||
            !std::isfinite(timeWarp) || std::abs(timeWarp - 1) > 0.0001) return false;
        return std::abs(levelSeconds / wallSeconds - 1) <= 0.05;
    }
};
}
