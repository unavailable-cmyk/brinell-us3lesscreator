#include "Scene.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace vulkanizer::v2 {
namespace {
std::atomic<std::uint64_t> nextGeneration{1};
bool finite(Translation p) {
    constexpr double limit = std::numeric_limits<float>::max();
    return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= limit && std::abs(p.y) <= limit;
}
}

Scene Scene::compile(std::span<const Decoration> decorations) {
    if (decorations.size() > std::numeric_limits<std::uint32_t>::max() / 6)
        throw std::length_error("V2 geometry exceeds addressable vertex range");
    Scene result;
    result.m_generation = nextGeneration.fetch_add(1);
    std::map<std::pair<std::vector<GroupId>, std::uint32_t>, ChainId> canonical;
    result.m_objects.reserve(decorations.size());
    result.m_vertices.reserve(decorations.size() * 6);
    for (const auto& input : decorations) {
        if (!finite(input.position)) throw std::invalid_argument("Invalid V2 base position");
        Object object;
        object.base = input.position;
        object.nativeTransform = input.nativeTransform;
        object.nativeTransform.position = input.position;
        object.groups = input.groups;
        std::sort(object.groups.begin(), object.groups.end());
        object.groups.erase(std::unique(object.groups.begin(), object.groups.end()), object.groups.end());
        object.material = input.material;
        object.visible = object.initialVisible = input.visible;
        // Path B's representation is reserved. Until its executor exists its
        // candidates retain the native owner, including native drawing.
        if (input.dependency == Dependency::decorativeTranslation) {
            object.path = ExecutionPath::sharedChain;
            auto [found, added] = canonical.try_emplace(std::make_pair(object.groups, input.chainPartition), static_cast<ChainId>(result.m_chains.size()));
            if (added) result.m_chains.emplace_back();
            object.chain = found->second;
            ++result.m_chains[object.chain].consumers;
            object.firstVertex = static_cast<std::uint32_t>(result.m_vertices.size());
            for (auto index : {0, 1, 2, 2, 1, 3}) {
                const auto& corner = input.quad[index];
                if (!std::isfinite(corner.x) || !std::isfinite(corner.y) || !std::isfinite(corner.z) ||
                    !std::isfinite(corner.u) || !std::isfinite(corner.v))
                    throw std::invalid_argument("Invalid V2 vertex");
                result.m_vertices.push_back({corner.x, corner.y, corner.z, corner.u, corner.v,
                    corner.color, static_cast<float>(object.chain % paletteSize),
                    static_cast<ObjectId>(result.m_objects.size())});
            }
        }
        result.m_objects.push_back(std::move(object));
    }
    result.m_palette.resize(result.m_chains.size());
    result.m_dirty.reserve(result.m_chains.size());
    result.m_updates.reserve(result.m_chains.size());
    result.rebuildRoutes();
    return result;
}

const Scene::Object& Scene::checked(Handle h) const {
    if (h.scene != m_generation || h.object >= m_objects.size() || !m_objects[h.object].alive)
        throw std::out_of_range("Stale V2 object handle");
    return m_objects[h.object];
}
Scene::Object& Scene::checked(Handle h) {
    return const_cast<Object&>(static_cast<const Scene&>(*this).checked(h));
}
Handle Scene::handle(ObjectId id) const {
    Handle h{m_generation, id};
    checked(h);
    return h;
}
ExecutionPath Scene::path(Handle h) const { return checked(h).path; }
NativeTransform Scene::nativeTransform(Handle h) const {
    auto result = checked(h).nativeTransform;
    result.position = position(h);
    return result;
}
Draw Scene::drawFor(Handle h) const {
    const auto& object = checked(h);
    return {object.path, object.material, object.chain / paletteSize, object.firstVertex,
        object.path == ExecutionPath::sharedChain ? 6u : 0u, h.object};
}
ChainId Scene::chainFor(Handle h) const { return checked(h).chain; }
Translation Scene::chainTranslation(ChainId id) const { return m_chains.at(id).translation; }
Translation Scene::position(Handle h) const {
    const auto& object = checked(h);
    if (object.path != ExecutionPath::sharedChain)
        throw std::logic_error("Native owner must supply authoritative position");
    const auto translation = m_chains[object.chain].translation;
    return {object.base.x + translation.x, object.base.y + translation.y};
}
void Scene::rebuildRoutes() {
    m_routes.clear();
    for (ObjectId id = 0; id < m_objects.size(); ++id) {
        const auto& object = m_objects[id];
        for (auto group : object.groups) {
            auto& route = m_routes[group];
            if (object.path == ExecutionPath::sharedChain) route.chains.push_back(object.chain);
            else route.native.push_back(id);
        }
    }
    for (auto& [group, route] : m_routes) {
        std::sort(route.chains.begin(), route.chains.end());
        route.chains.erase(std::unique(route.chains.begin(), route.chains.end()), route.chains.end());
    }
}
void Scene::dirty(ChainId id) {
    auto& chain = m_chains[id];
    if (!chain.dirty) { chain.dirty = true; m_dirty.push_back(id); }
}
void Scene::translate(GroupId group, Translation delta, std::uint64_t order, NativeSink& sink) {
    if (!finite(delta) || (m_hasOrder && order <= m_lastOrder))
        throw std::invalid_argument("Invalid V2 translation or native action order");
    auto found = m_routes.find(group);
    if (found != m_routes.end()) {
        auto& route = found->second;
        // Validate the entire shared operation before native or shared mutation.
        for (auto id : route.chains) {
            const auto previous = m_chains[id].translation;
            if (!finite({previous.x + delta.x, previous.y + delta.y}))
                throw std::overflow_error("V2 translation exceeds renderable range");
        }
        for (auto id : route.chains) {
            auto& p = m_chains[id].translation;
            p.x += delta.x;
            p.y += delta.y;
            dirty(id);
            ++m_counters.chainEvaluations;
        }
        if (!route.native.empty()) sink.translate(route.native, delta, order);
        m_counters.nativeVisits += route.native.size();
    }
    m_lastOrder = order;
    m_hasOrder = true;
    ++m_counters.actions;
}
std::span<const PaletteUpdate> Scene::updates() {
    m_updates.clear();
    for (auto id : m_dirty) {
        const auto p = m_chains[id].translation;
        m_palette[id] = {static_cast<float>(p.x), static_cast<float>(p.y), 0, 0};
        m_updates.push_back({id, m_palette[id]});
    }
    return m_updates;
}
void Scene::acknowledgeUpdates() {
    m_counters.paletteUpdates += m_dirty.size();
    for (auto id : m_dirty) m_chains[id].dirty = false;
    m_dirty.clear();
    m_updates.clear();
}
std::span<const Draw> Scene::draws() {
    if (!m_drawsDirty) return m_draws;
    m_draws.clear();
    for (ObjectId id = 0; id < m_objects.size(); ++id) {
        const auto& object = m_objects[id];
        ++m_counters.drawListObjectVisits;
        if (!object.alive || !object.visible) continue;
        if (object.path != ExecutionPath::sharedChain) {
            m_draws.push_back({object.path, object.material, 0, 0, 0, id});
            continue;
        }
        const auto page = object.chain / paletteSize;
        if (!m_draws.empty()) {
            auto& previous = m_draws.back();
            if (previous.path == ExecutionPath::sharedChain && previous.material == object.material &&
                previous.page == page && previous.firstVertex + previous.vertexCount == object.firstVertex) {
                previous.vertexCount += 6;
                continue;
            }
        }
        m_draws.push_back({object.path, object.material, page, object.firstVertex, 6, 0});
    }
    m_drawsDirty = false;
    return m_draws;
}
void Scene::acquireNative(Handle h, NativeSink& sink) {
    const auto& object = checked(h);
    if (object.path != ExecutionPath::nativeCpu) promote(h, object.groups, sink);
}
void Scene::acquireNativeBatch(std::span<const Handle> handles, NativeSink& sink) {
    std::vector<ObjectId> ids;
    ids.reserve(handles.size());
    for (auto h : handles) {
        if (checked(h).path == ExecutionPath::sharedChain) ids.push_back(h.object);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::unordered_map<GroupId, std::size_t> additions;
    for (auto id : ids) {
        const auto& object = m_objects[id];
        for (auto group : object.groups) ++additions[group];
    }
    for (const auto& [group, count] : additions) {
        auto& native = m_routes.at(group).native;
        native.reserve(native.size() + count);
    }
    // All potentially throwing allocation precedes the first ownership commit.
    for (auto id : ids) {
        auto& object = m_objects[id];
        sink.materialize(id, position(handle(id)), object.visible);
        object.path = ExecutionPath::nativeCpu;
        --m_chains[object.chain].consumers;
        for (auto group : object.groups) {
            auto& route = m_routes.at(group);
            route.native.insert(std::lower_bound(route.native.begin(), route.native.end(), id), id);
            if (!m_chains[object.chain].consumers) std::erase(route.chains, object.chain);
        }
    }
    m_drawsDirty = true;
}
void Scene::setGroups(Handle h, std::span<const GroupId> groups, NativeSink& sink) {
    checked(h);
    std::vector<GroupId> canonical(groups.begin(), groups.end());
    std::sort(canonical.begin(), canonical.end());
    canonical.erase(std::unique(canonical.begin(), canonical.end()), canonical.end());
    promote(h, std::move(canonical), sink);
}
void Scene::promote(Handle h, std::vector<GroupId> groups, NativeSink& sink) {
    auto& object = checked(h);
    // Prepare every allocation before materializing native state. An allocation
    // failure leaves the original ownership and reverse mappings intact.
    auto routes = m_routes;
    for (auto group : object.groups) {
        auto& route = routes.at(group);
        if (object.path == ExecutionPath::nativeCpu) std::erase(route.native, h.object);
        else if (m_chains[object.chain].consumers == 1) std::erase(route.chains, object.chain);
    }
    for (auto group : groups) {
        auto& ids = routes[group].native;
        ids.insert(std::lower_bound(ids.begin(), ids.end(), h.object), h.object);
    }
    if (object.path == ExecutionPath::sharedChain) {
        sink.materialize(h.object, position(h), object.visible);
        --m_chains[object.chain].consumers;
    }
    object.path = ExecutionPath::nativeCpu;
    object.groups.swap(groups);
    m_routes.swap(routes);
    m_drawsDirty = true;
}
void Scene::setVisible(Handle h, bool visible, NativeSink& sink) {
    auto& object = checked(h);
    if (object.visible == visible) return;
    object.visible = visible;
    if (object.path == ExecutionPath::nativeCpu) sink.visibility(h.object, visible);
    m_drawsDirty = true;
}
void Scene::retire(Handle h, NativeSink& sink) {
    promote(h, {}, sink);
    auto& object = checked(h);
    object.alive = false;
    object.visible = false;
}
void Scene::reset(NativeSink& sink) {
    for (ChainId id = 0; id < m_chains.size(); ++id) {
        m_chains[id].translation = {};
        dirty(id);
    }
    for (ObjectId id = 0; id < m_objects.size(); ++id) {
        auto& object = m_objects[id];
        if (!object.alive) continue;
        object.visible = object.initialVisible;
        if (object.path == ExecutionPath::nativeCpu) sink.reset(id, object.base, object.visible);
    }
    m_hasOrder = false;
    m_drawsDirty = true;
}
}
