#pragma once
#include "Scene.hpp"
#include <algorithm>
#include <map>

namespace vulkanizer::v2 {
struct AtlasRange {
    std::uint32_t first = 0, count = 0;
    Draw draw{};
    bool visible = true;
};
// Only V2 intervals are stored. Their complement is submitted to the original
// atlas in contiguous native ranges. Visibility never invalidates a scene list.
class AtlasPlan {
public:
    void insert(std::uint32_t slot, Draw draw, bool visible = true) {
        release(slot);
        auto current = m_ranges.emplace(slot, AtlasRange{slot, 1, draw, visible}).first;
        if (current != m_ranges.begin()) {
            auto previous = std::prev(current);
            if (adjacent(previous->second, current->second)) {
                previous->second.count += current->second.count;
                previous->second.draw.vertexCount += current->second.draw.vertexCount;
                m_ranges.erase(current);
                current = previous;
            }
        }
        auto next = std::next(current);
        if (next != m_ranges.end() && adjacent(current->second, next->second)) {
            current->second.count += next->second.count;
            current->second.draw.vertexCount += next->second.draw.vertexCount;
            m_ranges.erase(next);
        }
    }
    bool setVisible(std::uint32_t slot, bool visible) {
        auto found = m_ranges.upper_bound(slot);
        if (found == m_ranges.begin()) return false;
        --found;
        const auto range = found->second;
        if (slot >= range.first + range.count) return false;
        if (range.visible == visible) return true;
        auto draw = range.draw;
        draw.firstVertex += (slot - range.first) * 6;
        draw.vertexCount = 6;
        insert(slot, draw, visible);
        return true;
    }
    void release(std::uint32_t slot) {
        auto found = m_ranges.upper_bound(slot);
        if (found == m_ranges.begin()) return;
        --found;
        const auto range = found->second;
        const auto end = range.first + range.count;
        if (slot >= end) return;
        // Allocate a possible right split before altering the existing range.
        if (slot + 1 < end) {
            auto right = range;
            right.first = slot + 1;
            right.count = end - right.first;
            right.draw.firstVertex += (right.first - range.first) * 6;
            right.draw.vertexCount = right.count * 6;
            m_ranges.emplace(right.first, right);
        }
        if (slot > range.first) {
            found->second.count = slot - range.first;
            found->second.draw.vertexCount = found->second.count * 6;
        } else m_ranges.erase(found);
    }
    template<class Native, class Shared>
    void submit(std::uint32_t first, std::uint32_t count, Native native, Shared shared) {
        const auto end = static_cast<std::uint64_t>(first) + count;
        auto cursor = static_cast<std::uint64_t>(first);
        auto found = m_ranges.upper_bound(first);
        if (found != m_ranges.begin()) --found;
        for (; found != m_ranges.end() && found->first < end; ++found) {
            const auto& range = found->second;
            if (range.first + range.count <= first) continue;
            const auto begin = std::max<std::uint64_t>(first, range.first);
            const auto finish = std::min<std::uint64_t>(end, static_cast<std::uint64_t>(range.first) + range.count);
            if (begin > cursor) native(static_cast<std::uint32_t>(begin - cursor), static_cast<std::uint32_t>(cursor));
            auto draw = range.draw;
            draw.firstVertex += static_cast<std::uint32_t>(begin - range.first) * 6;
            draw.vertexCount = static_cast<std::uint32_t>(finish - begin) * 6;
            if (range.visible) shared(draw);
            cursor = finish;
        }
        if (cursor < end) native(static_cast<std::uint32_t>(end - cursor), static_cast<std::uint32_t>(cursor));
    }
private:
    static bool adjacent(const AtlasRange& a, const AtlasRange& b) {
        return a.visible == b.visible && a.first + a.count == b.first && a.draw.page == b.draw.page && a.draw.material == b.draw.material &&
            a.draw.firstVertex + a.draw.vertexCount == b.draw.firstVertex;
    }
    std::map<std::uint32_t, AtlasRange> m_ranges;
};
}
