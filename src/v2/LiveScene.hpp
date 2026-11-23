#pragma once
#include <functional>
#include <cstdint>
#include <cstddef>

class GJBaseGameLayer;
class PlayLayer;
class GameObject;
namespace cocos2d { class CCArray; class CCNode; class CCSprite; class CCSpriteBatchNode; class CCTextureAtlas; }

namespace vulkanizer::v2::live {
enum class CachedMoveSectionResult : std::uint8_t { notApplicable, skip, native };
enum class MovePolicyInvalidation : std::uint8_t {
    layerGroupAdd, layerGroupRemove, groupParentAdd, groupParentRemove,
    groupParentSet, groupParentRemoveOne, groupParentPromote, groupParentLoad,
    objectType, startPosition, objectGroupAdd, objectGroupRemove, objectReset,
    retirement, sceneDestroy, count
};
bool requested();
bool nativeVisibilityProfileEnabled();
bool moveSectionsRequested();
bool cachedPoliciesRequested();
// Attribution is scoped to the synchronous native hook; no per-object timer.
inline thread_local const char* auditPhase = "other";
struct AuditPhaseScope {
    const char* previous;
    explicit AuditPhaseScope(const char* phase) : previous(auditPhase) { auditPhase = phase; }
    ~AuditPhaseScope() { auditPhase = previous; }
    AuditPhaseScope(const AuditPhaseScope&) = delete;
    AuditPhaseScope& operator=(const AuditPhaseScope&) = delete;
};
bool auditRequested();
// Synchronous native mutation boundary. Unrelated unsupported operations
// still use the ordinary ownership handoff.
struct NativeEditScope {
    void* token = nullptr;
    NativeEditScope();
    ~NativeEditScope();
    NativeEditScope(const NativeEditScope&) = delete;
    NativeEditScope& operator=(const NativeEditScope&) = delete;
};
bool retainNativeEdit(GameObject*);
bool retainNativeEditArray(cocos2d::CCArray*);
// The scheduler owns only the command submission; all resident recipients
// are excluded from its native body.
bool groupRotationActive(GJBaseGameLayer*);
bool groupRotationClaimActive(GJBaseGameLayer*);
bool hyperAggressiveActive();
cocos2d::CCArray* submitPivotlessRotation(GJBaseGameLayer*, cocos2d::CCArray*, float orientation, int group);
void recordPivotlessNativeCalls(std::uint64_t count);
void setGroupRotationClaim(GJBaseGameLayer*, bool);
cocos2d::CCArray* submitGroupRotation(GJBaseGameLayer*, cocos2d::CCArray*,
    double centerX, double centerY, double dx, double dy,
    float a, float b, float c, float d, float tx, float ty, float orientation, bool finished);
void beforeSectionRegistration(GJBaseGameLayer*, GameObject*);
void recordAuditSectionSample(double milliseconds);
void recordAuditRotationArray(cocos2d::CCArray*);
void recordSpikeRotationArray(cocos2d::CCArray*);
bool owns(cocos2d::CCSprite*);
bool owns(GameObject*);
enum class NativeCallerKind : std::uint8_t { positionSetter, textureRect, opacity };
bool nativeCallersRequested();
bool revocationAuditRequested();
bool positionSetterAuditRequested();
struct PositionSetterAuditToken { void* scene = nullptr; std::size_t index = 0; };
PositionSetterAuditToken beginPositionSetterAudit(GameObject*, float x, float y, const void* callerAnchor);
void beforePositionSetterNative(PositionSetterAuditToken, GameObject*, float x, float y);
bool retainPositionSpriteSync(PositionSetterAuditToken, GameObject*, float x, float y);
void endPositionSetterAudit(PositionSetterAuditToken, GameObject*);
void recordNativeCaller(GameObject*, NativeCallerKind);
bool ownsHeadlessMaterial(GameObject*);
bool aggressiveActive();
bool prepareSpriteTransform(cocos2d::CCSprite*);
void beforeNodeVisit(cocos2d::CCNode*);
bool nativeMaterialChanged(cocos2d::CCSprite*, const char* reason);
void spriteBatchChanged(cocos2d::CCSprite*, cocos2d::CCSpriteBatchNode* oldBatch);
void beforeMembershipMutation(GJBaseGameLayer*, GameObject*, int group, const char* reason);
void beforeNativeGroupTransform(GJBaseGameLayer*, int group, bool optimized, const char* reason);
void beforeNativeFollow(GJBaseGameLayer*);
void afterSectionRegistration(GJBaseGameLayer*, GameObject*);
void beforeHistoryRead(GameObject*);
void beforeResidentRead(GameObject*, const char* reason);
bool retainVisibility(GameObject*, bool visible);
bool retainActivation(GameObject*);
bool activeFor(GJBaseGameLayer*);
void releaseArray(cocos2d::CCArray*, const char* reason);
void beginLevel(PlayLayer*, double nativeInitMs = 0);
void recordLegacyPrecompile(PlayLayer*, double milliseconds);
void destroy(PlayLayer*, const char* reason);
void beforeVisibility(PlayLayer*);
void afterVisibility(PlayLayer*);
void beforeDependencies(GJBaseGameLayer*);
void beforeToggle(GJBaseGameLayer*, int group);
void release(GameObject*, const char* reason, const void* callerAnchor = nullptr);
void releaseAll(const char* reason);
void atlasMutation(cocos2d::CCTextureAtlas*, const char* reason);
bool quadMutation(cocos2d::CCTextureAtlas*, unsigned slot, const void* quad);
bool move(GJBaseGameLayer*, cocos2d::CCArray*, double dx, double dy, bool lockPlayerY,
          const std::function<void(cocos2d::CCArray*)>& native);
bool drawAtlas(cocos2d::CCTextureAtlas*, unsigned count, unsigned first,
               const std::function<void(unsigned, unsigned)>& native);
void recordSpriteVisit(cocos2d::CCSprite*, bool suppressed);
void recordSectionCommit(GameObject*);
bool canSkipMoveSection(GJBaseGameLayer*, GameObject*);
CachedMoveSectionResult cachedMoveSection(GJBaseGameLayer*, GameObject*);
void invalidateMoveSectionPolicy(GameObject*, MovePolicyInvalidation);
void invalidateAllMoveSectionPolicies(MovePolicyInvalidation);
void beforeObjectRetirement(GameObject*);
void frameBegin();
void frameEnd();
void beforeSwap();
bool startTest();
void physicsStep(PlayLayer*, float dt, bool visibleFrame);
// Opt-in fixed clock for the independent setter-shadow validation only.
float testFixedFrameDelta();
}
