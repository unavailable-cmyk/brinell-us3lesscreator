#pragma once
#include "VisibilityCensus.hpp"
#include <algorithm>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace vulkanizer::v2 {
using VisibilitySection = std::pair<int, int>;
inline constexpr unsigned invalidVisibilitySpace = std::numeric_limits<unsigned>::max();

// Section zero and the last section include clamped coordinates. Expand by
// one section for FP rounding and native cell-boundary conventions.
inline int visibilitySectionCoordinate(double value, float factor, double limit) {
    if (value <= 0) return 0;
    // Match the GPU sectionIndex: the upper clamp multiplies float*float.
    if (value >= limit) return static_cast<int>(static_cast<float>(limit) * factor);
    return static_cast<int>(value * factor);
}
struct VisibilitySectionRange {
    int x0 = 0, x1 = std::numeric_limits<int>::max();
    int y0 = 0, y1 = std::numeric_limits<int>::max();
    bool all = true;
    bool contains(VisibilitySection s) const {
        return all || s.first < 0 || s.second < 0 ||
            (s.first >= x0 && s.first <= x1 && s.second >= y0 && s.second <= y1);
    }
};
inline VisibilitySectionRange visibilitySectionRange(CensusBounds view, double margin,
        float fx, float fy, double limit) {
    if (!(fx >= 0 && fy >= 0 && (fx > 0 || fy > 0)) || !std::isfinite(fx) || !std::isfinite(fy) ||
        double(static_cast<float>(limit) * fx) >= std::numeric_limits<int>::max() - 1.0 ||
        double(static_cast<float>(limit) * fy) >= std::numeric_limits<int>::max() - 1.0 ||
        !std::isfinite(margin) || margin < 0 || !std::isfinite(view.left) ||
        !std::isfinite(view.right) || !std::isfinite(view.bottom) || !std::isfinite(view.top) ||
        view.left >= view.right || view.bottom >= view.top) return {};
    return {std::max(0, visibilitySectionCoordinate(view.left - margin, fx, limit) - 1),
        fx > 0 ? visibilitySectionCoordinate(view.right + margin, fx, limit) + 1 : std::numeric_limits<int>::max(),
        std::max(0, visibilitySectionCoordinate(view.bottom - margin, fy, limit) - 1),
        fy > 0 ? visibilitySectionCoordinate(view.top + margin, fy, limit) + 1 : std::numeric_limits<int>::max(), false};
}

// Only deferred objects are indexed. Both the authoritative current section
// and the old mirror's section are tracked, so a camera move cannot expose a
// stale position. Queries walk occupied bins in the active range, not objects
// in distant bins. The caller merges IDs into the existing ordered bitset.
class VisibilitySectionFilter {
    struct Pending {
        unsigned space = invalidVisibilitySpace;
        VisibilitySection current{}, old{};
        std::size_t currentSlot = 0, oldSlot = 0;
    };
    struct Member { std::uint32_t object; bool old; };
    using Bin = std::vector<Member>;
    using Columns = std::map<int, std::map<int, Bin>>;
    std::vector<Columns> bins;
    std::vector<Pending> pending;
    std::size_t count = 0;
    std::size_t insert(unsigned space, VisibilitySection s, std::uint32_t id, bool old) {
        auto& members = bins[space][s.first][s.second];
        const auto slot = members.size();
        members.push_back({id,old});
        return slot;
    }
    void remove(unsigned space, VisibilitySection s, std::size_t slot) {
        auto& columns = bins[space];
        auto x = columns.find(s.first);
        if (x == columns.end()) return;
        auto y = x->second.find(s.second);
        if (y == x->second.end()) return;
        auto& members = y->second;
        const auto moved = members.back();
        members[slot] = moved;
        auto& p = pending[moved.object];
        (moved.old ? p.oldSlot : p.currentSlot) = slot;
        members.pop_back();
        if (members.empty()) x->second.erase(y);
        if (x->second.empty()) columns.erase(x);
    }
public:
    void initialize(std::size_t objects) { pending.assign(objects, {}); bins.clear(); count = 0; }
    unsigned addSpace() { bins.emplace_back(); return static_cast<unsigned>(bins.size() - 1); }
    bool indexed(std::uint32_t id) const { return id < pending.size() && pending[id].space != invalidVisibilitySpace; }
    std::size_t size() const { return count; }
    bool erase(std::uint32_t id) {
        if (!indexed(id)) return false;
        auto& p = pending[id];
        remove(p.space, p.current, p.currentSlot);
        if (p.old != p.current) remove(p.space, p.old, p.oldSlot);
        p = {}; --count;
        return true;
    }
    bool defer(std::uint32_t id, unsigned space, VisibilitySection current, VisibilitySection old) {
        auto& p = pending[id];
        if (p.space == space && p.current == current && p.old == old) return false;
        const bool fresh = !indexed(id);
        erase(id);
        p = {space, current, old};
        p.currentSlot = insert(space, current, id, false);
        if (old != current) p.oldSlot = insert(space, old, id, true);
        ++count;
        return fresh;
    }
    void move(std::uint32_t id, VisibilitySection current) {
        if (!indexed(id)) return;
        const auto p = pending[id];
        defer(id, p.space, current, p.old);
    }
    template<class F> void collect(unsigned space, VisibilitySectionRange range, F&& f) const {
        const auto& columns = bins[space];
        auto x = range.all ? columns.begin() : columns.lower_bound(range.x0);
        for (; x != columns.end() && (range.all || x->first <= range.x1); ++x) {
            auto y = range.all ? x->second.begin() : x->second.lower_bound(range.y0);
            for (; y != x->second.end() && (range.all || y->first <= range.y1); ++y)
                for (auto member : y->second) f(member.object);
        }
    }
};
}
