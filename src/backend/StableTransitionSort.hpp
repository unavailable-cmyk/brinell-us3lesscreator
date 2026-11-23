#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace vulkanizer {
// Stable lexicographic ordering by action and original array ordinal. Persistent
// scratch storage avoids per-frame allocation; small batches retain std::sort.
template<class T> void sortSectionTransitions(std::vector<T>& values, std::vector<T>& scratch) {
    if (values.size() < 4096) {
        std::sort(values.begin(), values.end(), [](auto const& a, auto const& b) {
            return a.action < b.action || (a.action == b.action && a.order < b.order);
        });
        return;
    }
    scratch.resize(values.size());
    const auto firstKey = (static_cast<std::uint64_t>(values.front().action) << 32) | values.front().order;
    std::uint64_t varyingBits = 0;
    for (auto const& value : values)
        varyingBits |= firstKey ^ ((static_cast<std::uint64_t>(value.action) << 32) | value.order);
    for (unsigned pass = 0; pass < 8; ++pass) {
        if (((varyingBits >> (pass * 8)) & 255) == 0) continue;
        std::array<std::size_t, 256> counts{};
        const auto digit = [pass](T const& value) {
            const auto key = (static_cast<std::uint64_t>(value.action) << 32) | value.order;
            return static_cast<unsigned>((key >> (pass * 8)) & 255);
        };
        for (auto const& value : values) ++counts[digit(value)];
        std::size_t offset = 0;
        for (auto& count : counts) { const auto size = count; count = offset; offset += size; }
        for (auto const& value : values) scratch[counts[digit(value)]++] = value;
        values.swap(scratch);
    }
}
}
