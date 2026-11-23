#pragma once
#include "Scene.hpp"
#include "backend/TriggerTransformTypes.hpp"
#include <functional>
#include <memory>

namespace vulkanizer::v2 {
struct RenderSink {
    virtual ~RenderSink() = default;
    // Complete native material state is established at each ordered range.
    // Shared ranges support the standard texture * vertex-color shader.
    virtual void bindMaterial(MaterialId) = 0;
    virtual void drawNative(ObjectId) = 0;
};
struct RenderCounters {
    std::uint64_t initialUploadBytes = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t paletteUploadBytes = 0;
    std::uint64_t drawUniformUploadBytes = 0;
    std::uint64_t drawCalls = 0;
    std::uint64_t dispatches = 0;
    std::uint64_t residentSubmissions = 0;
    std::uint64_t residentCommandBytes = 0;
    std::uint64_t residentMovedDrawCalls = 0;
    std::uint64_t residentGeometryBytes = 0;
    std::uint64_t residentValidationReadbackBytes = 0;
    std::uint64_t membershipChains = 0;
    std::uint64_t membershipGroupIncidences = 0;
    std::uint64_t maxGroupsPerMembershipChain = 0;
    std::uint64_t membershipBlocks = 0;
    double residentUploadMs = 0;
    double residentValidationWaitMs = 0;
};
struct ResidentObject {
    Translation base;
    std::span<const GroupId> groups;
    float rotationX = 0, rotationY = 0;
    std::int32_t sectionX = -1, sectionY = -1;
    bool canRotate = false;
};
struct ResidentValidationState {
    Translation position;
    float rotationX = 0, rotationY = 0;
};
// Desktop adapter, requiring GL 3.3. The same vec4 palette layout and vertex
// operations can be expressed with GLES 3.0 UBOs and GLSL ES 300 on mobile.
// Owns one context's resources. Destroy before that context is released.
// The caller owns GL state during render and must restore/invalidate its cache
// before returning to an external renderer such as Cocos.
class GlRenderer {
public:
    static constexpr std::uint32_t defaultReplayCapacity = 256;
    static constexpr std::uint32_t temporalReplayCapacity = 2048;
    using Resolve = std::function<void*(const char*)>;
    explicit GlRenderer(Resolve);
    ~GlRenderer();
    GlRenderer(const GlRenderer&) = delete;
    GlRenderer& operator=(const GlRenderer&) = delete;
    void attach(Scene&);
    // Groups are dense zero-based routes. Empty object membership denotes a
    // native recipient, which is never nominated by the compute passes.
    void attachResident(Scene&, std::span<const ResidentObject>, std::size_t groupCount,
        std::uint32_t commandCapacity = defaultReplayCapacity);
    void dispatchResident(std::span<const gpu::ResidentTranslation>, std::span<const ObjectId> retired,
        std::vector<gpu::ResidentSectionEvent>* events = nullptr, float sectionX = 0, float sectionY = 0);
    void updateResidentQuad(const Draw&, const std::array<Corner, 4>&, Translation origin,
        float rotationX = 0, float rotationY = 0);
    void updateResidentSection(ObjectId, int x, int y);
    std::vector<ResidentValidationState> validateResidentPositions();
    void render(Scene&, const std::array<float, 16>& mvp, RenderSink&);
    // Live atlas integration supplies resolved contiguous ranges. Native ranges
    // are submitted by the host atlas, preserving its existing batching.
    void renderRanges(Scene&, std::span<const Draw>, const std::array<float, 16>& mvp, RenderSink&);
    void abandonContext();
    void clearCounters();
    const RenderCounters& counters() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
