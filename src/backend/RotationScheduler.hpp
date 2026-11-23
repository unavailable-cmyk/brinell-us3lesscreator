#pragma once
class GJBaseGameLayer;
class GameObject;
namespace cocos2d { class CCArray; }
namespace vulkanizer {
// The isolated proof passes, but the live Peaceful checkpoints did not
// establish exact parity. Keep the recipient path unavailable in gameplay.
inline constexpr bool exactDeferredMovesLiveEnabled = false;
bool tryProcessRotationActions(GJBaseGameLayer* layer);
bool tryV2RotationActions(GJBaseGameLayer* layer);
bool runRotationSchedulerProof(bool cpuOnly = false);
void runRotationSchedulerBenchmarks();
bool startSchedulerGameplayTest();
void pollSchedulerGameplayTest();
void resetRotationPlans();
void precompileLevelTransforms(GJBaseGameLayer* layer);
void flushPendingVisualTransforms(GJBaseGameLayer* layer, bool force = false);
void resolveExactDeferredObject(GameObject* object);
void invalidateExactDeferredObject(GameObject* object);
void resolveExactDeferredToggle(GJBaseGameLayer* layer, int group);
void invalidateExactDeferredMoves(GJBaseGameLayer* layer);
void recordTransformCheckpoint(GJBaseGameLayer* layer);
void recordGameplayFrameInput(float dt);
void recordGameplayPhysicsInput(GJBaseGameLayer* layer, float dt);
void recordGameplayFrameComplete();
void prepareVisualMoveDependencies(GJBaseGameLayer* layer);
void enterNativeMoveAttribution();
void leaveNativeMoveAttribution();
bool tryVisualMoveObjects(GJBaseGameLayer* layer, cocos2d::CCArray* objects, double dx, double dy);
}
