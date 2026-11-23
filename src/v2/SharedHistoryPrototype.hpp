#pragma once
#include "Scene.hpp"
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace vulkanizer::v2 {
// One stream per cohort of identical native routes and initial command stamps.
// Replay is deferred until native state is required. Adding a combined offset
// to the base would round differently from native's ordered double additions.
class SharedHistoryPrototype {
public:
    struct RouteEvidence {
        std::vector<GroupId> orderedRoutes;
        std::uint32_t initialStamp = 0;
        bool skipHistory = false;
        bool operator==(const RouteEvidence&) const = default;
    };
    explicit SharedHistoryPrototype(std::span<const RouteEvidence> evidence) {
        if (evidence.empty()) throw std::invalid_argument("Empty history cohort");
        for (const auto& item : evidence)
            if (!(item == evidence.front())) throw std::invalid_argument("History route evidence differs");
        m_evidence = evidence.front();
        m_stamp = m_evidence.initialStamp;
    }
    explicit SharedHistoryPrototype(RouteEvidence evidence)
        : m_evidence(std::move(evidence)), m_stamp(m_evidence.initialStamp) {}
    struct NativeState {
        Translation position;
        Translation previous;
        std::uint32_t stamp;
        bool invalidateRect = false;
    };
    void move(GroupId route, Translation delta, std::uint64_t order, std::uint32_t commandStamp) {
        if (m_hasOrder && order <= m_order) throw std::invalid_argument("History action order");
        bool known = false;
        for (auto member : m_evidence.orderedRoutes) known |= member == route;
        if (!known) throw std::invalid_argument("Unknown native route");
        const bool save = !m_evidence.skipHistory && commandStamp != m_stamp;
        m_operations.push_back({delta, save});
        if (save) {
            m_history = m_position;
            m_stamp = commandStamp;
            m_hasHistory = true;
        }
        m_position.x += delta.x;
        m_position.y += delta.y;
        m_order = order;
        m_hasOrder = true;
    }
    Translation positionOffset() const { return m_position; }
    Translation historyOffset() const { return m_history; }
    std::uint32_t stamp() const { return m_stamp; }
    bool hasHistory() const { return m_hasHistory; }
    NativeState materialize(Translation base, Translation initialPrevious) const {
        NativeState state{base, initialPrevious, m_stamp, m_hasHistory};
        for (const auto& operation : m_operations) {
            if (operation.savePrevious)
                state.previous = {static_cast<float>(state.position.x), static_cast<float>(state.position.y)};
            if (operation.delta.x != 0) state.position.x += operation.delta.x;
            if (operation.delta.y != 0) state.position.y += operation.delta.y;
        }
        return state;
    }
private:
    struct Operation { Translation delta; bool savePrevious; };
    RouteEvidence m_evidence;
    std::vector<Operation> m_operations;
    Translation m_position, m_history;
    std::uint64_t m_order = 0;
    std::uint32_t m_stamp = 0;
    bool m_hasOrder = false, m_hasHistory = false;
};
}
