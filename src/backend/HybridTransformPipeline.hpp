#pragma once
#include <cstdint>

class GJBaseGameLayer;
namespace cocos2d { class CCArray; struct CCPoint; }

namespace vulkanizer {

bool tryHybridMoveObjects(GJBaseGameLayer* layer, cocos2d::CCArray* objects,
                          double dx, double dy, bool lockPlayerY);
bool tryHybridRotateObjects(GJBaseGameLayer* layer, cocos2d::CCArray* objects,
                            float rotation, cocos2d::CCPoint position,
                            cocos2d::CCPoint offset, bool finished, bool lockObjectRotation);
void invalidateHybridGroupAudit(GJBaseGameLayer* layer);
void beginHybridTransformStep();
void beginHybridTransformFrame();
void endHybridTransformStep();
bool hybridParityProbeActive();
void recordHybridParityCallback(bool finalizer);
void runRotationSelfTestIfRequested();
void recordSchedulerPositionVisits(std::uint64_t visits);

} // namespace vulkanizer
