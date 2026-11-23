#pragma once

#include <cstddef>
#include <cstdint>

namespace vulkanizer::gpu {

struct alignas(8) SectionEvent {
    double x = 0, y = 0;
    std::uint32_t actionIndex = 0, reserved = 0;
};

struct alignas(8) ObjectState {
    double x;
    double y;
    float rotation;
    float padding = 0;
    float offsetX = 0, offsetY = 0;
    float rotationOffsetX = 0, rotationOffsetY = 0;
    std::uint32_t options = 0, commandStamp = 0;
    std::int32_t sectionX = -1, sectionY = -1;
    float sectionFactorX = 0, sectionFactorY = 0;
    float historyX = 0, historyY = 0;
    std::uint32_t eventCount = 0, reserved = 0;
    SectionEvent events[4]{};
};

struct alignas(8) Action {
    std::uint32_t kind;  // 0 = move, 1 = rotate
    std::uint32_t flags; // bit 0 = lock rotation, bit 1 = resolved affine, bit 2 = lock X move
    double dx;
    double dy;
    double centerX;
    double centerY;
    float cosine;
    float sine;
    float rotationDelta;
    float padding = 0;
    float matrixA = 0;
    float matrixB = 0;
    float matrixC = 0;
    float matrixD = 0;
    float matrixTx = 0;
    float matrixTy = 0;
    float objectOffsetX = 0;
    float objectOffsetY = 0;
};

struct Span {
    std::uint32_t offset;
    std::uint32_t count;
};

// Shared by the ordered resident compute shaders and their command uploader.
// A single ordered group command, with the native scheduler's resolved matrix.
struct alignas(16) ResidentTranslation {
    double dx, dy;
    std::uint32_t group, stamp;
    std::uint32_t flags = 0;
    std::uint32_t kind = 0; // 0 = translate, 1 = centered rotation, 2 = angle-only rotation
    double centerX = 0, centerY = 0;
    float matrixA = 1, matrixB = 0, matrixC = 0, matrixD = 1;
    float matrixTx = 0, matrixTy = 0, orientation = 0;
    std::uint32_t journalIndex = 0;
};
static_assert(sizeof(ResidentTranslation) == 80 && offsetof(ResidentTranslation, group) == 16 &&
    offsetof(ResidentTranslation, centerX) == 32 && offsetof(ResidentTranslation, matrixA) == 48 &&
    offsetof(ResidentTranslation, journalIndex) == 76);
inline constexpr std::uint32_t residentDecorationStamp = 1;
inline constexpr std::uint32_t residentTrackSections = 2;
inline constexpr std::uint32_t residentRotationFinished = 4;
// Angle-only command masks, calibrated against native rotateObject once per scene.
// Bits 3..4 apply to locked objects, bits 5..6 to freely rotating objects.
inline constexpr std::uint32_t residentPivotlessLockedShift = 3;
inline constexpr std::uint32_t residentPivotlessFreeShift = 5;
struct alignas(16) ResidentSectionEvent {
    double x, y;
    std::uint32_t object, action;
    std::uint32_t reserved[2]{};
};
static_assert(sizeof(ResidentSectionEvent) == 32);
// Exact order of the native centered rotation kernel (no fused operations).
inline void residentRotatePosition(double& x, double& y, const ResidentTranslation& a) noexcept {
    const float rx = static_cast<float>(x - a.centerX), ry = static_cast<float>(y - a.centerY);
    // Prevent contraction even when the compiler uses a fast FP mode.
    const volatile double xa = static_cast<double>(rx) * a.matrixA, yc = static_cast<double>(ry) * a.matrixC;
    const volatile double xb = static_cast<double>(rx) * a.matrixB, yd = static_cast<double>(ry) * a.matrixD;
    const float tx = static_cast<float>((yc + xa) + a.matrixTx);
    const float ty = static_cast<float>((yd + xb) + a.matrixTy);
    const float mx = tx + static_cast<float>(a.dx), my = ty + static_cast<float>(a.dy);
    x += static_cast<double>(mx) - x;
    y += static_cast<double>(my) - y;
}

static_assert(sizeof(SectionEvent) == 24);
static_assert(sizeof(ObjectState) == 176 && offsetof(ObjectState, rotation) == 16 && offsetof(ObjectState, events) == 80);
static_assert(sizeof(Action) == 88 && offsetof(Action, cosine) == 40 && offsetof(Action, matrixA) == 56);
static_assert(sizeof(Span) == 8);

} // namespace vulkanizer::gpu
