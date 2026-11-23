#pragma once
#include <cstdint>

namespace vulkanizer {
// From GD 2.2091's moveObjects/rotateObjects pre-write bookkeeping.
template <class Object>
constexpr void preserveTransformHistory(Object& object, std::uint32_t commandIndex) {
    if (!object.m_isDecoration2 && object.m_unk4C4 != commandIndex) {
        object.m_lastPosition.x = static_cast<float>(object.m_positionX);
        object.m_lastPosition.y = static_cast<float>(object.m_positionY);
        object.m_unk4C4 = commandIndex;
        object.dirtifyObjectRect();
    }
}

namespace parity_test {
struct Object {
    bool m_isDecoration2 = false;
    std::uint32_t m_unk4C4 = 0;
    double m_positionX = 12.5, m_positionY = -7.25;
    struct { float x = 0, y = 0; } m_lastPosition;
    unsigned invalidations = 0;
    constexpr void dirtifyObjectRect() { ++invalidations; }
};
constexpr bool historyMatchesVanilla() {
    Object object;
    preserveTransformHistory(object, 1);
    if (object.m_lastPosition.x != 12.5f || object.m_lastPosition.y != -7.25f ||
        object.m_unk4C4 != 1 || object.invalidations != 1) return false;
    object.m_positionX = 99;
    preserveTransformHistory(object, 1);
    if (object.m_lastPosition.x != 12.5f || object.invalidations != 1) return false;
    preserveTransformHistory(object, 2);
    if (object.m_lastPosition.x != 99.f || object.invalidations != 2) return false;
    object.m_isDecoration2 = true;
    object.m_positionX = 101;
    preserveTransformHistory(object, 3);
    return object.m_lastPosition.x == 99.f && object.invalidations == 2 && object.m_unk4C4 == 2;
}
static_assert(historyMatchesVanilla(), "GD history must be captured once per command before mutation");
}
}
