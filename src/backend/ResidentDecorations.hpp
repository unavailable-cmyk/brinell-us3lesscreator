#pragma once
#include "TriggerTransformTypes.hpp"
#include <functional>
#include <span>
namespace cocos2d {class CCTextureAtlas;class CCSprite;}
class GameObject;
class PlayLayer;
namespace vulkanizer::resident {
using Restore=std::function<void(GameObject*,std::span<const gpu::Action>)>;
bool requested();
bool enqueue(GameObject*,std::span<const gpu::Action>,std::span<const std::uint32_t>,gpu::Span,Restore);
bool owns(cocos2d::CCSprite*);
bool draw(cocos2d::CCTextureAtlas*,unsigned,unsigned,const std::function<void(unsigned,unsigned)>&);
void reset();
void release(GameObject*);
void controlledFixture(PlayLayer*);
}
