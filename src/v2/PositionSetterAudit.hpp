#pragma once
#include <bit>
#include <cstdint>
#include <array>
#include <cmath>

namespace vulkanizer::v2::live::setter_audit {
inline std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }
inline std::uint32_t bits(float value) { return std::bit_cast<std::uint32_t>(value); }
struct Snapshot {
    std::array<std::uint64_t, 2> logical{};
    std::array<std::uint32_t, 2> offsets{}, sprite{}, previous{};
    std::array<std::uint32_t, 4> rotation{};
    std::int32_t outer = 0, middle = 0, stamp = 0;
    std::uint32_t dirty = 0, visibility = 0, groupCount = 0, linkedGroup = 0;
    std::uint64_t groupHash = 0;
};
enum Change : unsigned {
    logical = 1, sprite = 2, history = 4, sections = 8, dirty = 16,
    visibility = 32, offsets = 64, rotation = 128, membership = 256
};
inline unsigned changes(const Snapshot& a, const Snapshot& b) {
    return (a.logical != b.logical ? logical : 0u) | (a.sprite != b.sprite ? sprite : 0u) |
        (a.previous != b.previous || a.stamp != b.stamp ? history : 0u) |
        (a.outer != b.outer || a.middle != b.middle ? sections : 0u) |
        (a.dirty != b.dirty ? dirty : 0u) | (a.visibility != b.visibility ? visibility : 0u) |
        (a.offsets != b.offsets ? offsets : 0u) | (a.rotation != b.rotation ? rotation : 0u) |
        (a.groupCount != b.groupCount || a.groupHash != b.groupHash || a.linkedGroup != b.linkedGroup ? membership : 0u);
}
inline bool matchesInput(const Snapshot& s, std::uint32_t x, std::uint32_t y) {
    return s.logical[0] == bits(static_cast<double>(std::bit_cast<float>(x))) &&
        s.logical[1] == bits(static_cast<double>(std::bit_cast<float>(y)));
}
// The stock sprite setter accepts float coordinates, not the authoritative doubles.
inline bool matchesRoundedLogical(const Snapshot& s, std::uint32_t x, std::uint32_t y) {
    const auto dx = std::bit_cast<double>(s.logical[0]), dy = std::bit_cast<double>(s.logical[1]);
    return std::isfinite(dx) && std::isfinite(dy) &&
        std::isfinite(std::bit_cast<float>(x)) && std::isfinite(std::bit_cast<float>(y)) &&
        bits(static_cast<float>(dx)) == x && bits(static_cast<float>(dy)) == y;
}
inline bool shadowEligible(const Snapshot& s, unsigned cursorFlags, std::uint32_t x, std::uint32_t y) {
    // A stale sprite is expected; a stale logical mirror, pending commands,
    // unexplained history mismatch or pending rebase are not.
    return !(cursorFlags & ~4u) && s.offsets[0] == 0 && s.offsets[1] == 0 &&
        matchesRoundedLogical(s, x, y);
}
inline bool shadowSetterEquivalent(const Snapshot& before, const Snapshot& after,
                                  std::uint32_t x, std::uint32_t y) {
    return changes(before, after) == 0 && after.sprite[0] == x && after.sprite[1] == y &&
        matchesRoundedLogical(after, x, y);
}
// Observations only, not an ownership policy.
enum Bucket : unsigned { identical, unchangedLogical, changedLogical, count };
inline Bucket classify(const Snapshot& before, const Snapshot& after, std::uint32_t x, std::uint32_t y) {
    if (before.logical != after.logical) return changedLogical;
    return matchesInput(before, x, y) ? identical : unchangedLogical;
}
}
