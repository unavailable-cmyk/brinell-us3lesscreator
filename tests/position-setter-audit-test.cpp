#include "v2/PositionSetterAudit.hpp"
#include <iostream>

int main() {
    using namespace vulkanizer::v2::live::setter_audit;
    Snapshot a, b;
    a.logical = {bits(2.0), bits(-0.0)};
    b = a;
    if (classify(a, b, bits(2.0f), bits(-0.0f)) != identical) return 1;
    if (classify(a, b, bits(2.0f), bits(0.0f)) != unchangedLogical) return 2;
    b.sprite[0] = bits(3.0f);
    if (changes(a, b) != sprite || classify(a, b, bits(3.0f), bits(-0.0f)) != unchangedLogical) return 3;
    b.logical[1] = bits(0.0);
    if (classify(a, b, bits(2.0f), bits(-0.0f)) != changedLogical || changes(a, b) != (sprite | logical)) return 4;
    b = a; b.outer = 1; b.stamp = 2; b.dirty = 3; b.groupHash = 4;
    if (changes(a, b) != (sections | history | dirty | membership)) return 5;
    b = a; b.rotation[1] = 1; b.offsets[0] = 1; b.visibility = 1;
    if (changes(a, b) != (rotation | offsets | visibility)) return 6;
    a.logical[0] = bits(0.1);
    if (matchesInput(a, bits(0.1f), bits(-0.0f))) return 7;
    // A narrowed float input is exactly the expected sprite coordinate.
    if (!matchesRoundedLogical(a, bits(0.1f), bits(-0.0f))) return 8;
    if (matchesRoundedLogical(a, bits(0.1f), bits(0.0f))) return 9;
    if (!shadowEligible(a, 4, bits(0.1f), bits(-0.0f))) return 10;
    for (unsigned mask : {1u, 2u, 8u, 16u, 5u, 6u})
        if (shadowEligible(a, mask, bits(0.1f), bits(-0.0f))) return 11;
    a.offsets[0] = bits(1.0f);
    if (shadowEligible(a, 4, bits(0.1f), bits(-0.0f))) return 12;
    a.offsets[0] = 0; a.sprite = {bits(0.1f), bits(-0.0f)}; b = a;
    if (!shadowSetterEquivalent(a, b, bits(0.1f), bits(-0.0f))) return 13;
    b.logical[0] = bits(0.2);
    if (shadowSetterEquivalent(a, b, bits(0.1f), bits(-0.0f))) return 14;
    b = a; b.sprite[0] = bits(0.2f);
    if (shadowSetterEquivalent(a, b, bits(0.1f), bits(-0.0f))) return 15;
    b = a; b.visibility = 1;
    if (shadowSetterEquivalent(a, b, bits(0.1f), bits(-0.0f))) return 16;
    std::cout << "setter audit: raw bits, signed zero, narrowing, logical vs sprite and side effects OK\n";
}
