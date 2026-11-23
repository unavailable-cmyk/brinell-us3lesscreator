#include "CpuRenderPhaseHooks.hpp"
#include "backend/HybridTransformPipeline.hpp"
#include "backend/RotationScheduler.hpp"
#include "backend/ResidentDecorations.hpp"
#include "v2/LiveScene.hpp"
#include "v2/VisibilityOwnership.hpp"
#include "v2/SpikeAudit.hpp"
#include "core/RendererManager.hpp"
#include "diagnostics/CpuCallbackScope.hpp"

#include <Geode/modify/CCActionManager.hpp>
#include <Geode/modify/CCNode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/CCTimer.hpp>
#include <Geode/modify/CCSprite.hpp>
#include <Geode/modify/CCSpriteBatchNode.hpp>
#include <Geode/modify/CCTextureAtlas.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <Geode/binding/GJEffectManager.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <intrin.h>

using namespace geode::prelude;
using vulkanizer::RendererManager;
using vulkanizer::v2::live::MovePolicyInvalidation;
namespace spike = vulkanizer::v2::spike;

namespace {
using Clock = RendererManager::Clock;
thread_local GJBaseGameLayer* s_v2NativeMoveLayer = nullptr;
thread_local unsigned s_sectionUpdateDepth = 0;

void nativeVisibilityEvent(spike::Event event) {
    if (vulkanizer::v2::live::nativeVisibilityProfileEnabled()) spike::event(event);
}

class SectionTransitionCounter {
public:
    explicit SectionTransitionCounter(GameObject* object) : m_object(object) {
        if (!vulkanizer::v2::live::nativeVisibilityProfileEnabled()) return;
        m_enabled = true;
        m_root = s_sectionUpdateDepth++ == 0;
        if (m_root && m_object) {
            m_outer = m_object->m_outerSectionIndex;
            m_middle = m_object->m_middleSectionIndex;
        }
    }
    ~SectionTransitionCounter() {
        if (!m_enabled) return;
        --s_sectionUpdateDepth;
        if (m_root && m_object && (m_outer != m_object->m_outerSectionIndex ||
                m_middle != m_object->m_middleSectionIndex))
            spike::event(spike::Event::nativeSectionTransitions);
    }
    SectionTransitionCounter(const SectionTransitionCounter&) = delete;
    SectionTransitionCounter& operator=(const SectionTransitionCounter&) = delete;
private:
    GameObject* m_object = nullptr;
    int m_outer = 0, m_middle = 0;
    bool m_enabled = false, m_root = false;
};

std::uint64_t s_moveSectionVisits = 0;
std::uint64_t s_moveSectionsAvoided = 0;
std::uint64_t s_moveLastLogged = 0;

constexpr auto timerPhase = vulkanizer::SchedulerCallbackPhase::timerUpdate;
constexpr std::size_t timerPhaseIndex = static_cast<std::size_t>(timerPhase);

vulkanizer::FramePhaseSample makeEmptySample() {
    vulkanizer::FramePhaseSample sample{};
    sample.callbacks[timerPhaseIndex].estimateWeight = 8;
    return sample;
}

thread_local vulkanizer::FramePhaseSample s_sample = makeEmptySample();
thread_local unsigned s_nodeVisitDepth = 0;
thread_local std::uint64_t s_nodeVisitCalls = 0;
thread_local std::uint64_t s_spriteTransformCallIndex = 0;
thread_local std::uint64_t s_spriteDrawCallIndex = 0;
thread_local std::uint64_t s_atlasDrawCallIndex = 0;
thread_local std::uint64_t s_timerUpdateCallIndex = 0;
thread_local std::uint64_t s_moveActionSampleIndex = 0;
thread_local std::uint64_t s_rotationActionSampleIndex = 0;
thread_local vulkanizer::CpuCallbackScopeRecorder s_callbackScopeRecorder;

enum class TransformContext : std::uint8_t { none, move, rotation };
thread_local TransformContext s_transformContext = TransformContext::none;
thread_local bool s_transformTimingSampled = false;

class TransformContextScope {
public:
    TransformContextScope(TransformContext context, bool timingSampled)
      : m_previousContext(s_transformContext), m_previousTimingSampled(s_transformTimingSampled) {
        s_transformContext = context;
        s_transformTimingSampled = timingSampled;
    }
    ~TransformContextScope() {
        s_transformContext = m_previousContext;
        s_transformTimingSampled = m_previousTimingSampled;
    }
    TransformContextScope(TransformContextScope const&) = delete;
    TransformContextScope& operator=(TransformContextScope const&) = delete;

private:
    TransformContext m_previousContext;
    bool m_previousTimingSampled;
};

class CallbackProfileScope {
public:
    CallbackProfileScope(vulkanizer::SchedulerCallbackPhase phase, std::uint32_t estimateWeight = 1,
                         bool countInvocation = true, std::uint32_t parentEstimateWeight = 1)
        { s_callbackScopeRecorder.begin(m_scope, phase, Clock::now(), s_sample,
                                        estimateWeight, countInvocation, parentEstimateWeight); }

    CallbackProfileScope(CallbackProfileScope const&) = delete;
    CallbackProfileScope& operator=(CallbackProfileScope const&) = delete;

    ~CallbackProfileScope() { s_callbackScopeRecorder.end(m_scope, Clock::now(), s_sample); }

private:
    vulkanizer::CpuCallbackScopeRecorder::Token m_scope{};
};

template <class Callback>
void profileSchedulerCallback(RendererManager& manager, vulkanizer::SchedulerCallbackPhase phase,
                              Callback&& callback) {
    if (!manager.diagnosticsEnabled()) {
        callback();
        return;
    }
    CallbackProfileScope scope(phase);
    callback();
}

template <class Callback>
void profileObjectArrayCallback(RendererManager& manager, vulkanizer::SchedulerCallbackPhase phase,
                                cocos2d::CCArray* objects, Callback&& callback) {
    if (!manager.diagnosticsEnabled()) {
        callback();
        return;
    }
    const auto objectCount = static_cast<std::uint64_t>(objects ? objects->count() : 0u);
    if ((phase == vulkanizer::SchedulerCallbackPhase::moveObjects &&
         s_transformContext == TransformContext::move) ||
        (phase == vulkanizer::SchedulerCallbackPhase::rotateObjects &&
         s_transformContext == TransformContext::rotation)) {
        auto& measured = s_sample.callbacks[static_cast<std::size_t>(phase)];
        ++measured.calls;
        measured.workItems += objectCount;
        measured.estimateWeight = 64;
        if (s_transformTimingSampled) {
            CallbackProfileScope scope(phase, 64, false, 1);
            callback();
        } else {
            callback();
        }
        return;
    }
    s_sample.callbacks[static_cast<std::size_t>(phase)].workItems += objectCount;
    CallbackProfileScope scope(phase);
    callback();
}

template <class Callback>
void profileTransformChild(RendererManager& manager, vulkanizer::SchedulerCallbackPhase phase,
                           std::uint64_t workItems, Callback&& callback) {
    if (!manager.diagnosticsEnabled() || s_transformContext == TransformContext::none) {
        callback();
        return;
    }
    auto& measured = s_sample.callbacks[static_cast<std::size_t>(phase)];
    ++measured.calls;
    measured.workItems += workItems;
    measured.estimateWeight = 64;
    if (s_transformTimingSampled) {
        CallbackProfileScope scope(phase, 64, false, 1);
        callback();
    } else {
        callback();
    }
}

template <class Callback>
void profileTransformAction(RendererManager& manager, vulkanizer::SchedulerCallbackPhase phase,
                            TransformContext context, std::uint64_t workItems,
                            std::uint64_t& sampleIndex, Callback&& callback) {
    if (!manager.diagnosticsEnabled()) {
        callback();
        return;
    }
    auto& measured = s_sample.callbacks[static_cast<std::size_t>(phase)];
    ++measured.calls;
    measured.workItems += workItems;
    measured.estimateWeight = 64;
    const bool sampled = (sampleIndex++ & 0x3f) == 0;
    TransformContextScope contextScope(context, sampled);
    if (sampled) {
        CallbackProfileScope scope(phase, 64, false, 64);
        callback();
    } else {
        callback();
    }
}

template <class Callback>
void countOnlyCallback(RendererManager& manager, vulkanizer::SchedulerCallbackPhase phase,
                       Callback&& callback) {
    if (manager.diagnosticsEnabled())
        ++s_sample.callbacks[static_cast<std::size_t>(phase)].calls;
    callback();
}

template <class Callback>
void profileSampledCallback(RendererManager& manager, vulkanizer::SchedulerCallbackPhase phase,
                           std::uint32_t weight, bool sampled, Callback&& callback) {
    if (!manager.diagnosticsEnabled()) {
        callback();
        return;
    }
    if (!sampled) {
        ++s_sample.callbacks[static_cast<std::size_t>(phase)].calls;
        callback();
        return;
    }
    CallbackProfileScope scope(phase, weight);
    callback();
}
} // namespace

namespace vulkanizer {
FramePhaseSample takeCpuRenderPhaseSample() {
    if (!s_callbackScopeRecorder.balanced()) ++s_sample.scopeStackErrors;
    auto result = s_sample;
    s_sample = makeEmptySample();
    return result;
}
} // namespace vulkanizer

class $modify(VulkanizerSchedulerPhase, cocos2d::CCScheduler) {
    void update(float dt) {
        spike::Scope timing(spike::Phase::scheduler);
        spike::schedulerInput(dt);
        vulkanizer::recordGameplayFrameInput(dt);
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::schedulerUpdate,
            [&] { CCScheduler::update(dt); });
        vulkanizer::recordGameplayFrameComplete();
    }
};

class $modify(VulkanizerActionManagerPhase, cocos2d::CCActionManager) {
    void update(float dt) {
        spike::Scope timing(spike::Phase::actionManager);
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::actionManagerUpdate,
            [&] { CCActionManager::update(dt); });
    }
};

class $modify(VulkanizerTimerPhase, cocos2d::CCTimer) {
    void update(float dt) {
        auto& manager = RendererManager::get();
        if (manager.diagnosticsEnabled()) ++s_sample.timerUpdateCalls;
        const bool sampled = (s_timerUpdateCallIndex++ & 0x7) == 0;
        profileSampledCallback(manager, timerPhase, 8, sampled, [&] { CCTimer::update(dt); });
    }
};

class $modify(VulkanizerGameUpdatePhase, GJBaseGameLayer) {
    void update(float dt) {
        spike::Scope timing(spike::Phase::gameUpdate);
        spike::gameInput(dt);
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::gameLayerUpdate,
            [&] { GJBaseGameLayer::update(dt); });
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        spike::Scope timing(spike::Phase::commands);
        vulkanizer::v2::live::AuditPhaseScope audit("commands");
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::processCommands,
            [&] { GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick); });
    }

    void preUpdateVisibility(float dt) {
        spike::Scope timing(spike::Phase::preVisibility);
        {
            spike::Scope hookTiming(spike::Phase::preVisibilityHooks, 0, false, 0,
                vulkanizer::v2::live::nativeVisibilityProfileEnabled());
            if (auto* play = typeinfo_cast<PlayLayer*>(this)) vulkanizer::v2::live::beforeVisibility(play);
        }
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::preUpdateVisibility,
            [&] {
                spike::Scope stockTiming(spike::Phase::stockPreVisibility, 0, false, 0,
                    vulkanizer::v2::live::nativeVisibilityProfileEnabled());
                GJBaseGameLayer::preUpdateVisibility(dt);
            });
    }

    void processMoveActions() {
        spike::Scope timing(spike::Phase::moveActions);
        vulkanizer::v2::live::AuditPhaseScope audit("move_actions");
        vulkanizer::v2::live::beforeDependencies(this);
        vulkanizer::enterNativeMoveAttribution();
        struct AttributionScope { ~AttributionScope(){vulkanizer::leaveNativeMoveAttribution();} } attributionScope;
        vulkanizer::prepareVisualMoveDependencies(this);
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            GJBaseGameLayer::processMoveActions();
            return;
        }
        const auto entries = this->m_effectManager
            ? static_cast<std::uint64_t>(this->m_effectManager->m_unkVector6c0.size()) : 0;
        profileTransformAction(manager, vulkanizer::SchedulerCallbackPhase::processMoveActions,
            TransformContext::move, entries, s_moveActionSampleIndex,
            [&] { GJBaseGameLayer::processMoveActions(); });
    }

    void processMoveActionsStep(float dt, bool visibleFrame) {
        spike::Scope timing(spike::Phase::moveStep, 0, true, dt);
        vulkanizer::v2::live::AuditPhaseScope audit("move_step");
        vulkanizer::recordGameplayPhysicsInput(this, dt);
        auto& manager = RendererManager::get();
        vulkanizer::beginHybridTransformStep();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::processMoveActionsStep,
            [&] { GJBaseGameLayer::processMoveActionsStep(dt, visibleFrame); });
        vulkanizer::endHybridTransformStep();
        if (auto* play = typeinfo_cast<PlayLayer*>(this)) vulkanizer::v2::live::physicsStep(play, dt, visibleFrame);
        vulkanizer::recordTransformCheckpoint(this);
    }

    void moveObjects(cocos2d::CCArray* objects, double dx, double dy, bool lockPlayerY) {
        spike::Scope timing(spike::Phase::moveArray);
        // Preserve the parent phase for moves invoked by rotation/follow/spawn.
        vulkanizer::v2::live::AuditPhaseScope audit(
            std::strcmp(vulkanizer::v2::live::auditPhase, "other") == 0
                ? "move_array" : vulkanizer::v2::live::auditPhase);
        if (vulkanizer::v2::live::move(
    this, objects, dx, dy, lockPlayerY,
    [&](CCArray* native) {
        static const bool enabled = vulkanizer::v2::live::moveSectionsRequested();

        struct Scope {
            GJBaseGameLayer* previous;

            Scope(GJBaseGameLayer* layer)
                : previous(s_v2NativeMoveLayer) {
                s_v2NativeMoveLayer = layer;
            }

            ~Scope() {
                s_v2NativeMoveLayer = previous;
            }
        } scope(enabled ? this : nullptr);

        GJBaseGameLayer::moveObjects(
            native, dx, dy, lockPlayerY
        );

        if (s_moveSectionVisits - s_moveLastLogged >= 1000000) {
            s_moveLastLogged = s_moveSectionVisits;

            log::info(
                "VULKANIZER_MOVE_SECTIONS visits={} avoided={}",
                s_moveSectionVisits,
                s_moveSectionsAvoided
            );
        }
    }
)) return;
        auto& manager = RendererManager::get();
        profileObjectArrayCallback(manager, vulkanizer::SchedulerCallbackPhase::moveObjects, objects,
            [&] {
                if (!vulkanizer::tryVisualMoveObjects(this, objects, dx, dy) &&
                    !vulkanizer::tryHybridMoveObjects(this, objects, dx, dy, lockPlayerY))
                    GJBaseGameLayer::moveObjects(objects, dx, dy, lockPlayerY);
            });
    }

    void processRotationActions() {
    spike::Scope timing(spike::Phase::rotation);
    vulkanizer::v2::live::AuditPhaseScope audit("rotation");
    vulkanizer::v2::live::beforeDependencies(this);

    if (vulkanizer::v2::live::activeFor(this)) {
        vulkanizer::v2::live::NativeEditScope edit;
        if (!vulkanizer::tryV2RotationActions(this)) {
            GJBaseGameLayer::processRotationActions();
        }

        return;
    }
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            if (!vulkanizer::tryProcessRotationActions(this)) GJBaseGameLayer::processRotationActions();
            return;
        }
        const auto entries = this->m_effectManager
            ? static_cast<std::uint64_t>(this->m_effectManager->m_unkVector5b0.size()) : 0;
        profileTransformAction(manager, vulkanizer::SchedulerCallbackPhase::processRotationActions,
            TransformContext::rotation, entries, s_rotationActionSampleIndex,
            [&] { if (!vulkanizer::tryProcessRotationActions(this)) GJBaseGameLayer::processRotationActions(); });
    }

    GameObject* tryGetMainObject(int groupID) {
        auto* object = GJBaseGameLayer::tryGetMainObject(groupID);
        if (vulkanizer::v2::live::aggressiveActive())
            vulkanizer::v2::live::beforeResidentRead(object, "native_group_anchor");
        return object;
    }

    void claimRotationAction(int targetID, int centerID, float& rotation, float& offset, bool ignoreStatic, bool unused) {
        spike::Scope timing(spike::Phase::rotationClaim);
        vulkanizer::v2::live::AuditPhaseScope audit("rotation_claim");
        vulkanizer::v2::live::NativeEditScope edit;
        if (!vulkanizer::v2::live::groupRotationClaimActive(this))
            vulkanizer::v2::live::beforeNativeGroupTransform(this, targetID, ignoreStatic, "native_claimed_rotation_or_transform");
        GJBaseGameLayer::claimRotationAction(targetID, centerID, rotation, offset, ignoreStatic, unused);
    }

    void processFollowActions() {
        spike::Scope timing(spike::Phase::follow);
        vulkanizer::v2::live::AuditPhaseScope audit("follow");
        vulkanizer::v2::live::beforeNativeFollow(this);
        GJBaseGameLayer::processFollowActions();
    }

    void addToSection(GameObject* object) {
        vulkanizer::v2::live::beforeSectionRegistration(this, object);
        if (vulkanizer::v2::live::aggressiveActive())
            vulkanizer::v2::live::beforeResidentRead(object, "native_section_registration");
        GJBaseGameLayer::addToSection(object);
        nativeVisibilityEvent(spike::Event::nativeSectionRegistrations);
        vulkanizer::v2::live::afterSectionRegistration(this, object);
    }

    void updateObjectSection(GameObject* object) {
        SectionTransitionCounter transition(object);
        bool sectionCommitRecorded = false;
        if (s_v2NativeMoveLayer == this) {
    ++s_moveSectionVisits;

    static const bool cachedPolicies = vulkanizer::v2::live::cachedPoliciesRequested();
    if (cachedPolicies) {
        switch (vulkanizer::v2::live::cachedMoveSection(this, object)) {
            case vulkanizer::v2::live::CachedMoveSectionResult::skip:
                ++s_moveSectionsAvoided;
                return;
            case vulkanizer::v2::live::CachedMoveSectionResult::native:
                sectionCommitRecorded = true;
                break;
            case vulkanizer::v2::live::CachedMoveSectionResult::notApplicable:
                break;
        }
    }
    if (!sectionCommitRecorded &&
        vulkanizer::v2::live::canSkipMoveSection(this, object) &&
        !vulkanizer::v2::live::owns(object)) {
        ++s_moveSectionsAvoided;
        return;
    }
}
        if (!sectionCommitRecorded) vulkanizer::v2::live::recordSectionCommit(object);
        if (vulkanizer::hybridParityProbeActive()) {
            vulkanizer::recordHybridParityCallback(false);
            return;
        }
        // Sample only actual base-layer section calls, after skip decisions.
        // The native array timer already includes these; never add the estimate
        // to nativeMoveMs. Preserve the ordinary profiler path otherwise.
        static const bool auditSections = vulkanizer::v2::live::auditRequested();
        static thread_local std::uint64_t auditSectionIndex = 0;
        auto nativeSection = [&] {
            spike::Scope timing(spike::Phase::nativeSection);
            if (!(auditSections && s_v2NativeMoveLayer == this &&
                    (++auditSectionIndex & 1023u) == 0)) {
                GJBaseGameLayer::updateObjectSection(object);
                return;
            }
            const auto started = Clock::now();
            GJBaseGameLayer::updateObjectSection(object);
            vulkanizer::v2::live::recordAuditSectionSample(
                std::chrono::duration<double, std::milli>(Clock::now() - started).count());
        };
        auto& manager = RendererManager::get();
        if (s_transformContext == TransformContext::move) {
            profileTransformChild(manager, vulkanizer::SchedulerCallbackPhase::moveUpdateObjectSection,
                1, nativeSection);
        } else if (s_transformContext == TransformContext::rotation) {
            profileTransformChild(manager, vulkanizer::SchedulerCallbackPhase::rotationUpdateObjectSection,
                1, nativeSection);
        } else {
            nativeSection();
        }
    }


    void rotateObjects(cocos2d::CCArray* objects, float rotation, cocos2d::CCPoint position,
                       cocos2d::CCPoint offset, bool finished, bool unused) {
        spike::Scope timing(spike::Phase::rotationArray, spike::audit.active && objects ? objects->count() : 0);
        if (spike::audit.active) vulkanizer::v2::live::recordSpikeRotationArray(objects);
        vulkanizer::v2::live::AuditPhaseScope audit("rotation_array");
        if (vulkanizer::v2::live::activeFor(this)) {
            vulkanizer::v2::live::NativeEditScope edit;
            vulkanizer::v2::live::recordAuditRotationArray(objects);
            if (!vulkanizer::v2::live::retainNativeEditArray(objects))
                vulkanizer::v2::live::releaseArray(objects, "native_bulk_rotation");
            GJBaseGameLayer::rotateObjects(objects, rotation, position, offset, finished, unused);
            return;
        }
        auto& manager = RendererManager::get();
        profileObjectArrayCallback(manager, vulkanizer::SchedulerCallbackPhase::rotateObjects, objects,
            [&] {
                if (!vulkanizer::tryHybridRotateObjects(this, objects, rotation, position, offset, finished, unused))
                    GJBaseGameLayer::rotateObjects(objects, rotation, position, offset, finished, unused);
            });
    }

    void rotateObject(GameObject* object, float rotation) {
        vulkanizer::v2::live::AuditPhaseScope audit("rotation_object");
        vulkanizer::v2::live::NativeEditScope edit;
        if (!vulkanizer::v2::live::retainNativeEdit(object))
            vulkanizer::v2::live::release(object, "native_single_rotation");
        auto& manager = RendererManager::get();
        countOnlyCallback(manager, vulkanizer::SchedulerCallbackPhase::rotateObject,
            [&] { GJBaseGameLayer::rotateObject(object, rotation); });
    }

    void processTransformActions(bool visibleFrame) {
        spike::Scope timing(spike::Phase::transform);
        vulkanizer::v2::live::AuditPhaseScope audit("transform");
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::processTransformActions,
            [&] { GJBaseGameLayer::processTransformActions(visibleFrame); });
    }

    void processAreaActions(float dt, bool visibleFrame) {
        spike::Scope timing(spike::Phase::area);
        vulkanizer::v2::live::AuditPhaseScope audit("area");
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::processAreaActions,
            [&] { GJBaseGameLayer::processAreaActions(dt, visibleFrame); });
    }

    void processAreaEffects(std::vector<EnterEffectInstance>* effects, GJAreaActionType type,
                            float dt, bool visibleFrame) {
        spike::Scope timing(spike::Phase::areaEffects);
        vulkanizer::v2::live::AuditPhaseScope audit("area_effects");
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::processAreaEffects,
            [&] { GJBaseGameLayer::processAreaEffects(effects, type, dt, visibleFrame); });
    }

    void processAreaMoveGroupAction(CCArray* objects, EnterEffectInstance* instance, CCPoint position,
                                    int outerMin, int outerMax, int middleMin, int middleMax,
                                    int startIndex, bool targetGroups, bool reset) {
        if (vulkanizer::v2::live::aggressiveActive())
            vulkanizer::v2::live::releaseArray(objects, "native_area_move");
        GJBaseGameLayer::processAreaMoveGroupAction(objects, instance, position, outerMin, outerMax,
            middleMin, middleMax, startIndex, targetGroups, reset);
    }

    void processAreaRotateGroupAction(CCArray* objects, EnterEffectInstance* instance, CCPoint position,
                                      int outerMin, int outerMax, int middleMin, int middleMax,
                                      int startIndex, bool targetGroups, bool reset) {
        if (vulkanizer::v2::live::aggressiveActive())
            vulkanizer::v2::live::releaseArray(objects, "native_area_rotation");
        GJBaseGameLayer::processAreaRotateGroupAction(objects, instance, position, outerMin, outerMax,
            middleMin, middleMax, startIndex, targetGroups, reset);
    }

    void processAreaTransformGroupAction(CCArray* objects, EnterEffectInstance* instance, CCPoint position,
                                         int outerMin, int outerMax, int middleMin, int middleMax,
                                         int startIndex, bool targetGroups, bool reset) {
        if (vulkanizer::v2::live::aggressiveActive())
            vulkanizer::v2::live::releaseArray(objects, "native_area_transform");
        GJBaseGameLayer::processAreaTransformGroupAction(objects, instance, position, outerMin, outerMax,
            middleMin, middleMax, startIndex, targetGroups, reset);
    }

    void addToGroup(GameObject* object, int groupID, bool triggerGroup) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeMembershipMutation(this, object, groupID, "group_membership_add");
        vulkanizer::v2::live::invalidateAllMoveSectionPolicies(MovePolicyInvalidation::layerGroupAdd);
        GJBaseGameLayer::addToGroup(object, groupID, triggerGroup);
        vulkanizer::invalidateHybridGroupAudit(this);
    }

    void removeFromGroup(GameObject* object, int groupID) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeMembershipMutation(this, object, groupID, "group_membership_remove");
        vulkanizer::v2::live::invalidateAllMoveSectionPolicies(MovePolicyInvalidation::layerGroupRemove);
        GJBaseGameLayer::removeFromGroup(object, groupID);
        vulkanizer::invalidateHybridGroupAudit(this);
    }

    void addToGroupParents(GameObject* object) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeMembershipMutation(this, object, 0, "group_parent_add");
        vulkanizer::v2::live::invalidateAllMoveSectionPolicies(MovePolicyInvalidation::groupParentAdd);
        GJBaseGameLayer::addToGroupParents(object);
    }

    void removeFromGroupParents(GameObject* object) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeMembershipMutation(this, object, 0, "group_parent_remove");
        vulkanizer::v2::live::invalidateAllMoveSectionPolicies(MovePolicyInvalidation::groupParentRemove);
        GJBaseGameLayer::removeFromGroupParents(object);
    }

    void setGroupParent(GameObject* object, int groupID) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeMembershipMutation(this, object, groupID, "group_parent_set");
        vulkanizer::v2::live::invalidateAllMoveSectionPolicies(MovePolicyInvalidation::groupParentSet);
        GJBaseGameLayer::setGroupParent(object, groupID);
    }

    void removeGroupParent(GameObject* object, int groupID) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeMembershipMutation(this, object, groupID, "group_parent_remove_one");
        vulkanizer::v2::live::invalidateAllMoveSectionPolicies(MovePolicyInvalidation::groupParentRemoveOne);
        GJBaseGameLayer::removeGroupParent(object, groupID);
    }

    void promoteGroupParent(GameObject* object, int groupID) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeMembershipMutation(this, object, groupID, "group_parent_promote");
        vulkanizer::v2::live::invalidateAllMoveSectionPolicies(MovePolicyInvalidation::groupParentPromote);
        GJBaseGameLayer::promoteGroupParent(object, groupID);
    }

    void loadGroupParentsFromString(GameObject* object, std::string groupList) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeMembershipMutation(this, object, 0, "group_parent_load");
        vulkanizer::v2::live::invalidateAllMoveSectionPolicies(MovePolicyInvalidation::groupParentLoad);
        GJBaseGameLayer::loadGroupParentsFromString(object, groupList);
    }

    void destroyObject(GameObject* object) {
        vulkanizer::invalidateExactDeferredMoves(this);
        vulkanizer::v2::live::beforeObjectRetirement(object);
        GJBaseGameLayer::destroyObject(object);
    }

    void toggleGroup(int groupID, bool activate) {
        spike::Scope timing(spike::Phase::toggle);
        vulkanizer::resolveExactDeferredToggle(this, groupID);
        vulkanizer::v2::live::beforeToggle(this, groupID);
        GJBaseGameLayer::toggleGroup(groupID, activate);
    }

    void spawnGroup(int group, bool ordered, double delay, std::vector<int> const& remapKeys,
                    int triggerID, int controlID) {
        spike::Scope timing(spike::Phase::spawn);
        spike::event(spike::Event::spawnCalls);
        vulkanizer::v2::live::AuditPhaseScope audit("spawn");
        if (!remapKeys.empty()) {
            vulkanizer::invalidateExactDeferredMoves(this);
            if (!vulkanizer::v2::live::aggressiveActive()) vulkanizer::v2::live::releaseAll("spawn_remapping");
        }
        GJBaseGameLayer::spawnGroup(group, ordered, delay, remapKeys, triggerID, controlID);
        vulkanizer::invalidateHybridGroupAudit(this);
    }
};

class $modify(VulkanizerV2ObjectBoundary, GameObject) {
    static void onModify(auto& self) {
        const auto* exact = std::getenv("VULKANIZER_TRANSFORM_EXACT_XLOCK");
        if (!vulkanizer::v2::live::requested() &&
            !(vulkanizer::exactDeferredMovesLiveEnabled && exact && std::strcmp(exact, "1") == 0))
            for (auto* hook : self.getAllHooks()) hook->setAutoEnable(false);
    }
    void setPosition(CCPoint const& position) {
        static const bool callerAudit = vulkanizer::v2::live::nativeCallersRequested();
        static const bool revocationAudit = vulkanizer::v2::live::revocationAuditRequested();
        static const bool setterAudit = vulkanizer::v2::live::positionSetterAuditRequested();
        const auto audit = setterAudit ? vulkanizer::v2::live::beginPositionSetterAudit(this, position.x, position.y, _AddressOfReturnAddress()) :
            vulkanizer::v2::live::PositionSetterAuditToken{};
        if (callerAudit) vulkanizer::v2::live::recordNativeCaller(this, vulkanizer::v2::live::NativeCallerKind::positionSetter);
        vulkanizer::resolveExactDeferredObject(this);
        vulkanizer::v2::live::NativeEditScope edit;
        if (!(audit.scene && vulkanizer::v2::live::retainPositionSpriteSync(audit, this, position.x, position.y)) &&
            !vulkanizer::v2::live::retainNativeEdit(this))
            vulkanizer::v2::live::release(this, "native_position_setter", revocationAudit ? _AddressOfReturnAddress() : nullptr);
        if (audit.scene) vulkanizer::v2::live::beforePositionSetterNative(audit, this, position.x, position.y);
        GameObject::setPosition(position);
        if (audit.scene) vulkanizer::v2::live::endPositionSetterAudit(audit, this);
    }
    void setType(GameObjectType type) {
        vulkanizer::invalidateExactDeferredObject(this);
        if (m_objectType != type) {
            vulkanizer::v2::live::release(this, "native_object_type");
            vulkanizer::v2::live::invalidateMoveSectionPolicy(this, MovePolicyInvalidation::objectType);
        }
        GameObject::setType(type);
    }
    void setStartPos(CCPoint position) {
        vulkanizer::invalidateExactDeferredObject(this);
        vulkanizer::v2::live::release(this, "native_start_position_classification");
        vulkanizer::v2::live::invalidateMoveSectionPolicy(this, MovePolicyInvalidation::startPosition);
        GameObject::setStartPos(position);
    }
    int addToGroup(int id) {
        vulkanizer::invalidateExactDeferredObject(this);
        vulkanizer::v2::live::release(this, "native_object_group_add");
        vulkanizer::v2::live::invalidateMoveSectionPolicy(this, MovePolicyInvalidation::objectGroupAdd);
        return GameObject::addToGroup(id);
    }
    void removeFromGroup(int id) {
        vulkanizer::invalidateExactDeferredObject(this);
        vulkanizer::v2::live::release(this, "native_object_group_remove");
        vulkanizer::v2::live::invalidateMoveSectionPolicy(this, MovePolicyInvalidation::objectGroupRemove);
        GameObject::removeFromGroup(id);
    }
    CCRect const& getObjectRect() {
        vulkanizer::resolveExactDeferredObject(this);
        vulkanizer::v2::live::beforeHistoryRead(this);
        return GameObject::getObjectRect();
    }
    CCRect getObjectRect(float width, float height) {
        vulkanizer::resolveExactDeferredObject(this);
        vulkanizer::v2::live::beforeHistoryRead(this);
        return GameObject::getObjectRect(width, height);
    }
    CCRect const& getObjectRect2(float width, float height) {
        vulkanizer::resolveExactDeferredObject(this);
        vulkanizer::v2::live::beforeResidentRead(this, "resident_collision_rectangle_read");
        return GameObject::getObjectRect2(width, height);
    }
    CCRect const& getObjectTextureRect() {
        static const bool callerAudit = vulkanizer::v2::live::nativeCallersRequested();
        if (callerAudit) vulkanizer::v2::live::recordNativeCaller(this, vulkanizer::v2::live::NativeCallerKind::textureRect);
        vulkanizer::resolveExactDeferredObject(this);
        vulkanizer::v2::live::beforeResidentRead(this, "resident_texture_rectangle_read");
        return GameObject::getObjectTextureRect();
    }
    CCPoint getRealPosition() {
        vulkanizer::resolveExactDeferredObject(this);
        vulkanizer::v2::live::beforeResidentRead(this, "resident_real_position_read");
        return GameObject::getRealPosition();
    }
    void setVisible(bool visible) {
        static const bool ownershipShadowRequested = [] {
            const auto* value = std::getenv("VULKANIZER_V2_VISIBILITY_OWNERSHIP_SHADOW");
            return value && std::strcmp(value, "1") == 0;
        }();
        if (visible != isVisible()) nativeVisibilityEvent(spike::Event::visibilitySetChanges);
        if (visible != isVisible()) vulkanizer::resolveExactDeferredObject(this);
        if (visible != isVisible() && !vulkanizer::v2::live::retainVisibility(this, visible))
            vulkanizer::v2::live::release(this, "native_visibility_change");
        const bool shadowObservation = ownershipShadowRequested && visible != isVisible() &&
            vulkanizer::v2::live::visibilityOwnershipShadowEnabled();
        GameObject::setVisible(visible);
        if (shadowObservation)
            vulkanizer::v2::live::recordVisibilityOwnershipNativeVisibility(this, visible);
    }
    void setOpacity(unsigned char opacity) {
        static const bool callerAudit = vulkanizer::v2::live::nativeCallersRequested();
        const bool changed = opacity != getOpacity();
        if (changed) nativeVisibilityEvent(spike::Event::opacityChanges);
        if (callerAudit && changed)
            vulkanizer::v2::live::recordNativeCaller(this, vulkanizer::v2::live::NativeCallerKind::opacity);
        if (changed && !vulkanizer::v2::live::nativeMaterialChanged(this, "native_opacity_change") &&
            !vulkanizer::v2::live::ownsHeadlessMaterial(this))
            vulkanizer::v2::live::release(this, "native_opacity_change");
        GameObject::setOpacity(opacity);
    }
    void activateObject() {
        if (!m_isActivated) nativeVisibilityEvent(spike::Event::activationTransitions);
        if (!m_isActivated) vulkanizer::resolveExactDeferredObject(this);
        if (!m_isActivated && !vulkanizer::v2::live::retainActivation(this)) vulkanizer::v2::live::release(this, "native_activation");
        GameObject::activateObject();
    }
    void deactivateObject(bool deactivate) {
        // Native false calls mark the visibility pass. Its actual visibility
        // transition reaches setVisible, where ownership is released first.
        if (deactivate && m_isActivated) nativeVisibilityEvent(spike::Event::deactivationTransitions);
        if (deactivate) vulkanizer::resolveExactDeferredObject(this);
        if (deactivate && !vulkanizer::v2::live::retainActivation(this)) vulkanizer::v2::live::release(this, "native_deactivation");
        GameObject::deactivateObject(deactivate);
    }
    void setRotation(float value) {
        vulkanizer::v2::live::NativeEditScope edit;
        if (value != getRotation() && !vulkanizer::v2::live::retainNativeEdit(this))
            vulkanizer::v2::live::release(this, "native_rotation");
        GameObject::setRotation(value);
    }
    void setScale(float value) {
        if (value != getScaleX() || value != getScaleY()) vulkanizer::v2::live::release(this, "native_scale");
        GameObject::setScale(value);
    }
    void setScaleX(float value) {
        if (value != getScaleX()) vulkanizer::v2::live::release(this, "native_scale_x");
        GameObject::setScaleX(value);
    }
    void setScaleY(float value) {
        if (value != getScaleY()) vulkanizer::v2::live::release(this, "native_scale_y");
        GameObject::setScaleY(value);
    }
    void setRotationX(float value) {
        vulkanizer::v2::live::NativeEditScope edit;
        if (value != getRotationX() && !vulkanizer::v2::live::retainNativeEdit(this))
            vulkanizer::v2::live::release(this, "native_rotation_x");
        GameObject::setRotationX(value);
    }
    void setRotationY(float value) {
        vulkanizer::v2::live::NativeEditScope edit;
        if (value != getRotationY() && !vulkanizer::v2::live::retainNativeEdit(this))
            vulkanizer::v2::live::release(this, "native_rotation_y");
        GameObject::setRotationY(value);
    }
    void setFlipX(bool value) {
        if (value != isFlipX()) vulkanizer::v2::live::release(this, "native_flip_x");
        GameObject::setFlipX(value);
    }
    void setFlipY(bool value) {
        if (value != isFlipY()) vulkanizer::v2::live::release(this, "native_flip_y");
        GameObject::setFlipY(value);
    }
    void resetObject() {
        vulkanizer::invalidateExactDeferredObject(this);
        vulkanizer::v2::live::release(this, "native_object_reset");
        vulkanizer::v2::live::invalidateMoveSectionPolicy(this, MovePolicyInvalidation::objectReset);
        GameObject::resetObject();
    }
};

class $modify(VulkanizerTransformObjectWork, GameObject) {
    void addRotation(float rotation) {
        vulkanizer::v2::live::NativeEditScope edit;
        if (!vulkanizer::v2::live::retainNativeEdit(this))
            vulkanizer::v2::live::release(this, "native_rotation_delta");
        auto& manager = RendererManager::get();
        if (s_transformContext == TransformContext::rotation) {
            profileTransformChild(manager, vulkanizer::SchedulerCallbackPhase::rotationAddRotation,
                1, [&] { GameObject::addRotation(rotation); });
        } else {
            GameObject::addRotation(rotation);
        }
    }

    void updateOrientedBox() {
        auto& manager = RendererManager::get();
        if (s_transformContext == TransformContext::rotation) {
            profileTransformChild(manager, vulkanizer::SchedulerCallbackPhase::rotationUpdateOrientedBox,
                1, [&] { GameObject::updateOrientedBox(); });
        } else {
            GameObject::updateOrientedBox();
        }
    }

};

class $modify(VulkanizerPlayLayerUpdatePhase, PlayLayer) {
    bool init(GJGameLevel* level, bool dontCreateObjects) {
        const auto initStarted = Clock::now();
        if (!PlayLayer::init(level, dontCreateObjects)) return false;
        if (!dontCreateObjects) {
            vulkanizer::v2::live::beginLevel(this,
                std::chrono::duration<double, std::milli>(Clock::now() - initStarted).count());
            const auto precompileStarted = Clock::now();
            vulkanizer::precompileLevelTransforms(this);
            vulkanizer::v2::live::recordLegacyPrecompile(this,
                std::chrono::duration<double, std::milli>(Clock::now() - precompileStarted).count());
        }
        return true;
    }
    void onExit() {
        vulkanizer::v2::live::destroy(this, "scene_exit");
        vulkanizer::flushPendingVisualTransforms(this, true);
        vulkanizer::resetRotationPlans();
        vulkanizer::invalidateHybridGroupAudit(this);
        PlayLayer::onExit();
    }

    void updateVisibility(float dt) {
        spike::Scope timing(spike::Phase::visibility);
        {
            spike::Scope hookTiming(spike::Phase::visibilityHooks, 0, false, 0,
                vulkanizer::v2::live::nativeVisibilityProfileEnabled());
            vulkanizer::v2::live::beforeVisibility(this);
            vulkanizer::flushPendingVisualTransforms(this);
        }
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::playLayerUpdateVisibility,
            [&] {
                spike::Scope stockTiming(spike::Phase::stockVisibility, 0, false, 0,
                    vulkanizer::v2::live::nativeVisibilityProfileEnabled());
                PlayLayer::updateVisibility(dt);
            });
        {
            spike::Scope hookTiming(spike::Phase::visibilityHooks, 0, false, 0,
                vulkanizer::v2::live::nativeVisibilityProfileEnabled());
            vulkanizer::v2::live::afterVisibility(this);
        }
    }
    void updateObjectSection(GameObject* object) {
        SectionTransitionCounter transition(object);
        spike::Scope timing(spike::Phase::nativeSection);
        if (vulkanizer::v2::live::aggressiveActive())
            vulkanizer::v2::live::recordSectionCommit(object);
        PlayLayer::updateObjectSection(object);
    }
    void resetLevel() {
        vulkanizer::v2::live::destroy(this, "native_reset");
        vulkanizer::flushPendingVisualTransforms(this, true);
        vulkanizer::resetRotationPlans();
        PlayLayer::resetLevel();
        vulkanizer::v2::live::beginLevel(this);
    }

    void updateColor(cocos2d::ccColor3B& color, float fadeTime, int colorID, bool blending,
                     float opacity, cocos2d::ccHSVValue& copyHSV, int colorIDToCopy,
                     bool copyOpacity, EffectGameObject* callerObject, int unk1, int unk2) {
        auto& manager = RendererManager::get();
        profileSchedulerCallback(manager, vulkanizer::SchedulerCallbackPhase::playLayerUpdateColor,
            [&] { PlayLayer::updateColor(color, fadeTime, colorID, blending, opacity,
                                         copyHSV, colorIDToCopy, copyOpacity, callerObject, unk1, unk2); });
    }
};

class $modify(VulkanizerNodeVisitPhase, cocos2d::CCNode) {
    void visit() {
        vulkanizer::v2::live::beforeNodeVisit(this);
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            CCNode::visit();
            return;
        }
        const bool topLevel = s_nodeVisitDepth++ == 0;
        if (topLevel) s_nodeVisitCalls = 0;
        ++s_nodeVisitCalls;
        if (!topLevel) {
            CCNode::visit();
            --s_nodeVisitDepth;
            return;
        }
        ++s_sample.nodeVisitRoots;
        auto start = RendererManager::Clock::now();
        ++s_sample.clockPairs;
        CCNode::visit();
        const double elapsed = std::chrono::duration<double, std::milli>(RendererManager::Clock::now() - start).count();
        s_sample.nodeVisitMs += elapsed;
        s_sample.nodeVisitCalls += s_nodeVisitCalls;
        --s_nodeVisitDepth;
    }
};

class $modify(VulkanizerSpriteDrawPhase, cocos2d::CCSprite) {
    void updateTransform() {
        const bool v2Owned = vulkanizer::v2::live::prepareSpriteTransform(this);
        vulkanizer::v2::live::recordSpriteVisit(this, v2Owned);
        if (v2Owned) return;
        if(vulkanizer::resident::owns(this))return;
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            CCSprite::updateTransform();
            return;
        }
        ++s_sample.spriteTransformCalls;
        const bool sampled = (s_spriteTransformCallIndex++ & 0x3f) == 0;
        if (!sampled) {
            CCSprite::updateTransform();
            return;
        }
        auto start = RendererManager::Clock::now();
        ++s_sample.clockPairs;
        CCSprite::updateTransform();
        const double elapsed = std::chrono::duration<double, std::milli>(RendererManager::Clock::now() - start).count();
        ++s_sample.spriteTransformTimedCalls;
        s_sample.spriteTransformEstimatedMs += elapsed * 64.0;
    }

    void draw() {
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            CCSprite::draw();
            return;
        }
        ++s_sample.spriteDrawCalls;
        const bool sampled = (s_spriteDrawCallIndex++ & 0x0f) == 0;
        if (!sampled) {
            CCSprite::draw();
            return;
        }
        auto start = RendererManager::Clock::now();
        ++s_sample.clockPairs;
        CCSprite::draw();
        const double elapsed = std::chrono::duration<double, std::milli>(RendererManager::Clock::now() - start).count();
        ++s_sample.spriteDrawTimedCalls;
        s_sample.spriteDrawMs += elapsed * 16.0;
    }
};

class $modify(VulkanizerSpikeBatchTraversal, cocos2d::CCSpriteBatchNode) {
    static void onModify(auto& self) {
        const auto* enabled = std::getenv("VULKANIZER_V2_SPIKE_AUDIT");
        if (!enabled || std::strcmp(enabled, "1") != 0)
            for (auto* hook : self.getAllHooks()) hook->setAutoEnable(false);
    }
    void visit() {
        spike::Scope timing(spike::Phase::batchVisit);
        CCSpriteBatchNode::visit();
    }
    void sortAllChildren() {
        spike::Scope timing(spike::Phase::batchSort, spike::audit.active ? getChildrenCount() : 0);
        CCSpriteBatchNode::sortAllChildren();
    }
};

class $modify(VulkanizerBatchDrawPhase, cocos2d::CCSpriteBatchNode) {
    void draw() {
        spike::Scope timing(spike::Phase::batchDraw);
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            CCSpriteBatchNode::draw();
            return;
        }
        ++s_sample.batchDrawCalls;
        auto start = RendererManager::Clock::now();
        ++s_sample.clockPairs;
        CCSpriteBatchNode::draw();
        s_sample.batchDrawMs += std::chrono::duration<double, std::milli>(RendererManager::Clock::now() - start).count();
    }
};

class $modify(VulkanizerTextureAtlasPhase, cocos2d::CCTextureAtlas) {
    static void onModify(auto& self) {
        if (vulkanizer::v2::live::requested()) return;
        for (auto* hook : self.getAllHooks())
            if (!hook->getDisplayName().ends_with("CCTextureAtlas::drawNumberOfQuads")) hook->setAutoEnable(false);
    }
    void drawNumberOfQuads(unsigned int count) {
        spike::Scope timing(spike::Phase::atlasDraw, count);
        if (vulkanizer::v2::live::drawAtlas(this, count, 0, [this](unsigned n, unsigned start) { CCTextureAtlas::drawNumberOfQuads(n, start); })) return;
        if(vulkanizer::resident::draw(this,count,0,[this](unsigned n,unsigned start){CCTextureAtlas::drawNumberOfQuads(n,start);}))return;
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            CCTextureAtlas::drawNumberOfQuads(count);
            return;
        }
        ++s_sample.atlasDrawCalls;
        s_sample.atlasQuads += count;
        const bool sampled = (s_atlasDrawCallIndex++ & 0x07) == 0;
        if (!sampled) {
            CCTextureAtlas::drawNumberOfQuads(count);
            return;
        }
        auto start = RendererManager::Clock::now();
        ++s_sample.clockPairs;
        CCTextureAtlas::drawNumberOfQuads(count);
        const double elapsed = std::chrono::duration<double, std::milli>(RendererManager::Clock::now() - start).count();
        ++s_sample.atlasDrawTimedCalls;
        s_sample.atlasDrawMs += elapsed * 8.0;
    }

    void drawNumberOfQuads(unsigned int count, unsigned int startQuad) {
        spike::Scope timing(spike::Phase::atlasDraw, count);
        if (vulkanizer::v2::live::drawAtlas(this, count, startQuad, [this](unsigned n, unsigned start) { CCTextureAtlas::drawNumberOfQuads(n, start); })) return;
        if(vulkanizer::resident::draw(this,count,startQuad,[this](unsigned n,unsigned start){CCTextureAtlas::drawNumberOfQuads(n,start);}))return;
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            CCTextureAtlas::drawNumberOfQuads(count, startQuad);
            return;
        }
        ++s_sample.atlasDrawCalls;
        s_sample.atlasQuads += count;
        const bool sampled = (s_atlasDrawCallIndex++ & 0x07) == 0;
        if (!sampled) {
            CCTextureAtlas::drawNumberOfQuads(count, startQuad);
            return;
        }
        auto start = RendererManager::Clock::now();
        ++s_sample.clockPairs;
        CCTextureAtlas::drawNumberOfQuads(count, startQuad);
        const double elapsed = std::chrono::duration<double, std::milli>(RendererManager::Clock::now() - start).count();
        ++s_sample.atlasDrawTimedCalls;
        s_sample.atlasDrawMs += elapsed * 8.0;
    }
    void updateQuad(ccV3F_C4B_T2F_Quad* quad, unsigned index) {
        // A material callback may submit a stack copy of the old quad. Complete
        // handoff first, then preserve its colors/UVs with freshly native vertices.
        auto copy = *quad;
        if (vulkanizer::v2::live::quadMutation(this, index, quad) && index < getTotalQuads()) {
            const auto current = getQuads()[index];
            copy.bl.vertices = current.bl.vertices;
            copy.br.vertices = current.br.vertices;
            copy.tl.vertices = current.tl.vertices;
            copy.tr.vertices = current.tr.vertices;
            CCTextureAtlas::updateQuad(&copy, index);
            return;
        }
        CCTextureAtlas::updateQuad(quad, index);
    }
    void insertQuad(ccV3F_C4B_T2F_Quad* quad, unsigned index) {
        spike::event(spike::Event::atlasInsert);
        vulkanizer::v2::live::atlasMutation(this, "atlas_insert");
        CCTextureAtlas::insertQuad(quad, index);
    }
    void insertQuads(ccV3F_C4B_T2F_Quad* quads, unsigned index, unsigned amount) {
        spike::event(spike::Event::atlasInsert, amount);
        vulkanizer::v2::live::atlasMutation(this, "atlas_insert_many");
        CCTextureAtlas::insertQuads(quads, index, amount);
    }
    void removeQuadAtIndex(unsigned index) {
        spike::event(spike::Event::atlasRemove);
        vulkanizer::v2::live::atlasMutation(this, "atlas_remove");
        CCTextureAtlas::removeQuadAtIndex(index);
    }
    void removeQuadsAtIndex(unsigned index, unsigned amount) {
        spike::event(spike::Event::atlasRemove, amount);
        vulkanizer::v2::live::atlasMutation(this, "atlas_remove_many");
        CCTextureAtlas::removeQuadsAtIndex(index, amount);
    }
    void insertQuadFromIndex(unsigned from, unsigned to) {
        spike::event(spike::Event::atlasReorder);
        vulkanizer::v2::live::atlasMutation(this, "atlas_reorder");
        CCTextureAtlas::insertQuadFromIndex(from, to);
    }
    void moveQuadsFromIndex(unsigned from, unsigned amount, unsigned to) {
        spike::event(spike::Event::atlasReorder, amount);
        vulkanizer::v2::live::atlasMutation(this, "atlas_move_many");
        CCTextureAtlas::moveQuadsFromIndex(from, amount, to);
    }
    void moveQuadsFromIndex(unsigned from, unsigned to) {
        if (spike::audit.active) spike::event(spike::Event::atlasReorder, from < getTotalQuads() ? getTotalQuads()-from : 0);
        vulkanizer::v2::live::atlasMutation(this, "atlas_move");
        CCTextureAtlas::moveQuadsFromIndex(from, to);
    }
    bool resizeCapacity(unsigned capacity) {
        spike::event(spike::Event::atlasResize);
        vulkanizer::v2::live::atlasMutation(this, "atlas_reallocation");
        return CCTextureAtlas::resizeCapacity(capacity);
    }
    void removeAllQuads() {
        if (spike::audit.active) spike::event(spike::Event::atlasRemove, getTotalQuads());
        vulkanizer::v2::live::atlasMutation(this, "atlas_clear");
        CCTextureAtlas::removeAllQuads();
    }
};

class $modify(VulkanizerV2BatchBoundary, cocos2d::CCSpriteBatchNode) {
    static void onModify(auto& self) {
        if (!vulkanizer::v2::live::requested()) for (auto* hook : self.getAllHooks()) hook->setAutoEnable(false);
    }
    void addChild(CCNode* child, int order, int tag) {
        spike::event(spike::Event::batchAdd);
        vulkanizer::v2::live::atlasMutation(getTextureAtlas(), "batch_child_add");
        CCSpriteBatchNode::addChild(child, order, tag);
    }
    void removeChild(CCNode* child, bool cleanup) {
        spike::event(spike::Event::batchRemove);
        vulkanizer::v2::live::atlasMutation(getTextureAtlas(), "batch_child_remove");
        CCSpriteBatchNode::removeChild(child, cleanup);
    }
    void removeAllChildrenWithCleanup(bool cleanup) {
        spike::event(spike::Event::batchClear);
        vulkanizer::v2::live::atlasMutation(getTextureAtlas(), "batch_children_clear");
        CCSpriteBatchNode::removeAllChildrenWithCleanup(cleanup);
    }
    void reorderChild(CCNode* child, int order) {
        spike::event(spike::Event::batchReorder);
        vulkanizer::v2::live::atlasMutation(getTextureAtlas(), "batch_child_order");
        CCSpriteBatchNode::reorderChild(child, order);
    }
    void setTexture(CCTexture2D* texture) {
        spike::event(spike::Event::batchTexture);
        vulkanizer::v2::live::atlasMutation(getTextureAtlas(), "batch_texture_change");
        CCSpriteBatchNode::setTexture(texture);
    }
};

class $modify(VulkanizerV2SpriteBoundary, cocos2d::CCSprite) {
    static void onModify(auto& self) {
        if (!vulkanizer::v2::live::requested()) for (auto* hook : self.getAllHooks()) hook->setAutoEnable(false);
    }
    void updateColor() {
        nativeVisibilityEvent(spike::Event::spriteColorUpdates);
        const bool owned = vulkanizer::v2::live::owns(this);
        const auto before = getQuad();
        CCSprite::updateColor();
        const auto after = getQuad();
        if (owned && !vulkanizer::v2::live::ownsHeadlessMaterial(typeinfo_cast<GameObject*>(this)) &&
            (std::memcmp(&before.bl.colors, &after.bl.colors, sizeof(ccColor4B)) ||
            std::memcmp(&before.br.colors, &after.br.colors, sizeof(ccColor4B)) ||
            std::memcmp(&before.tl.colors, &after.tl.colors, sizeof(ccColor4B)) ||
            std::memcmp(&before.tr.colors, &after.tr.colors, sizeof(ccColor4B)))) {
            if (!vulkanizer::v2::live::nativeMaterialChanged(this, "native_vertex_color_change"))
                vulkanizer::v2::live::release(typeinfo_cast<GameObject*>(this), "native_vertex_color_change");
        }
    }
    void setSkewX(float value) {
        if (value != getSkewX()) vulkanizer::v2::live::release(typeinfo_cast<GameObject*>(this), "native_skew_x");
        CCSprite::setSkewX(value);
    }
    void setSkewY(float value) {
        if (value != getSkewY()) vulkanizer::v2::live::release(typeinfo_cast<GameObject*>(this), "native_skew_y");
        CCSprite::setSkewY(value);
    }
    void setBatchNode(CCSpriteBatchNode* batch) {
        auto* old = getBatchNode();
        if (batch != old) {
            if (old) nativeVisibilityEvent(spike::Event::spriteBatchDetach);
            if (batch) nativeVisibilityEvent(spike::Event::spriteBatchAttach);
        }
        if (batch != old && vulkanizer::v2::live::aggressiveActive()) {
            const Ref<CCSpriteBatchNode> keepOld(old);
            CCSprite::setBatchNode(batch);
            vulkanizer::v2::live::spriteBatchChanged(this, keepOld.data());
            return;
        }
        if (batch != old) vulkanizer::v2::live::release(typeinfo_cast<GameObject*>(this), "sprite_batch_change");
        CCSprite::setBatchNode(batch);
    }
};
