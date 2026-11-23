#pragma once
#include <cstdint>
#include <cstddef>
#include <span>

namespace vulkanizer::v2::live::caller_audit {
// Heuristic only: a stack word in executable memory preceded by an x64
// near CALL is a candidate return PC, not proof of a stack frame.
inline bool callEndsAt(std::span<const std::uint8_t> bytes) {
    const auto n = bytes.size();
    if (n >= 5 && bytes[n - 5] == 0xe8) return true;
    for (std::size_t i = 0; i + 2 <= n; ++i) {
        if (bytes[i] != 0xff || (bytes[i + 1] & 0x38) != 0x10) continue;
        const auto mod = bytes[i + 1] >> 6;
        const auto rm = bytes[i + 1] & 7;
        auto end = i + 2;
        unsigned base = rm;
        if (mod != 3 && rm == 4) {
            if (end >= n) continue;
            base = bytes[end++] & 7;
        }
        if (mod == 1) ++end;
        else if (mod == 2 || (mod == 0 && base == 5)) end += 4;
        if (end == n) return true;
    }
    return false;
}
}
