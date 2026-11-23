#pragma once
#include <cmath>
#include <cstdint>

namespace vulkanizer::v2 {
// Windows headers define legacy macros named near and far.
enum class VisibilityRegion : unsigned { Intersects, NearViewport, FarOffscreen, Unknown, Count };
inline constexpr const char* visibilityRegionNames[] = {"intersects", "near", "far", "unknown"};
inline constexpr double visibilityCensusMargin = 128.0;
struct CensusBounds { double left, right, bottom, top; };
inline VisibilityRegion classifyVisibility(CensusBounds view, double x, double y, double radius) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || radius < 0 ||
        !std::isfinite(view.left) || !std::isfinite(view.right) || !std::isfinite(view.bottom) || !std::isfinite(view.top) ||
        view.left >= view.right || view.bottom >= view.top) return VisibilityRegion::Unknown;
    const auto overlaps = [&](double margin) {
        return x + radius >= view.left - margin && x - radius <= view.right + margin &&
            y + radius >= view.bottom - margin && y - radius <= view.top + margin;
    };
    return overlaps(0) ? VisibilityRegion::Intersects : overlaps(visibilityCensusMargin) ? VisibilityRegion::NearViewport : VisibilityRegion::FarOffscreen;
}
// Stable hash of event ordinal and object ID: don't select every Nth object
// in container order, which could alias repeating group patterns.
inline bool sampleVisibility(std::uint64_t ordinal, std::uint32_t id) {
    auto x = ordinal ^ (std::uint64_t{id} << 32);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return ((x ^ (x >> 31)) & 31u) == 0; // ~1/32 of materializations
}
}
