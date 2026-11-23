#pragma once

#include "LiveScene.hpp"
#include "VisibilitySectionFilter.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vulkanizer::v2 {

// Shadow-only sparse section index. Each section owns a compact ObjectId list;
// object slots make removal a swap with the last member, without scanning it.
class VisibilityOwnershipIndex {
    struct SectionHash {
        std::size_t operator()(VisibilitySection section) const noexcept {
            const auto x = std::hash<int>{}(section.first);
            const auto y = std::hash<int>{}(section.second);
            return x ^ (y + std::size_t{0x9e3779b9} + (x << 6) + (x >> 2));
        }
    };
    struct Slot {
        unsigned space = invalidVisibilitySpace;
        VisibilitySection section{};
        std::size_t slot = 0;
    };
    using Bin = std::vector<std::uint32_t>;
    using Space = std::unordered_map<VisibilitySection, Bin, SectionHash>;
    std::vector<Space> spaces;
    std::vector<Slot> objects;
    std::size_t members = 0;

    void eraseFromBin(std::uint32_t id) {
        auto& slot = objects[id];
        if (slot.space == invalidVisibilitySpace || slot.space >= spaces.size()) return;
        auto bin = spaces[slot.space].find(slot.section);
        if (bin == spaces[slot.space].end() || slot.slot >= bin->second.size()) return;
        auto& ids = bin->second;
        const auto moved = ids.back();
        ids[slot.slot] = moved;
        objects[moved].slot = slot.slot;
        ids.pop_back();
        if (ids.empty()) spaces[slot.space].erase(bin);
        slot = {};
        --members;
    }

public:
    void initialize(std::size_t objectCount, std::size_t spaceCount) {
        objects.assign(objectCount, {});
        spaces.clear();
        spaces.resize(spaceCount);
        members = 0;
    }

    bool indexed(std::uint32_t id) const {
        return id < objects.size() && objects[id].space != invalidVisibilitySpace;
    }
    std::size_t size() const { return members; }
    std::size_t spaceCount() const { return spaces.size(); }
    unsigned objectSpace(std::uint32_t id) const {
        return indexed(id) ? objects[id].space : invalidVisibilitySpace;
    }
    VisibilitySection objectSection(std::uint32_t id) const {
        return indexed(id) ? objects[id].section : VisibilitySection{-1, -1};
    }

    // Average O(1) section lookup/insertion, O(1) indexed swap removal.
    bool set(std::uint32_t id, unsigned space, VisibilitySection section) {
        if (id >= objects.size()) return false;
        if (space == invalidVisibilitySpace || section.first < 0 || section.second < 0) {
            const bool had = indexed(id);
            if (had) eraseFromBin(id);
            return had;
        }
        if (indexed(id) && objects[id].space == space && objects[id].section == section) return false;
        if (indexed(id)) eraseFromBin(id);
        if (space >= spaces.size()) spaces.resize(static_cast<std::size_t>(space) + 1);
        auto& ids = spaces[space][section];
        objects[id] = {space, section, ids.size()};
        ids.push_back(id);
        ++members;
        return true;
    }

    bool erase(std::uint32_t id) {
        if (!indexed(id)) return false;
        eraseFromBin(id);
        return true;
    }

    template<class F>
    void collect(unsigned space, VisibilitySectionRange range, F&& f) const {
        if (space >= spaces.size()) return;
        for (const auto& [section, ids] : spaces[space]) {
            if (!range.contains(section)) continue;
            for (const auto id : ids) f(id);
        }
    }

    std::size_t occupiedSections() const {
        std::size_t count = 0;
        for (const auto& space : spaces) count += space.size();
        return count;
    }

    std::size_t maxSectionPopulation() const {
        std::size_t maximum = 0;
        for (const auto& space : spaces)
            for (const auto& [section, ids] : space) maximum = std::max(maximum, ids.size());
        return maximum;
    }
};

} // namespace vulkanizer::v2

namespace vulkanizer::v2::live {
bool visibilityOwnershipShadowEnabled();
void recordVisibilityOwnershipNativeVisibility(::GameObject* object, bool visible);
}
