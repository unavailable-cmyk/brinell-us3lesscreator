#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace vulkanizer::v2 {

using GroupId = std::uint32_t;
using ObjectId = std::uint32_t;
using ChainId = std::uint32_t;
using MaterialId = std::uint32_t;
constexpr std::uint32_t paletteSize = 256;

enum class ExecutionPath { sharedChain, independentGpu, nativeCpu };
enum class Dependency { decorativeTranslation, independentTransform, authoritativeCpu };
struct Handle {
    std::uint64_t scene = 0;
    ObjectId object = 0;
};
struct Translation { double x = 0, y = 0; };
struct NativeTransform {
    Translation position;
    float rotationX = 0, rotationY = 0;
    float scaleX = 1, scaleY = 1;
    float skewX = 0, skewY = 0;
    float anchorX = 0, anchorY = 0;
    bool flipX = false, flipY = false;
};
struct Corner {
    float x = 0, y = 0, z = 0;
    float u = 0, v = 0;
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};
};
// Input order is the resolved native drawing order. Material IDs identify the
// complete texture/blend/shader/clipping state in the owning renderer adapter.
struct Decoration {
    Translation position;
    NativeTransform nativeTransform;
    std::array<Corner, 4> quad;
    std::vector<GroupId> groups;
    // Adapter-defined compilation partition. It does not change native routes.
    std::uint32_t chainPartition = 0;
    MaterialId material = 0;
    Dependency dependency = Dependency::decorativeTranslation;
    bool visible = true;
};
struct Vertex {
    float x, y, z, u, v;
    std::array<std::uint8_t, 4> color;
    float chain;
    ObjectId object;
};
struct Draw {
    ExecutionPath path;
    MaterialId material;
    std::uint32_t page;
    std::uint32_t firstVertex;
    std::uint32_t vertexCount;
    ObjectId nativeObject;
};
struct alignas(16) PaletteEntry { float x = 0, y = 0, reserved0 = 0, reserved1 = 0; };
struct PaletteUpdate { ChainId chain; PaletteEntry value; };
struct Counters {
    std::uint64_t actions = 0;
    std::uint64_t chainEvaluations = 0;
    std::uint64_t paletteUpdates = 0;
    std::uint64_t nativeVisits = 0;
    std::uint64_t drawListObjectVisits = 0;
};

// The game adapter owns native state. Calls occur in the original action order.
// It must materialize before exposing a newly dependent object to any CPU read.
// These synchronous callbacks must complete without throwing or re-entering Scene.
struct NativeSink {
    virtual ~NativeSink() = default;
    virtual void translate(std::span<const ObjectId>, Translation, std::uint64_t order) noexcept = 0;
    virtual void materialize(ObjectId, Translation, bool visible) noexcept = 0;
    virtual void visibility(ObjectId, bool visible) noexcept = 0;
    virtual void reset(ObjectId, Translation, bool visible) noexcept = 0;
};

class Scene {
public:
    // Compilation is transactional. A failed compilation cannot modify a live scene.
    static Scene compile(std::span<const Decoration> objects);
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    std::uint64_t generation() const { return m_generation; }
    std::size_t objectCount() const { return m_objects.size(); }
    std::size_t chainCount() const { return m_chains.size(); }
    Handle handle(ObjectId id) const;
    ExecutionPath path(Handle) const;
    Translation position(Handle) const;
    NativeTransform nativeTransform(Handle) const;
    Draw drawFor(Handle) const;
    ChainId chainFor(Handle) const;
    Translation chainTranslation(ChainId) const;
    std::span<const Vertex> geometry() const { return m_vertices; }
    std::span<const Draw> draws();
    std::span<const PaletteEntry> palette() const { return m_palette; }
    std::span<const PaletteUpdate> updates();
    std::size_t dirtyCount() const { return m_dirty.size(); }
    void acknowledgeUpdates();
    const Counters& counters() const { return m_counters; }
    void clearCounters() { m_counters = {}; }

    // order is a native activation/action sequence, independent of group IDs.
    // Rejects stale/out-of-order/non-finite operations before either owner runs.
    void translate(GroupId, Translation, std::uint64_t order, NativeSink&);
    void setVisible(Handle, bool, NativeSink&);
    void acquireNative(Handle, NativeSink&);
    void acquireNativeBatch(std::span<const Handle>, NativeSink&);
    // Membership mutation promotes the affected object before native code
    // changes its dependencies. Other members keep their compiled shared chain.
    void setGroups(Handle, std::span<const GroupId>, NativeSink&);
    // Complete before the adapter releases the native object or its material.
    void retire(Handle, NativeSink&);
    // Reset preserves dependency promotions. Recompilation creates a new lifetime.
    void reset(NativeSink&);

private:
    Scene() = default;
    struct Object {
        Translation base;
        NativeTransform nativeTransform;
        std::vector<GroupId> groups;
        ChainId chain = 0;
        MaterialId material = 0;
        std::uint32_t firstVertex = 0;
        ExecutionPath path = ExecutionPath::nativeCpu;
        bool visible = true, initialVisible = true;
        bool alive = true;
    };
    struct Chain {
        Translation translation;
        std::uint32_t consumers = 0;
        bool dirty = false;
    };
    struct Route {
        std::vector<ChainId> chains;
        std::vector<ObjectId> native;
    };
    Object& checked(Handle);
    const Object& checked(Handle) const;
    void rebuildRoutes();
    void promote(Handle, std::vector<GroupId>, NativeSink&);
    void dirty(ChainId);
    std::uint64_t m_generation = 0, m_lastOrder = 0;
    bool m_hasOrder = false, m_drawsDirty = true;
    std::vector<Object> m_objects;
    std::vector<Chain> m_chains;
    std::vector<Vertex> m_vertices;
    std::vector<Draw> m_draws;
    std::vector<PaletteEntry> m_palette;
    std::vector<ChainId> m_dirty;
    std::vector<PaletteUpdate> m_updates;
    std::unordered_map<GroupId, Route> m_routes;
    Counters m_counters;
};
}
