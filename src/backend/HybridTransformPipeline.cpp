#include "HybridTransformPipeline.hpp"

#include "HybridTransformExecutor.hpp"
#include "TransformStateParity.hpp"
#include "RotationScheduler.hpp"
#include "core/RendererManager.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/EffectGameObject.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <tuple>
#include <cstdlib>
#include <windows.h>
#include <GL/gl.h>

using namespace geode::prelude;

namespace vulkanizer {
namespace {
using Clock = std::chrono::steady_clock;
using gpu::Action;
using gpu::HybridBatchMetrics;
using gpu::ObjectState;
using gpu::Span;
// Verify field interpretation against the installed 2.2091 disassembly. A
// different layout must fail compilation rather than silently corrupt state.
static_assert(offsetof(GameObject, m_positionX) == 0x3b8);
static_assert(offsetof(GameObject, m_lastPosition) == 0x4d0);
static_assert(offsetof(GameObject, m_unk4C4) == 0x4dc);
static_assert(offsetof(GameObject, m_isDecoration2) == 0x512);
static_assert(offsetof(GameObject, m_isObjectPosDirty) == 0x351);
static_assert(offsetof(GameObject, m_isObjectRectDirty) == 0x368);

struct GroupAudit {
    GJBaseGameLayer* layer = nullptr;
    int group = 0;
    bool checked = false;
    bool safe = false;
    unsigned objectCount = 0;
    std::string reason;
};

struct StepTotals {
    std::uint64_t batches = 0;
    std::uint64_t readbacks = 0;
    std::uint64_t moveObjectsReplaced = 0;
    std::uint64_t rotateObjectsReplaced = 0;
    std::uint64_t referenceObjects = 0;
    std::uint64_t fallbackBatches = 0;
    std::uint64_t budgetFallbackBatches = 0;
    std::uint64_t referenceMatches = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t bufferAllocations = 0;
    double cpuPrepareMs = 0;
    double referenceCheckMs = 0;
    double uploadMs = 0;
    double dispatchSubmitMs = 0;
    double gpuComputeMs = 0;
    double syncWaitMs = 0;
    double transferMs = 0;
    double cpuApplyMs = 0;
    double totalMs = 0;
};

constexpr std::uint32_t maximumAllObjectBatch = 1'000'000;
constexpr std::uint32_t minimumAllObjectBatch = 64;
// The hybrid path synchronizes and reads back after every helper batch. Keep
// the all-object experiment bounded to one such round trip per simulation step;
// the rest of that step is handled by GD's original CPU implementation.
constexpr unsigned maximumAllObjectGpuBatchesPerStep = 1;

GroupAudit s_groupAudit{};
StepTotals s_stepTotals{};
StepTotals s_intervalTotals{};
unsigned s_intervalSteps = 0;
unsigned s_gpuBatchesThisStep = 0;
unsigned s_gpuBatchesThisFrame = 0;
bool s_stepActive = false;
bool s_moveReferenceChecked = false;
bool s_rotateReferenceChecked = false;
bool s_moveDisabled = false;
bool s_rotateDisabled = false;
bool s_runtimeErrorLogged = false;
bool s_groupReadyLogged = false;
bool s_allObjectModeLogged = false;
enum class RotationProbe { none, vanilla, gpu, cpu, gpuUncached };
RotationProbe s_rotationProbe = RotationProbe::none;
std::vector<unsigned> s_probeCallbacks;
int s_rotationParity = 0;
std::uint64_t s_cpuRotationObjects = 0;
double s_cpuRotationMs = 0;
bool s_liveRotationGpuVerified = false;
std::uint64_t s_cpuRotationBatches = 0, s_rotationRejectedBatches = 0;
std::uint64_t s_rotationRejectedObjects = 0;
std::uint64_t s_totalSimulationSteps = 0, s_totalCpuRotationObjects = 0, s_totalRejectedRotationObjects = 0;
HybridBatchMetrics s_probeGpuMetrics{};
bool validateRotationAgainstGD(GJBaseGameLayer* layer);
bool rotationArrayEligible(CCArray* objects) {
    for (auto* object : CCArrayExt<GameObject, false>(objects)) {
        if (!object || object->m_objectType != GameObjectType::Decoration ||
            !object->m_isDecoration2 || object->m_isTrigger || object->m_hasGroupParent ||
            object->m_hasAreaParent || object->m_parentMode != 0) return false;
    }
    return true;
}

bool cpuRotationArrayEligible(CCArray* objects, bool explain = false) {
    // The CPU path evaluates and applies one object at a time in native order.
    // Its state parity includes both solid and decoration objects; it does not
    // require the stricter independent-object rule used by GPU readback.
    for (auto* object : CCArrayExt<GameObject, false>(objects)) {
        if (!object || (object->m_objectType != GameObjectType::Decoration &&
                        object->m_objectType != GameObjectType::Solid) ||
            object->m_isTrigger || object->m_isStartPos || object->m_hasGroupParent ||
            object->m_hasAreaParent || object->m_parentMode != 0) {
            static unsigned explanations = 0;
            if (explain && explanations < 8) {
                ++explanations;
                if (object)
                    log::info("VULKANIZER_ROTATION_REJECT object_id={} type={} trigger={} start_pos={} group_parent={} area_parent={} parent_mode={}; batch_objects={}",
                        object->m_objectID, static_cast<int>(object->m_objectType), object->m_isTrigger,
                        object->m_isStartPos, object->m_hasGroupParent, object->m_hasAreaParent,
                        object->m_parentMode, objects->count());
                else log::info("VULKANIZER_ROTATION_REJECT null object; batch_objects={}", objects->count());
            }
            return false;
        }
    }
    return true;
}
class RotationProbeLayer : public GJBaseGameLayer {
public:
    void updateObjectSection(GameObject*) override { recordHybridParityCallback(false); }
    void updateDisabledObjectsLastPos(CCArray*) override { recordHybridParityCallback(true); }
};

GJBaseGameLayer* rotationProbeLayer() {
    static auto* layer = [] {
        auto* value = new RotationProbeLayer();
        value->cocos2d::CCLayer::init();
        value->m_objectLayer = cocos2d::CCLayer::create();
        value->addChild(value->m_objectLayer);
        auto makeNode = [&] {
            auto* node = cocos2d::CCNode::create();
            value->m_objectLayer->addChild(node);
            return node;
        };
        value->m_areaTransformNode = makeNode();
        value->m_areaSkewNode = makeNode();
        value->m_areaScaleNode = makeNode();
        value->m_areaRotateNode = makeNode();
        value->m_areaTransformNode2 = makeNode();
        value->m_gameState.m_commandIndex = 7;
        return value;
    }();
    return layer;
}

double elapsedMs(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool sameBits(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}
bool sameBits(float left, float right) {
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}

std::array<float, 2> rotationTranslation(float x, float y, CCAffineTransform const& matrix) {
    // Preserve the MULSS/ADDSS rounding sequence in cocos affine concat.
    volatile float x0 = matrix.a * x, x1 = matrix.c * y;
    volatile float y0 = matrix.b * x, y1 = matrix.d * y;
    const float tx = (x1 + x0) + matrix.tx;
    const float ty = (y1 + y0) + matrix.ty;
    return {tx, ty};
}

bool memberOfOnlyGroup(GameObject* object, int group) {
    return object && object->m_groups && object->m_groupCount == 1 &&
        (*object->m_groups)[0] == group;
}

bool simpleDecoration(GameObject* object, int group, std::string& reason) {
    if (!object) { reason = "null object"; return false; }
    if (!memberOfOnlyGroup(object, group)) { reason = "object has mixed or changing group membership"; return false; }
    if (!object->m_isDecoration2 || object->m_objectType != GameObjectType::Decoration) {
        reason = "object is not positively identified as a decoration";
        return false;
    }
    if (object->m_isTrigger || object->m_isStartPos || object->m_isDisabled ||
        object->m_isGroupDisabled || object->m_isGroupDisabledTemp) {
        reason = "trigger, start, or disabled object";
        return false;
    }
    if (object->m_hasGroupParent || object->m_hasAreaParent || object->m_linkedGroup != 0 ||
        object->m_parentMode != 0 || object->m_useObjectRect) {
        reason = "object has a group, area, parent, or custom-rect dependency";
        return false;
    }
    return true;
}

bool auditPilotGroup(GJBaseGameLayer* layer, int group, std::string& reason) {
    auto* members = layer->getGroup(group);
    if (!members || members->count() == 0) {
        reason = "selected group has no loaded objects";
        return false;
    }
    if (members->count() > 512) {
        reason = "pilot group exceeds the 512-object prototype limit";
        return false;
    }
    for (unsigned i = 0; i < members->count(); ++i) {
        auto* object = static_cast<GameObject*>(members->objectAtIndex(i));
        if (!simpleDecoration(object, group, reason)) return false;
    }

    // A selected decoration may be an ordinary move/rotation target, but it
    // cannot serve as a center, dynamic target, remap anchor, or dynamic-mode
    // input. Those relationships keep their entire group on vanilla CPU.
    auto* allObjects = layer->m_objects;
    if (!allObjects) {
        reason = "level object index is unavailable for dependency audit";
        return false;
    }
    for (unsigned i = 0; i < allObjects->count(); ++i) {
        auto* object = static_cast<GameObject*>(allObjects->objectAtIndex(i));
        if (!object || !object->m_isTrigger) continue;
        auto* effect = static_cast<EffectGameObject*>(object);
        if (effect->m_centerGroupID == group || effect->m_targetModCenterID == group ||
            effect->m_rotationTargetID == group) {
            reason = "selected group is referenced as a trigger center or dynamic target";
            return false;
        }
        if (effect->m_targetGroupID == group &&
            (effect->m_isDynamicMode || effect->m_useMoveTarget || effect->m_isDynamicBlock)) {
            reason = "selected group is targeted by a dynamic trigger";
            return false;
        }
    }
    return true;
}

bool ensureGroupAudit(GJBaseGameLayer* layer, int group) {
    if (s_groupAudit.layer != layer || s_groupAudit.group != group) {
        s_groupAudit = {layer, group, false, false, 0, {}};
    }
    if (!s_groupAudit.checked) {
        s_groupAudit.checked = true;
        s_groupAudit.safe = auditPilotGroup(layer, group, s_groupAudit.reason);
        if (s_groupAudit.safe) {
            auto* members = layer->getGroup(group);
            s_groupAudit.objectCount = members ? members->count() : 0;
            log::info("GPU transform pilot group audit passed: group={} objects={} class=decoration-only dynamic-center-target=none",
                group, s_groupAudit.objectCount);
        } else {
            log::warn("GPU transform pilot group audit rejected group={}: {}; vanilla processing remains active",
                group, s_groupAudit.reason);
        }
    }
    return s_groupAudit.safe;
}

bool transformArray(GJBaseGameLayer* layer, CCArray* objects, int group, bool allObjects,
                    std::vector<GameObject*>& output, std::string& reason) {
    if (!objects || objects->count() == 0) {
        reason = "empty object batch";
        return false;
    }
    const auto count = objects->count();
    if (allObjects ? count > maximumAllObjectBatch : count > 512) {
        reason = "object batch exceeds configured safe size";
        return false;
    }
    if (!allObjects && !ensureGroupAudit(layer, group)) {
        reason = s_groupAudit.reason;
        return false;
    }
    output.clear();
    output.reserve(objects->count());
    for (unsigned i = 0; i < objects->count(); ++i) {
        auto* object = static_cast<GameObject*>(objects->objectAtIndex(i));
        if (!allObjects && !simpleDecoration(object, group, reason)) return false;
        if (allObjects && !object) {
            reason = "null object in GD transform batch";
            return false;
        }
        output.push_back(object);
    }
    // Reusable scratch avoids one allocation/free per object on every helper.
    // Sorting a copy keeps the authoritative callback order unchanged.
    thread_local std::vector<GameObject*> uniquenessScratch;
    uniquenessScratch.assign(output.begin(), output.end());
    std::sort(uniquenessScratch.begin(), uniquenessScratch.end(), std::less<GameObject*>{});
    if (std::adjacent_find(uniquenessScratch.begin(), uniquenessScratch.end()) != uniquenessScratch.end()) {
        reason = "batch contains the same object more than once";
        return false;
    }
    return true;
}

void addMetrics(HybridBatchMetrics const& metrics, bool isMove, std::uint64_t count,
                double applyMs, double totalMs, double referenceCheckMs,
                bool referenceMatched) {
    auto& target = s_stepActive ? s_stepTotals : s_intervalTotals;
    ++target.batches;
    ++target.readbacks;
    if (referenceMatched) target.referenceObjects += count;
    else if (isMove) target.moveObjectsReplaced += count;
    else target.rotateObjectsReplaced += count;
    target.bytesRead += metrics.readbackBytes;
    target.bufferAllocations += metrics.bufferAllocations;
    target.cpuPrepareMs += metrics.cpuPrepareMs;
    target.referenceCheckMs += referenceCheckMs;
    target.uploadMs += metrics.uploadMs;
    target.dispatchSubmitMs += metrics.dispatchSubmitMs;
    target.gpuComputeMs += metrics.gpuComputeMs;
    target.syncWaitMs += metrics.syncWaitMs;
    target.transferMs += metrics.transferMs;
    target.cpuApplyMs += applyMs;
    target.totalMs += totalMs;
    if (referenceMatched) ++target.referenceMatches;
}

void addFallback() {
    auto& target = s_stepActive ? s_stepTotals : s_intervalTotals;
    ++target.fallbackBatches;
}

bool reserveGpuBatch(bool allObjects) {
    if (!allObjects) return true;
    if (!s_stepActive || s_gpuBatchesThisFrame >= 1 || s_gpuBatchesThisStep >= maximumAllObjectGpuBatchesPerStep) {
        auto& target = s_stepActive ? s_stepTotals : s_intervalTotals;
        ++target.fallbackBatches;
        ++target.budgetFallbackBatches;
        return false;
    }
    ++s_gpuBatchesThisStep;
    ++s_gpuBatchesThisFrame;
    return true;
}

bool gpuBudgetExhausted(bool allObjects) {
    if (!allObjects || (s_stepActive && s_gpuBatchesThisFrame < 1 && s_gpuBatchesThisStep < maximumAllObjectGpuBatchesPerStep))
        return false;
    auto& target = s_stepActive ? s_stepTotals : s_intervalTotals;
    ++target.fallbackBatches;
    ++target.budgetFallbackBatches;
    return true;
}

void logComparisonSample(char const* operation, std::size_t objectCount,
                         HybridBatchMetrics const& metrics, double referenceMathAndCheckMs,
                         double cpuApplyMs, double totalPipelineMs) {
    const double estimatedVanillaBodyMs = referenceMathAndCheckMs + cpuApplyMs;
    const double steadyHybridPipelineMs = std::max(0.0, totalPipelineMs - referenceMathAndCheckMs);
    log::info("VULKANIZER_GPU_TRANSFORM_COMPARISON schema=1 operation={} objects={} action_entries={} batches=1 readbacks=1 cpu_reference_math_and_check_ms={:.3f} cpu_state_application_callbacks_ms={:.3f} estimated_vanilla_body_plus_callbacks_ms={:.3f} hybrid_pipeline_steady_ms={:.3f} estimated_cpu_delta_ms={:.3f} cpu_prepare_ms={:.3f} gpu_compute_ms={:.3f} sync_wait_ms={:.3f} readback_transfer_ms={:.3f} note=first_verified_batch;includes_hybrid_prepare_and_group_audit;GPU_compute_overlaps_sync_wait",
        operation, objectCount, objectCount, referenceMathAndCheckMs, cpuApplyMs,
        estimatedVanillaBodyMs, steadyHybridPipelineMs,
        estimatedVanillaBodyMs - steadyHybridPipelineMs, metrics.cpuPrepareMs,
        metrics.gpuComputeMs, metrics.syncWaitMs, metrics.transferMs);
}

void logRuntimeFailure(std::string const& error) {
    // A failed fence can leave compute work in flight on the executor's shared
    // buffers. Stop both transform paths immediately; all later calls use GD's
    // original CPU helpers for the rest of this process.
    s_moveDisabled = true;
    s_rotateDisabled = true;
    if (s_runtimeErrorLogged) return;
    s_runtimeErrorLogged = true;
    log::warn("GPU transform executor disabled for the rest of this process; vanilla move/rotation paths remain active: {}", error);
}

bool finiteState(ObjectState const& state) {
    return std::isfinite(state.x) && std::isfinite(state.y) && std::isfinite(state.rotation);
}

void appendMoveInput(std::vector<ObjectState>& states, std::vector<Span>& spans,
                     std::vector<std::uint32_t>& indices, double x, double y, bool lockX) {
    const auto index = static_cast<std::uint32_t>(indices.size());
    states.push_back({x, y, 0.f, 0.f});
    spans.push_back({index, 1});
    indices.push_back(lockX ? 1u : 0u);
}

bool ensureGpuRegressionChecks() {
    static bool checked = false;
    static bool passed = false;
    if (checked) return passed;
    checked = true;
    // Exercise actual shader execution with mixed X locks, rounded dispatches,
    // and shrinking live counts inside the same reusable allocation. This does
    // not modify any GD object and is excluded from gameplay throughput totals.
    for (std::uint32_t count : {129u, 65u, 17u}) {
        std::vector<ObjectState> states;
        std::vector<Action> actions(2);
        std::vector<Span> spans;
        std::vector<std::uint32_t> indices;
        actions[0].dx = 3.25;
        actions[0].dy = -2.5;
        actions[1] = actions[0];
        actions[1].flags = 4u;
        for (std::uint32_t i = 0; i < count; ++i) {
            appendMoveInput(states, spans, indices, static_cast<double>(i),
                            -static_cast<double>(i), i % 2 != 0);
        }
        HybridBatchMetrics metrics;
        std::string error;
        if (!gpu::HybridTransformExecutor::get().evaluate(states, actions, spans, indices, metrics, error)) {
            logRuntimeFailure(error);
            return false;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            const double x = static_cast<double>(i) + (i % 2 ? 0.0 : 3.25);
            const double y = -static_cast<double>(i) - 2.5;
            if (!sameBits(states[i].x, x) || !sameBits(states[i].y, y)) {
                logRuntimeFailure("mixed-lock / shrinking-buffer GPU regression failed");
                return false;
            }
        }
    }
    passed = true;
    log::info("Hybrid GPU regression passed: mixed X-lock flags; live counts 129/65/17; shared-buffer growth/shrink; bit-exact results; schema=6");
    return true;
}

struct ObjectLayerRestore {
    cocos2d::CCNode* layer = nullptr;
    cocos2d::CCPoint position{};
    float scaleX = 1.f;
    float scaleY = 1.f;
    ~ObjectLayerRestore() {
        if (!layer) return;
        layer->setScaleX(scaleX);
        layer->setScaleY(scaleY);
        layer->setPosition(position);
    }
};

bool resolveVanillaRotationTransform(GJBaseGameLayer* layer, float rotation,
                                     cocos2d::CCPoint center,
                                     cocos2d::CCAffineTransform& transform,
                                     ObjectLayerRestore& restore) {
    if (!layer->m_objectLayer || !layer->m_areaTransformNode || !layer->m_areaSkewNode ||
        !layer->m_areaScaleNode || !layer->m_areaTransformNode2) return false;
    restore.layer = layer->m_objectLayer;
    restore.position = restore.layer->getPosition();
    restore.scaleX = restore.layer->getScaleX();
    restore.scaleY = restore.layer->getScaleY();

    restore.layer->setScaleX(1.f);
    restore.layer->setScaleY(1.f);
    restore.layer->setPosition(cocos2d::CCPointZero);

    layer->m_areaTransformNode->setPosition(center);
    layer->m_areaTransformNode->setScaleX(1.f);
    layer->m_areaTransformNode->setScaleY(1.f);
    layer->m_areaScaleNode->setScaleX(1.f);
    layer->m_areaScaleNode->setScaleY(1.f);
    layer->m_areaTransformNode->setSkewX(0.f);
    layer->m_areaTransformNode->setSkewY(0.f);
    layer->m_areaSkewNode->setSkewX(0.f);
    layer->m_areaSkewNode->setSkewY(0.f);
    layer->m_areaTransformNode->setRotation(rotation);
    layer->prepareTransformParent(false);

    transform = layer->m_areaTransformNode->nodeToWorldTransform();
    layer->m_areaTransformNode2->setScaleX(1.f);
    layer->m_areaTransformNode2->setScaleY(1.f);
    layer->m_areaTransformNode2->setSkewX(0.f);
    layer->m_areaTransformNode2->setSkewY(0.f);
    layer->m_areaTransformNode2->setRotation(0.f);
    return std::isfinite(transform.a) && std::isfinite(transform.b) &&
        std::isfinite(transform.c) && std::isfinite(transform.d) &&
        std::isfinite(transform.tx) && std::isfinite(transform.ty);
}

} // namespace

bool hybridParityProbeActive() { return s_rotationProbe != RotationProbe::none; }
void recordSchedulerPositionVisits(std::uint64_t visits) { s_totalCpuRotationObjects += visits; }
void recordHybridParityCallback(bool finalizer) { s_probeCallbacks.push_back(finalizer ? 1u : 0u); }

void invalidateHybridGroupAudit(GJBaseGameLayer* layer) {
    if (!layer || s_groupAudit.layer == layer) s_groupAudit.checked = false;
}

void beginHybridTransformStep() {
    auto& manager = RendererManager::get();
    if (!manager.gpuTransformRequested()) return;
    ++s_totalSimulationSteps;
    s_stepTotals = {};
    s_gpuBatchesThisStep = 0;
    s_stepActive = true;
}

void beginHybridTransformFrame() {
    s_gpuBatchesThisFrame = 0;
    static const bool performanceOnly = std::getenv("VULKANIZER_V2_PERFORMANCE_ONLY") != nullptr;
    if (performanceOnly) return;
    if (!RendererManager::get().gpuTransformRequested()) return;
    // One clock read per rendered frame; no callback hooks or per-object work.
    // This remains available when detailed profiling/PresentMon is unavailable.
    static auto last = Clock::now(), window = last;
    static auto* scene = PlayLayer::get();
    static std::uint64_t frames = 0, over100 = 0;
    static double totalMs = 0, maxMs = 0;
    static auto previousSteps = s_totalSimulationSteps;
    static auto previousOptimized = s_totalCpuRotationObjects;
    static auto previousRejected = s_totalRejectedRotationObjects;
    const auto now = Clock::now();
    auto* currentScene = PlayLayer::get();
    if (scene != currentScene) {
        scene = currentScene;
        window = now;
        frames = over100 = 0;
        totalMs = maxMs = 0;
        last = now;
        previousSteps = s_totalSimulationSteps;
        previousOptimized = s_totalCpuRotationObjects;
        previousRejected = s_totalRejectedRotationObjects;
        return;
    }
    const auto interval = elapsedMs(last, now);
    last = now;
    ++frames;
    totalMs += interval;
    maxMs = std::max(maxMs, interval);
    if (interval > 100.0) ++over100;
    if (elapsedMs(window, now) >= 2000.0) {
        log::info("VULKANIZER_FRAME_LIGHT scene={} frames={} mean_ms={:.3f} max_ms={:.3f} over_100ms={} simulation_steps={} optimized_rotation_visits={} rejected_rotation_visits={} interval=drawScene_start_to_start",
            scene ? "gameplay" : "menu", frames, totalMs / frames, maxMs, over100,
            s_totalSimulationSteps - previousSteps, s_totalCpuRotationObjects - previousOptimized,
            s_totalRejectedRotationObjects - previousRejected);
        previousSteps = s_totalSimulationSteps;
        previousOptimized = s_totalCpuRotationObjects;
        previousRejected = s_totalRejectedRotationObjects;
        window = now;
        frames = over100 = 0;
        totalMs = maxMs = 0;
    }
}

void endHybridTransformStep() {
    if (!s_stepActive) return;
    s_stepActive = false;
    auto const& value = s_stepTotals;
    s_intervalTotals.batches += value.batches;
    s_intervalTotals.readbacks += value.readbacks;
    s_intervalTotals.moveObjectsReplaced += value.moveObjectsReplaced;
    s_intervalTotals.rotateObjectsReplaced += value.rotateObjectsReplaced;
    s_intervalTotals.referenceObjects += value.referenceObjects;
    s_intervalTotals.fallbackBatches += value.fallbackBatches;
    s_intervalTotals.budgetFallbackBatches += value.budgetFallbackBatches;
    s_intervalTotals.referenceMatches += value.referenceMatches;
    s_intervalTotals.bytesRead += value.bytesRead;
    s_intervalTotals.bufferAllocations += value.bufferAllocations;
    s_intervalTotals.cpuPrepareMs += value.cpuPrepareMs;
    s_intervalTotals.referenceCheckMs += value.referenceCheckMs;
    s_intervalTotals.uploadMs += value.uploadMs;
    s_intervalTotals.dispatchSubmitMs += value.dispatchSubmitMs;
    s_intervalTotals.gpuComputeMs += value.gpuComputeMs;
    s_intervalTotals.syncWaitMs += value.syncWaitMs;
    s_intervalTotals.transferMs += value.transferMs;
    s_intervalTotals.cpuApplyMs += value.cpuApplyMs;
    s_intervalTotals.totalMs += value.totalMs;
    if (++s_intervalSteps < 120) return;
    log::info("VULKANIZER_TRANSFORM_STATUS cpu_rotation_batches={} cpu_rotation_objects={} rejected_rotation_batches={} rejected_rotation_objects={} live_gpu_rotation_applied={} disabled={} scope=standalone_helper; actual_scheduler_coverage=VULKANIZER_SCHEDULER_LIVE",
        s_cpuRotationBatches, s_cpuRotationObjects, s_rotationRejectedBatches,
        s_rotationRejectedObjects, s_liveRotationGpuVerified, s_rotateDisabled);
    s_cpuRotationBatches = s_rotationRejectedBatches = 0;
    s_rotationRejectedObjects = 0;
    if (s_cpuRotationObjects) {
        log::info("VULKANIZER_ROTATION_CPU_FAST objects={} total_ms={:.3f} removed=per_object_Cocos_node_reposition_and_world_transform callbacks=vanilla_order",
            s_cpuRotationObjects, s_cpuRotationMs);
        s_cpuRotationObjects = 0;
        s_cpuRotationMs = 0;
    }
    if (s_intervalTotals.batches || s_intervalTotals.fallbackBatches) {
        log::info("VULKANIZER_GPU_TRANSFORM_HYBRID schema=6 steps={} gpu_batches={} readbacks={} move_position_math_replaced={} rotation_position_math_replaced={} reference_checks_passed={} reference_objects_cpu_checked={} fallback_batches={} budget_fallback_batches={} bytes_read={} buffer_allocations={} cpu_prepare_ms={:.3f} cpu_reference_check_ms={:.3f} upload_ms={:.3f} dispatch_submit_ms={:.3f} gpu_compute_ms={:.3f} sync_wait_ms={:.3f} transfer_ms={:.3f} cpu_state_apply_callbacks_ms={:.3f} total_pipeline_ms={:.3f}",
            s_intervalSteps, s_intervalTotals.batches, s_intervalTotals.readbacks,
            s_intervalTotals.moveObjectsReplaced, s_intervalTotals.rotateObjectsReplaced,
            s_intervalTotals.referenceMatches, s_intervalTotals.referenceObjects,
            s_intervalTotals.fallbackBatches, s_intervalTotals.budgetFallbackBatches,
            s_intervalTotals.bytesRead,
            s_intervalTotals.bufferAllocations,
            s_intervalTotals.cpuPrepareMs, s_intervalTotals.referenceCheckMs, s_intervalTotals.uploadMs,
            s_intervalTotals.dispatchSubmitMs, s_intervalTotals.gpuComputeMs,
            s_intervalTotals.syncWaitMs, s_intervalTotals.transferMs,
            s_intervalTotals.cpuApplyMs, s_intervalTotals.totalMs);
    }
    s_intervalTotals = {};
    s_intervalSteps = 0;
}

bool tryHybridMoveObjects(GJBaseGameLayer* layer, CCArray* objects,
                          double dx, double dy, bool lockPlayerY) {
    if (hybridParityProbeActive()) return false;
    auto& manager = RendererManager::get();
    const bool allObjects = manager.gpuTransformAllRequested();
    // Repeated synchronous readback loses against GD's already-simple move
    // body. Automatic mode keeps it native; the explicit pilot is retained.
    if (allObjects) return false;
    const int group = manager.gpuTransformPilotGroup();
    if (!manager.gpuTransformReady() || (!allObjects && group <= 0) || s_moveDisabled)
        return false;
    // Reject budget/size/no-op cases before copying or auditing a group.
    if (gpuBudgetExhausted(allObjects)) return false;
    if (dx == 0.0 && dy == 0.0) return false;
    if (allObjects && (!objects || objects->count() < minimumAllObjectBatch ||
                      objects->count() > maximumAllObjectBatch)) {
        addFallback();
        return false;
    }
    if (!ensureGpuRegressionChecks()) return false;
    if (allObjects && !s_allObjectModeLogged) {
        s_allObjectModeLogged = true;
        log::warn("All-object GPU move mode active with a cap of one synchronous GPU batch per rendered frame including all catch-up steps; remaining work uses vanilla; state-parity revision=7");
    }
    (void)lockPlayerY; // The 2.2091 moveObjects body does not read this argument.
    auto started = Clock::now();
    auto prepareStart = started;
    std::vector<GameObject*> targets;
    std::string reason;
    if (!transformArray(layer, objects, group, allObjects, targets, reason) ||
        (allObjects && targets.size() < minimumAllObjectBatch)) {
        if (objects && objects->count() > 0) addFallback();
        return false;
    }
    if (dx == 0.0 && dy == 0.0) return false;
    if (!reserveGpuBatch(allObjects)) return false;

    std::vector<ObjectState> states;
    std::vector<Action> actions;
    std::vector<Span> spans;
    std::vector<std::uint32_t> indices;
    states.reserve(targets.size()); actions.reserve(allObjects ? 2 : targets.size());
    spans.reserve(targets.size()); indices.reserve(targets.size());
    Action updateXY{};
    updateXY.kind = 0;
    updateXY.dx = dx;
    updateXY.dy = dy;
    Action updateY = updateXY;
    updateY.flags = 4u;
    actions.push_back(updateXY);
    actions.push_back(updateY);
    for (std::uint32_t i = 0; i < targets.size(); ++i) {
        auto* object = targets[i];
        appendMoveInput(states, spans, indices, object->m_positionX,
                        object->m_positionY, object->m_tempOffsetXRelated);
    }
    const double prepareMs = elapsedMs(prepareStart, Clock::now());

    HybridBatchMetrics gpuMetrics;
    std::string error;
    if (!gpu::HybridTransformExecutor::get().evaluate(states, actions, spans, indices, gpuMetrics, error)) {
        addFallback();
        logRuntimeFailure(error);
        return false;
    }
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (!finiteState(states[i])) { error = "non-finite GPU move output"; addFallback(); logRuntimeFailure(error); return false; }
    }

    bool referenceMatched = false;
    bool referenceCheckPerformed = false;
    double referenceCheckMs = 0;
    if (!s_moveReferenceChecked) {
        referenceCheckPerformed = true;
        auto referenceStart = Clock::now();
        referenceMatched = true;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            auto* object = targets[i];
            double expectedX = object->m_positionX;
            double expectedY = object->m_positionY;
            if (dx != 0.0 && !object->m_tempOffsetXRelated) expectedX = expectedX + dx;
            if (dy != 0.0) expectedY = expectedY + dy;
            if (!sameBits(expectedX, states[i].x) || !sameBits(expectedY, states[i].y)) {
                referenceMatched = false;
                break;
            }
        }
        referenceCheckMs = elapsedMs(referenceStart, Clock::now());
        s_moveReferenceChecked = true;
        if (!referenceMatched) {
            s_moveDisabled = true;
            addFallback();
            log::error("Hybrid GPU move reference check failed; move offload is disabled for this process and vanilla handled the batch");
            return false;
        }
        log::info("Hybrid GPU move reference check passed bit-for-bit for {} live GD objects", targets.size());
    }

    auto applyStart = Clock::now();
    layer->m_movedCount += static_cast<int>(objects->count());
    for (std::size_t i = 0; i < targets.size(); ++i) {
        auto* object = targets[i];
        // GD 2.2091 RVA 0x33696c..0x3369b7 performs this before position
        // arithmetic, once per command index, for non-decoration objects.
        preserveTransformHistory(*object, layer->m_gameState.m_commandIndex);
        object->m_positionX = states[i].x;
        object->m_positionY = states[i].y;
        object->dirtifyObjectPos();
        layer->updateObjectSection(object);
    }
    layer->updateDisabledObjectsLastPos(objects);
    const double applyMs = elapsedMs(applyStart, Clock::now());
    const double totalMs = elapsedMs(started, Clock::now());
    gpuMetrics.cpuPrepareMs = prepareMs;
    addMetrics(gpuMetrics, true, targets.size(), applyMs, totalMs, referenceCheckMs, referenceMatched);
    if (referenceCheckPerformed)
        logComparisonSample("move", targets.size(), gpuMetrics, referenceCheckMs, applyMs, totalMs);
    return true;
}

bool tryHybridRotateObjects(GJBaseGameLayer* layer, CCArray* objects,
                            float rotation, CCPoint position, CCPoint offset,
                            bool finished, bool lockObjectRotation) {
    auto& manager = RendererManager::get();
    if (s_rotationProbe == RotationProbe::vanilla) return false;
    const bool probing = hybridParityProbeActive();
    const bool allObjects = manager.gpuTransformAllRequested();
    const int group = manager.gpuTransformPilotGroup();
    if (!probing && (!manager.gpuTransformReady() || (!allObjects && group <= 0) || s_rotateDisabled))
        return false;
    if (!objects || objects->count() == 0 || objects->count() > maximumAllObjectBatch) {
        addFallback();
        return false;
    }
    if (!probing && allObjects) {
        // Start with plain decorations. Mixed gameplay arrays stay completely
        // on the original helper, with no deferred side effects.
        if (!cpuRotationArrayEligible(objects, true)) {
            ++s_rotationRejectedBatches;
            s_rotationRejectedObjects += objects->count();
            s_totalRejectedRotationObjects += objects->count();
            return false;
        }
        if (!validateRotationAgainstGD(layer)) return false;
        if (!s_allObjectModeLogged) {
            s_allObjectModeLogged = true;
            log::info("Automatic transform optimization: native movement; GPU sample restricted to independent decoration; CPU fast rotations support solid/decoration mixed arrays; revision=9");
        }
    }
    if (!ensureGpuRegressionChecks()) return false;
    auto started = Clock::now();
    auto prepareStart = started;
    const bool useGpuCandidate = probing ? (s_rotationProbe == RotationProbe::gpu || s_rotationProbe == RotationProbe::gpuUncached) :
        objects->count() >= minimumAllObjectBatch &&
        !s_liveRotationGpuVerified &&
        rotationArrayEligible(objects) &&
        (!allObjects || (s_stepActive && s_gpuBatchesThisFrame == 0));
    if (!useGpuCandidate && (allObjects || probing)) {
        // No transform staging, hashing, copying, or readback is needed on the
        // CPU path: reproduce the native per-object operation/callback order.
        ObjectLayerRestore restore;
        CCAffineTransform transform{};
        if (!resolveVanillaRotationTransform(layer, rotation, position, transform, restore)) return false;
        CCPoint lastRelative;
        for (auto* object : CCArrayExt<GameObject, false>(objects)) {
            const float baseX = static_cast<float>(object->m_positionX - object->m_positionXOffset);
            const float baseY = static_cast<float>(object->m_positionY - object->m_positionYOffset);
            const auto translated = rotationTranslation(baseX - position.x, baseY - position.y, transform);
            lastRelative.x = baseX - position.x;
            lastRelative.y = baseY - position.y;
            object->m_unk4fb = false;
            preserveTransformHistory(*object, layer->m_gameState.m_commandIndex);
            object->m_isDirty = true;
            object->m_isUnmodifiedPosDirty = true;
            object->m_positionX += static_cast<double>((translated[0] + offset.x) - baseX);
            object->m_positionY += static_cast<double>((translated[1] + offset.y) - baseY);
            layer->rotateObject(object, rotation);
            layer->updateObjectSection(object);
        }
        layer->m_areaTransformNode2->setPosition(lastRelative);
        layer->m_areaTransformNode2->nodeToWorldTransform();
        layer->updateDisabledObjectsLastPos(objects);
        if (!probing) {
            ++s_cpuRotationBatches;
            s_totalCpuRotationObjects += objects->count();
            s_cpuRotationObjects += objects->count();
            s_cpuRotationMs += elapsedMs(started, Clock::now());
        }
        return true;
    }
    std::vector<GameObject*> targets;
    std::string reason;
    if (!transformArray(layer, objects, group, allObjects || probing, targets, reason)) {
        if (objects && objects->count() > 0) addFallback();
        return false;
    }
    const bool useGpu = probing ? (s_rotationProbe == RotationProbe::gpu || s_rotationProbe == RotationProbe::gpuUncached) :
        targets.size() >= minimumAllObjectBatch &&
        !s_liveRotationGpuVerified &&
        rotationArrayEligible(objects) &&
        (!allObjects || (s_stepActive && s_gpuBatchesThisFrame == 0));
    if (useGpu && !probing && !reserveGpuBatch(allObjects)) return false;

    ObjectLayerRestore restore;
    CCAffineTransform transform{};
    if (!resolveVanillaRotationTransform(layer, rotation, position, transform, restore)) {
        addFallback();
        log::warn("Hybrid GPU rotation rejected a non-finite or unavailable Cocos transform; vanilla handled this batch");
        return false;
    }

    std::vector<ObjectState> states;
    std::vector<Action> actions;
    std::vector<Span> spans;
    std::vector<std::uint32_t> indices;
    states.reserve(targets.size());
    if (useGpu) {
        actions.reserve(targets.size());
        spans.reserve(targets.size()); indices.reserve(targets.size());
    }
    for (std::uint32_t i = 0; i < targets.size(); ++i) {
        auto* object = targets[i];
        states.push_back({object->m_positionX, object->m_positionY, 0.f, 0.f});
        if (!useGpu) continue;
        Action action{};
        action.kind = 1;
        action.flags = 2u; // resolved vanilla affine transform
        if (!object->m_canRotateFree) action.flags |= 1u;
        action.dx = static_cast<double>(offset.x);
        action.dy = static_cast<double>(offset.y);
        action.centerX = static_cast<double>(position.x);
        action.centerY = static_cast<double>(position.y);
        action.rotationDelta = rotation;
        action.matrixA = transform.a;
        action.matrixB = transform.b;
        action.matrixC = transform.c;
        action.matrixD = transform.d;
        action.matrixTx = transform.tx;
        action.matrixTy = transform.ty;
        action.objectOffsetX = object->m_positionXOffset;
        action.objectOffsetY = object->m_positionYOffset;
        actions.push_back(action);
        spans.push_back({i, 1});
        indices.push_back(i);
    }
    const double prepareMs = elapsedMs(prepareStart, Clock::now());

    HybridBatchMetrics gpuMetrics;
    std::string error;
    if (useGpu && !gpu::HybridTransformExecutor::get().evaluate(states, actions, spans, indices, gpuMetrics, error)) {
        addFallback();
        logRuntimeFailure(error);
        return false;
    }
    if (useGpu && probing) {
        s_probeGpuMetrics.uploadMs += gpuMetrics.uploadMs;
        s_probeGpuMetrics.dispatchSubmitMs += gpuMetrics.dispatchSubmitMs;
        s_probeGpuMetrics.gpuComputeMs += gpuMetrics.gpuComputeMs;
        s_probeGpuMetrics.syncWaitMs += gpuMetrics.syncWaitMs;
        s_probeGpuMetrics.transferMs += gpuMetrics.transferMs;
        s_probeGpuMetrics.uploadBytes += gpuMetrics.uploadBytes;
        s_probeGpuMetrics.cachedInputBuffers += gpuMetrics.cachedInputBuffers;
    }
    if (!useGpu) {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            auto* object = targets[i];
            const float baseX = static_cast<float>(object->m_positionX - object->m_positionXOffset);
            const float baseY = static_cast<float>(object->m_positionY - object->m_positionYOffset);
            const auto point = rotationTranslation(baseX - position.x, baseY - position.y, transform);
            states[i].x += static_cast<double>((point[0] + offset.x) - baseX);
            states[i].y += static_cast<double>((point[1] + offset.y) - baseY);
            states[i].rotation = object->m_canRotateFree ? rotation : 0.f;
        }
    }
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (!finiteState(states[i])) { error = "non-finite GPU rotation output"; addFallback(); logRuntimeFailure(error); return false; }
    }

    bool referenceMatched = false;
    bool referenceCheckPerformed = false;
    double referenceCheckMs = 0;
    if (useGpu && !s_rotateReferenceChecked) {
        referenceCheckPerformed = true;
        auto referenceStart = Clock::now();
        referenceMatched = true;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            auto* object = targets[i];
            CCPoint centerToObject(
                static_cast<float>(object->m_positionX - object->m_positionXOffset) - position.x,
                static_cast<float>(object->m_positionY - object->m_positionYOffset) - position.y);
            const CCAffineTransform translation{1.f, 0.f, 0.f, 1.f, centerToObject.x, centerToObject.y};
            const auto concatenated = cocos2d::CCAffineTransformConcat(translation, transform);
            const CCPoint rotated(concatenated.tx, concatenated.ty);
            double expectedX = object->m_positionX;
            double expectedY = object->m_positionY;
            const float baseX = static_cast<float>(object->m_positionX - object->m_positionXOffset);
            const float baseY = static_cast<float>(object->m_positionY - object->m_positionYOffset);
            expectedX += static_cast<double>((rotated.x + offset.x) - baseX);
            expectedY += static_cast<double>((rotated.y + offset.y) - baseY);
            const float expectedRotation = !object->m_canRotateFree ? 0.f : rotation;
            if (!sameBits(expectedX, states[i].x) || !sameBits(expectedY, states[i].y) ||
                !sameBits(expectedRotation, states[i].rotation)) {
                referenceMatched = false;
                break;
            }
        }
        referenceCheckMs = elapsedMs(referenceStart, Clock::now());
        s_rotateReferenceChecked = true;
        if (!referenceMatched) {
            s_rotateDisabled = true;
            addFallback();
            log::error("Hybrid GPU rotation reference check failed; rotation offload is disabled for this process and vanilla handled the batch");
            return false;
        }
        log::info("Hybrid GPU rotation reference check passed bit-for-bit for {} live GD objects", targets.size());
    }

    const auto* lastObject = targets.back();
    const CCPoint finalRelative(
        static_cast<float>(lastObject->m_positionX - lastObject->m_positionXOffset) - position.x,
        static_cast<float>(lastObject->m_positionY - lastObject->m_positionYOffset) - position.y);
    auto applyStart = Clock::now();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        auto* object = targets[i];
        object->m_unk4fb = false; // GD 2.2091 RVA 0x335071 clears this flag.
        preserveTransformHistory(*object, layer->m_gameState.m_commandIndex);
        object->m_isDirty = true;
        object->m_isUnmodifiedPosDirty = true;
        object->m_positionX = states[i].x;
        object->m_positionY = states[i].y;
        layer->rotateObject(object, rotation);
        layer->updateObjectSection(object);
    }
    layer->m_areaTransformNode2->setPosition(finalRelative);
    layer->m_areaTransformNode2->nodeToWorldTransform();
    layer->updateDisabledObjectsLastPos(objects);
    const double applyMs = elapsedMs(applyStart, Clock::now());
    const double totalMs = elapsedMs(started, Clock::now());
    gpuMetrics.cpuPrepareMs = prepareMs;
    if (!probing && useGpu)
        addMetrics(gpuMetrics, false, targets.size(), applyMs, totalMs, referenceCheckMs, referenceMatched);
    if (!probing && useGpu && !s_liveRotationGpuVerified) {
        s_liveRotationGpuVerified = true;
        log::info("VULKANIZER_ROTATION_LIVE_GPU objects={} wall_ms={:.4f} output_applied_to_GD=true; subsequent rotations use the CPU fast path to avoid repeated readback stalls",
            targets.size(), totalMs);
    }
    if (!probing && !useGpu) {
        s_cpuRotationObjects += targets.size();
        s_cpuRotationMs += totalMs;
    }
    if (referenceCheckPerformed)
        logComparisonSample("rotation", targets.size(), gpuMetrics, referenceCheckMs, applyMs, totalMs);
    (void)finished; // The 2.2091 rotateObjects implementation sets this flag true.
    (void)lockObjectRotation; // Unused by the installed 2.2091 rotateObjects body.
    return true;
}

} // namespace vulkanizer

namespace vulkanizer {
namespace {
auto rotationState(GameObject* object) {
    return std::make_tuple(std::bit_cast<std::uint64_t>(object->m_positionX),
        std::bit_cast<std::uint64_t>(object->m_positionY),
        std::bit_cast<std::uint32_t>(object->getRotationX()),
        std::bit_cast<std::uint32_t>(object->getRotationY()),
        std::bit_cast<std::uint32_t>(object->m_rotationXOffset),
        std::bit_cast<std::uint32_t>(object->m_rotationYOffset),
        object->m_lastPosition.x, object->m_lastPosition.y, object->m_unk4C4,
        object->m_isDirty, object->m_isObjectPosDirty, object->m_isUnmodifiedPosDirty,
        object->m_isObjectRectDirty, object->m_isOrientedBoxDirty, object->m_unk4fb);
}

bool validateRotationAgainstGD(GJBaseGameLayer* layer) {
    if (s_rotationParity) return s_rotationParity == 1;
    layer = rotationProbeLayer();
    s_rotationParity = -1;
    struct ProbeScope {
        GJBaseGameLayer* layer;
        int moved;
        ~ProbeScope() { s_rotationProbe = RotationProbe::none; layer->m_movedCount = moved; }
    } scope{layer, layer->m_movedCount};
    auto* control = CCArray::create();
    auto* candidate = CCArray::create();
    for (unsigned i = 0; i < 4; ++i) {
        auto* a = GameObject::createWithKey(i % 2 ? 1 : 1757);
        auto* b = GameObject::createWithKey(i % 2 ? 1 : 1757);
        if (!a || !b) return false;
        for (auto* object : {a, b}) {
            // Runtime level loading assigns these classification fields. The
            // fixture configures both classes explicitly instead of assuming
            // createWithKey alone has performed level-load classification.
            object->m_isDecoration2 = i % 2 == 0;
            object->m_objectType = i % 2 == 0 ? GameObjectType::Decoration : GameObjectType::Solid;
            object->m_isTrigger = false;
            object->m_isStartPos = false;
            object->m_hasGroupParent = false;
            object->m_hasAreaParent = false;
            object->m_parentMode = 0;
            object->m_positionX = 10000.125 + i * 0.123456;
            object->m_positionY = -50.75 + i * 2.25;
            object->m_positionXOffset = 3.25f;
            object->m_positionYOffset = -2.5f;
            object->m_canRotateFree = i < 2;
            object->m_unk4C4 = layer->m_gameState.m_commandIndex - 1;
            object->setRotation(13.25f);
        }
        control->addObject(a);
        candidate->addObject(b);
    }
    for (auto mode : {RotationProbe::gpu, RotationProbe::cpu}) {
        // Both arrays retain prior sequence state, so this also exercises
        // accumulated rotation and repeated transforms within one command.
        for (float angle : {27.5f, -63.25f, 0.f, 90.f}) {
            const CCPoint center(127.25f, -33.5f), offset(2.75f, -1.125f);
            s_rotationProbe = RotationProbe::vanilla;
            s_probeCallbacks.clear();
            layer->moveObjects(control, 1.25, -0.5, false);
            layer->rotateObjects(control, angle, center, offset, false, true);
            const auto expectedCallbacks = s_probeCallbacks;
            const auto expectedNodePosition = layer->m_areaTransformNode2->getPosition();
            s_probeCallbacks.clear();
            layer->moveObjects(candidate, 1.25, -0.5, false);
            s_rotationProbe = mode;
            if (!tryHybridRotateObjects(layer, candidate, angle, center, offset, false, true)) {
                log::warn("Rotation GD parity probe could not execute candidate; keeping vanilla");
                return false;
            }
            if (s_probeCallbacks != expectedCallbacks) {
                log::error("Rotation GD parity callback order mismatch; keeping vanilla");
                return false;
            }
            if (layer->m_areaTransformNode2->getPosition() != expectedNodePosition) {
                log::error("Rotation GD parity shared translation-node mismatch; keeping vanilla");
                return false;
            }
            for (unsigned i = 0; i < control->count(); ++i) {
                auto* a = static_cast<GameObject*>(control->objectAtIndex(i));
                auto* b = static_cast<GameObject*>(candidate->objectAtIndex(i));
                if (rotationState(a) != rotationState(b)) {
                    log::error("Rotation GD parity mismatch: mode={} object={} angle={} native=({:.12f},{:.12f}) candidate=({:.12f},{:.12f}); keeping vanilla",
                        mode == RotationProbe::gpu ? "GPU" : "CPU", i, angle,
                        a->m_positionX, a->m_positionY, b->m_positionX, b->m_positionY);
                    return false;
                }
            }
        }
    }
    s_rotationParity = 1;
    if (RendererManager::get().gpuTransformAllRequested()) {
        // Exercise the real automatic gate on the mixed fixture as well as
        // the forced GPU/CPU paths. Do not count fixture work as gameplay.
        const auto savedBatches = s_cpuRotationBatches;
        const auto savedObjects = s_cpuRotationObjects;
        const auto savedTotalObjects = s_totalCpuRotationObjects;
        const auto savedMs = s_cpuRotationMs;
        s_rotationProbe = RotationProbe::vanilla;
        s_probeCallbacks.clear();
        layer->rotateObjects(control, 12.5f, {5.f, 7.f}, {0.f, 0.f}, false, false);
        const auto callbacks = s_probeCallbacks;
        s_probeCallbacks.clear();
        s_rotationProbe = RotationProbe::none;
        const bool handled = tryHybridRotateObjects(layer, candidate, 12.5f,
            {5.f, 7.f}, {0.f, 0.f}, false, false);
        bool equal = handled && s_cpuRotationBatches == savedBatches + 1 && s_probeCallbacks == callbacks;
        for (unsigned i = 0; i < control->count() && equal; ++i)
            equal = rotationState(static_cast<GameObject*>(control->objectAtIndex(i))) ==
                rotationState(static_cast<GameObject*>(candidate->objectAtIndex(i)));
        s_cpuRotationBatches = savedBatches;
        s_cpuRotationObjects = savedObjects;
        s_totalCpuRotationObjects = savedTotalObjects;
        s_cpuRotationMs = savedMs;
        if (!equal) {
            s_rotationParity = -1;
            log::error("Automatic mixed solid/decoration rotation parity failed: handled={} expected_callbacks={} actual_callbacks={} cpu_gate={}; keeping vanilla",
                handled, callbacks.size(), s_probeCallbacks.size(), cpuRotationArrayEligible(candidate));
            return false;
        }
        log::info("VULKANIZER_ROTATION_AUTOMATIC_MIXED passed: real automatic gate selected CPU fast path; native state/callback parity matched; fixture work excluded from gameplay counts");
    }
    log::info("VULKANIZER_ROTATION_GD_PARITY passed: actual GD rotateObjects vs GPU/CPU candidate; 32 object comparisons; external pivot; move/rotate sequence; free/locked orientation; decoration/solid; history/dirty/rotation fields; section/finalizer callback order; scratch callbacks recorded without altering real level sections");
    return true;
}
}

void runRotationSelfTestIfRequested() {
    if (std::getenv("VULKANIZER_TRANSFORM_VANILLA_BASELINE") && std::getenv("VULKANIZER_SCHEDULER_GAMEPLAY_TEST")) {
        static bool nativeStarted = false;
        if (!nativeStarted) { nativeStarted = true; startSchedulerGameplayTest(); }
        return;
    }
    if (!std::getenv("VULKANIZER_ROTATION_SELFTEST")) return;
    static bool ran = false;
    if (ran || !RendererManager::get().gpuTransformReady()) return;
    ran = true;
    if (std::getenv("VULKANIZER_SCHEDULER_SELFTEST")) {
        if (std::getenv("VULKANIZER_SCHEDULER_DEFER_PROOF") && startSchedulerGameplayTest()) return;
        const bool passed = runRotationSchedulerProof();
        if (passed) runRotationSchedulerBenchmarks();
        log::info("VULKANIZER_SCHEDULER_SELFTEST result={}", passed ? "PASS" : "FAIL");
        if (passed && startSchedulerGameplayTest()) return;
        if (std::getenv("VULKANIZER_SELFTEST_EXIT"))
            PostMessageW(WindowFromDC(wglGetCurrentDC()), WM_CLOSE, 0, 0);
        return;
    }
    // A separate real GD layer and real GD objects exercise native helper
    // code. They never join gameplay collision/visibility sections. This
    // process-only test fixture remains alive until the test process exits.
    auto* layer = rotationProbeLayer();
    bool passed = validateRotationAgainstGD(layer);
    if (passed) {
      for (unsigned workload : {128u, 1024u, 8192u}) {
        auto* objects = CCArray::create();
        for (unsigned i = 0; i < workload; ++i) {
            auto* object = GameObject::createWithKey(1757);
            if (!object) { passed = false; break; }
            object->m_objectType = GameObjectType::Decoration;
            object->m_isDecoration2 = true;
            object->m_isTrigger = false;
            object->m_isStartPos = false;
            object->m_hasGroupParent = false;
            object->m_hasAreaParent = false;
            object->m_parentMode = 0;
            object->m_positionX = i * 3.125;
            object->m_positionY = i * 1.25;
            objects->addObject(object);
        }
        for (auto mode : {RotationProbe::vanilla, RotationProbe::cpu, RotationProbe::gpuUncached, RotationProbe::gpu}) {
            s_rotationProbe = mode;
            gpu::HybridTransformExecutor::get().setReadOnlyCachingForBenchmark(mode != RotationProbe::gpuUncached);
            s_probeCallbacks.clear();
            s_probeCallbacks.reserve(32 * (workload + 1));
            s_probeGpuMetrics = {};
            for (unsigned i = 0; i < objects->count(); ++i) {
                auto* object = static_cast<GameObject*>(objects->objectAtIndex(i));
                object->m_positionX = i * 3.125;
                object->m_positionY = i * 1.25;
                object->m_rotationXOffset = 0.f;
                object->m_rotationYOffset = 0.f;
                object->setRotation(0.f);
            }
            if (mode == RotationProbe::gpu || mode == RotationProbe::gpuUncached) {
                // Warm allocation/compilation equally in both GPU variants.
                if (!tryHybridRotateObjects(layer, objects, 0.25f, {120.f, 50.f}, {0.f, 0.f}, false, false)) {
                    passed = false;
                    break;
                }
                for (unsigned i = 0; i < objects->count(); ++i) {
                    auto* object = static_cast<GameObject*>(objects->objectAtIndex(i));
                    object->m_positionX = i * 3.125;
                    object->m_positionY = i * 1.25;
                    object->m_rotationXOffset = object->m_rotationYOffset = 0.f;
                    object->setRotation(0.f);
                }
                s_probeCallbacks.clear();
                s_probeGpuMetrics = {};
            }
            auto started = Clock::now();
            unsigned completed = 0;
            for (; completed < 32; ++completed) {
                // Include the live candidate's whole-array eligibility audit
                // in its measured time. Vanilla does not pay for this audit.
                if (mode != RotationProbe::vanilla && !cpuRotationArrayEligible(objects)) break;
                if (mode == RotationProbe::vanilla)
                    layer->rotateObjects(objects, 0.25f, {120.f, 50.f}, {0.f, 0.f}, false, false);
                else if (!tryHybridRotateObjects(layer, objects, 0.25f, {120.f, 50.f}, {0.f, 0.f}, false, false))
                    break;
            }
            const double wallMs = elapsedMs(started, Clock::now());
            log::info("VULKANIZER_ROTATION_BENCH mode={} objects={} batches={} wall_ms={:.4f} callback_scope=recorded_not_real_section_mutation",
                mode == RotationProbe::vanilla ? "vanilla" : mode == RotationProbe::cpu ? "CPU_fast" : mode == RotationProbe::gpuUncached ? "GPU_uncached" : "GPU_cached",
                workload, completed, wallMs);
            if (completed != 32) passed = false;
            if (mode == RotationProbe::gpu || mode == RotationProbe::gpuUncached)
                log::info("VULKANIZER_GPU_CACHE_BENCH caching={} objects={} cached_inputs={} uploaded_bytes={} upload_ms={:.4f} submit_ms={:.4f} gpu_ms={:.4f} wait_ms={:.4f} transfer_ms={:.4f}; GPU_time_overlaps_wait=true",
                    mode == RotationProbe::gpu, workload, s_probeGpuMetrics.cachedInputBuffers, s_probeGpuMetrics.uploadBytes,
                    s_probeGpuMetrics.uploadMs, s_probeGpuMetrics.dispatchSubmitMs,
                    s_probeGpuMetrics.gpuComputeMs, s_probeGpuMetrics.syncWaitMs, s_probeGpuMetrics.transferMs);
        }
        s_rotationProbe = RotationProbe::none;
      }
    }
    log::info("VULKANIZER_ROTATION_SELFTEST result={}", passed ? "PASS" : "FAIL");
    if (std::getenv("VULKANIZER_SELFTEST_EXIT"))
        PostMessageW(WindowFromDC(wglGetCurrentDC()), WM_CLOSE, 0, 0);
}
}
