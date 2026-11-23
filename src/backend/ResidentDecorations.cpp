#include "ResidentDecorations.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <windows.h>
#include <GL/gl.h>
#include <cmath>
#include <vector>
#include <cstring>
using namespace geode::prelude;
namespace vulkanizer::resident {namespace {
constexpr GLenum storage=0x90d2;constexpr GLuint slot=6;
#define FN(R,N,A) R(APIENTRY*N)A=nullptr
struct Api{
 FN(GLuint,glCreateShader,(GLenum));FN(void,glShaderSource,(GLuint,GLsizei,const char*const*,const GLint*));FN(void,glCompileShader,(GLuint));FN(void,glGetShaderiv,(GLuint,GLenum,GLint*));FN(void,glDeleteShader,(GLuint));
 FN(GLuint,glCreateProgram,());FN(void,glAttachShader,(GLuint,GLuint));FN(void,glLinkProgram,(GLuint));FN(void,glGetProgramiv,(GLuint,GLenum,GLint*));FN(void,glDeleteProgram,(GLuint));
 FN(void,glGenBuffers,(GLsizei,GLuint*));FN(void,glDeleteBuffers,(GLsizei,const GLuint*));FN(void,glBindBuffer,(GLenum,GLuint));FN(void,glBufferData,(GLenum,std::ptrdiff_t,const void*,GLenum));FN(void,glBindBufferBase,(GLenum,GLuint,GLuint));FN(void,glGetIntegeri_v,(GLenum,GLuint,GLint*));
 FN(GLint,glGetUniformLocation,(GLuint,const char*));FN(void,glUniform2d,(GLint,double,double));FN(void,glUniform2f,(GLint,float,float));FN(void,glUniform1i,(GLint,GLint));FN(void,glUniformMatrix4fv,(GLint,GLsizei,GLboolean,const float*));FN(void,glDispatchCompute,(GLuint,GLuint,GLuint));FN(void,glMemoryBarrier,(GLbitfield));
 FN(void,glGetBufferSubData,(GLenum,std::ptrdiff_t,std::ptrdiff_t,void*));
 bool load(){
#define LOAD(N) N=reinterpret_cast<decltype(N)>(wglGetProcAddress(#N));if(reinterpret_cast<std::uintptr_t>(N)<=3 || reinterpret_cast<std::uintptr_t>(N)==~std::uintptr_t{})return false;
 LOAD(glCreateShader) LOAD(glShaderSource) LOAD(glCompileShader) LOAD(glGetShaderiv) LOAD(glDeleteShader) LOAD(glCreateProgram) LOAD(glAttachShader) LOAD(glLinkProgram) LOAD(glGetProgramiv) LOAD(glDeleteProgram) LOAD(glGenBuffers) LOAD(glDeleteBuffers) LOAD(glBindBuffer) LOAD(glBufferData) LOAD(glBindBufferBase) LOAD(glGetIntegeri_v) LOAD(glGetUniformLocation) LOAD(glUniform2d) LOAD(glUniform2f) LOAD(glUniform1i) LOAD(glUniformMatrix4fv) LOAD(glDispatchCompute) LOAD(glMemoryBarrier)
 LOAD(glGetBufferSubData)
#undef LOAD
 return true;}
}api;
#undef FN
struct State{
 Ref<GameObject> object;Ref<CCTextureAtlas> quad;CCTextureAtlas* source=nullptr;
 HGLRC context=nullptr;GLuint buffer=0,compute=0,render=0;std::uint64_t generation=0;
 double baseX=0,baseY=0,x=0,y=0;std::vector<gpu::Action> pending;Restore restore;
 ccV3F_C4B_T2F_Quad baseQuad{};bool denied=false,drawing=false;
 std::uint64_t avoided=0,uploads=0,draws=0;
}s;
std::uint64_t sceneGeneration=1;
GLuint program(const char* vs,const char* fs){
 GLuint p=api.glCreateProgram();std::vector<GLuint> shaders;
 for(auto [type,text]:{std::pair<GLenum,const char*>{vs?0x8b31u:0x91b9u,vs?vs:fs},std::pair<GLenum,const char*>{0x8b30u,vs?fs:nullptr}}){
  if(!text)continue;GLuint shader=api.glCreateShader(type);api.glShaderSource(shader,1,&text,nullptr);api.glCompileShader(shader);GLint okay=0;api.glGetShaderiv(shader,0x8b81,&okay);
  if(!okay){api.glDeleteShader(shader);for(auto id:shaders)api.glDeleteShader(id);api.glDeleteProgram(p);return 0;}api.glAttachShader(p,shader);shaders.push_back(shader);
 }
 api.glLinkProgram(p);GLint okay=0;api.glGetProgramiv(p,0x8b82,&okay);for(auto id:shaders)api.glDeleteShader(id);if(!okay){api.glDeleteProgram(p);return 0;}return p;
}
struct Bindings{
 GLint program=0,generic=0,indexed=0;
 Bindings(){glGetIntegerv(0x8b8d,&program);glGetIntegerv(0x90d3,&generic);api.glGetIntegeri_v(0x90d3,slot,&indexed);}
 ~Bindings(){api.glBindBufferBase(storage,slot,indexed);api.glBindBuffer(storage,generic);ccGLUseProgram(program);}
};
void handoff(){
 if(s.object){log::info("VULKANIZER_RESIDENT handoff generation={} coverage=1 commits_avoided={} draw_calls={} upload_bytes={} routine_readback_bytes=0 reason=ownership_guard_or_reset",s.generation,s.avoided,s.draws,s.uploads);auto held=s.object;s.object=nullptr;s.restore(held,s.pending);}
 if(s.context==wglGetCurrentContext()){
  if(s.buffer)api.glDeleteBuffers(1,&s.buffer);if(s.compute)api.glDeleteProgram(s.compute);if(s.render)api.glDeleteProgram(s.render);
 }else if(s.buffer)log::warn("Resident context unavailable at cleanup; GL context retains pending resource deletion until process/context destruction");
 s.object=nullptr;s.quad=nullptr;s.buffer=s.compute=s.render=0;s.pending.clear();s.restore={};s.denied=true;
}
bool viewport(GameObject* o,double x,double y){
 auto* parent=o->getParent();if(!parent)return false;
 auto size=CCDirector::sharedDirector()->getWinSize();auto a=parent->convertToNodeSpace({0,0});auto b=parent->convertToNodeSpace({size.width,size.height});
 return x>std::min(a.x,b.x)+64 && x<std::max(a.x,b.x)-64 && y>std::min(a.y,b.y)+64 && y<std::max(a.y,b.y)-64;
}
bool stable(GameObject* o){return o->getBatchNode() && o->getTextureAtlas()==s.source && o->isVisible() &&
 o->m_positionX==s.baseX && o->m_positionY==s.baseY && o->getRotationX()==0 && o->getRotationY()==0 &&
 o->getScaleX()==1 && o->getScaleY()==1 && o->getSkewX()==0 && o->getSkewY()==0 && !o->isFlipX() && !o->isFlipY() &&
 !o->m_colorSprite && !o->m_glowSprite && o->getChildrenCount()==0 &&
 o->getBatchNode()->getShaderProgram()==CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor) && viewport(o,s.x,s.y) && viewport(o,s.baseX,s.baseY);}
}
bool requested(){static const bool value=Mod::get()->getSettingValue<bool>("experimental-resident-rendering");return value && !Loader::get()->isModLoaded("iliashdz.bismuth");}
bool owns(CCSprite* o){return requested() && s.object.data()==o && !s.drawing;}
bool enqueue(GameObject* object,std::span<const gpu::Action> actions,std::span<const std::uint32_t> indices,gpu::Span span,Restore restore){
 if(!requested() || !span.count)return false;
 if(s.object && s.object.data()!=object)return false;
 if(s.denied)return false;
 for(unsigned j=0;j<span.count;++j)if(actions[indices[span.offset+j]].kind!=0){if(s.object)handoff();return false;}
 if(!s.object){
  const int group=std::getenv("VULKANIZER_RESIDENT_GROUP")?std::atoi(std::getenv("VULKANIZER_RESIDENT_GROUP")):Mod::get()->getSettingValue<int>("resident-decoration-group");bool selected=false;
  if(group>0 && object->m_groups)for(short i=0;i<object->m_groupCount;++i)selected|=(*object->m_groups)[i]==group;
  static unsigned reports=0;
  if(selected && std::getenv("VULKANIZER_RESIDENT_FIXTURE") && reports++<3)log::info("VULKANIZER_RESIDENT_CANDIDATE decoration2={} dont_enter={} dont_fade={} batch={} children={} color_sprite={} glow={} visible={} position=({:.2f},{:.2f}) section=({},{}) standard_shader={}",object->m_isDecoration2,object->m_isDontEnter,object->m_isDontFade,object->getBatchNode()!=nullptr,object->getChildrenCount(),object->m_colorSprite!=nullptr,object->m_glowSprite!=nullptr,object->isVisible(),object->m_positionX,object->m_positionY,object->m_outerSectionIndex,object->m_middleSectionIndex,object->getBatchNode() && object->getBatchNode()->getShaderProgram()==CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
  if(!selected || !object->m_isDecoration2 || !object->m_isDontEnter || !object->m_isDontFade || object->m_tempOffsetXRelated || !object->getBatchNode())return false;
  s.baseX=s.x=object->m_positionX;s.baseY=s.y=object->m_positionY;s.source=object->getTextureAtlas();if(!stable(object))return false;
  auto* renderer=reinterpret_cast<const char*>(glGetString(GL_RENDERER));if(!renderer || std::string_view(renderer).find("zink")==std::string_view::npos || !api.load())return false;
  s.context=wglGetCurrentContext();s.generation=sceneGeneration;
  const char* compute=R"(#version 430
layout(local_size_x=1) in;layout(std430,binding=6) buffer State{dvec2 position;};uniform dvec2 delta;
void main(){position.x=position.x+delta.x;position.y=position.y+delta.y;})";
  const char* vertex=R"(#version 430
layout(location=0) in vec3 a_position;layout(location=1) in vec4 a_color;layout(location=2) in vec2 a_texCoord;
layout(std430,binding=6) readonly buffer State{dvec2 position;};uniform vec2 base;uniform mat4 mvp;out vec4 color;out vec2 uv;
void main(){vec3 p=a_position;p.xy+=vec2(position)-base;gl_Position=mvp*vec4(p,1);color=a_color;uv=a_texCoord;})";
  const char* fragment=R"(#version 430
in vec4 color;in vec2 uv;uniform sampler2D texture0;out vec4 outColor;void main(){outColor=texture(texture0,uv)*color;})";
  s.compute=program(nullptr,compute);s.render=program(vertex,fragment);
  if(!s.compute||!s.render){handoff();log::warn("VULKANIZER_RESIDENT unavailable: shader compilation/link failed; legacy retained");return false;}
  object->updateTransform();s.baseQuad=object->getQuad();
  s.quad=CCTextureAtlas::createWithTexture(s.source->getTexture(),1);if(!s.quad){handoff();return false;}
  {Bindings keep;api.glGenBuffers(1,&s.buffer);api.glBindBuffer(storage,s.buffer);double initial[2]{s.x,s.y};api.glBufferData(storage,sizeof(initial),initial,0x88e8);s.uploads+=sizeof(initial);}
  s.object=object;s.restore=std::move(restore);
  log::info("VULKANIZER_RESIDENT acquired generation={} handle=0 object_id={} atlas_index={} coverage=1 supported=translation_only",s.generation,object->m_objectID,object->getAtlasIndex());
 }
 if(s.context!=wglGetCurrentContext() || !stable(object) || s.pending.size()+span.count>512){handoff();return false;}
 auto* layer=PlayLayer::get();if(!layer){handoff();return false;}
 double x=s.x,y=s.y;
 for(unsigned j=0;j<span.count;++j){auto& a=actions[indices[span.offset+j]];x+=a.dx;y+=a.dy;
  if(a.kind!=0 || !std::isfinite(x)||!std::isfinite(y)||!viewport(object,x,y)||x<0||y<0 ||
   static_cast<int>(x*layer->m_sectionXFactor)!=object->m_outerSectionIndex || static_cast<int>(y*layer->m_sectionYFactor)!=object->m_middleSectionIndex){handoff();return false;}
 }
 {Bindings keep;ccGLUseProgram(s.compute);api.glBindBufferBase(storage,slot,s.buffer);
  for(unsigned j=0;j<span.count;++j){const auto& a=actions[indices[span.offset+j]];api.glUniform2d(api.glGetUniformLocation(s.compute,"delta"),a.dx,a.dy);api.glDispatchCompute(1,1,1);api.glMemoryBarrier(0x2000);s.pending.push_back(a);s.uploads+=16;++s.avoided;}
 }
 s.x=x;s.y=y;return true;
}
bool draw(CCTextureAtlas* atlas,unsigned count,unsigned first,const std::function<void(unsigned,unsigned)>& native){
 if(!s.object || s.drawing || atlas!=s.source)return false;
 auto* o=s.object.data();if(!stable(o)||s.context!=wglGetCurrentContext()){handoff();return false;}
 unsigned index=o->getAtlasIndex();if(index<first||index>=first+count)return false;
 auto* batch=o->getBatchNode();if(!batch || batch->getDescendants()->count()<=index || batch->getDescendants()->objectAtIndex(index)!=o){handoff();return false;}
 if(s.draws==0 && std::getenv("VULKANIZER_RESIDENT_VALIDATE")){
  double actual[2]{},expected[2]{s.x,s.y};
  {Bindings keep;api.glMemoryBarrier(0x200);api.glBindBuffer(storage,s.buffer);api.glGetBufferSubData(storage,0,sizeof(actual),actual);}
  if(std::memcmp(actual,expected,sizeof(actual))!=0){log::error("VULKANIZER_RESIDENT_REFERENCE FAIL; restoring CPU state");handoff();return false;}
  log::info("VULKANIZER_RESIDENT_REFERENCE PASS native_ordered_double_additions=true validation_readback_bytes=16 routine_readback_bytes=0");
 }
 s.drawing=true;
 if(index>first)native(index-first,first);
 {Bindings keep;auto quad=o->getQuad();quad.bl.vertices=s.baseQuad.bl.vertices;quad.br.vertices=s.baseQuad.br.vertices;quad.tl.vertices=s.baseQuad.tl.vertices;quad.tr.vertices=s.baseQuad.tr.vertices;
  s.quad->updateQuad(&quad,0);s.uploads+=sizeof(quad);ccGLUseProgram(s.render);api.glBindBufferBase(storage,slot,s.buffer);
  kmMat4 projection,view,mvp;kmGLGetMatrix(KM_GL_PROJECTION,&projection);kmGLGetMatrix(KM_GL_MODELVIEW,&view);kmMat4Multiply(&mvp,&projection,&view);
  api.glUniformMatrix4fv(api.glGetUniformLocation(s.render,"mvp"),1,GL_FALSE,mvp.mat);api.glUniform2f(api.glGetUniformLocation(s.render,"base"),static_cast<float>(s.baseX),static_cast<float>(s.baseY));api.glUniform1i(api.glGetUniformLocation(s.render,"texture0"),0);
  s.quad->drawNumberOfQuads(1,0);++s.draws;
  if(s.draws==1)log::info("VULKANIZER_RESIDENT_DRAW generation={} handle=0 original_quad_suppressed=true persistent_ssbo=true readback_bytes=0 cpu_transform_commits=0",s.generation);
 }
 if(index+1<first+count)native(first+count-index-1,index+1);
 s.drawing=false;return true;
}
void reset(){handoff();s=State{};++sceneGeneration;}
void release(GameObject* object){if(s.object && (!object || s.object.data()==object))handoff();}
void controlledFixture(PlayLayer* layer){
 if(!std::getenv("VULKANIZER_RESIDENT_FIXTURE") || !requested())return;
 if(layer->getGroup(9998)->count()){log::error("Resident fixture group is already occupied");return;}
 int key=0;
 for(auto* object:CCArrayExt<GameObject,false>(layer->m_objects))
  if(object->m_objectType==GameObjectType::Decoration && object->m_classType==GameObjectClassType::Game &&
     !object->m_isTrigger && !object->m_colorSprite && !object->m_glowSprite && object->getChildrenCount()==0){key=object->m_objectID;break;}
 if(!key){log::error("Resident fixture has no native simple decorative prototype");return;}
 auto* object=GameObject::createWithKey(key);if(!object)return;
 object->customSetup();
 object->m_isDontEnter=object->m_isDontFade=object->m_isDecoration2=true;
 object->setScale(1);object->setRotation(0);
 const float x=layer->m_player1?layer->m_player1->getPositionX()+180:400;
 object->setPosition({x,180});object->addToGroup(9998);layer->addObject(object);
 if(!layer->m_objects->containsObject(object))layer->m_objects->addObject(object);
 if(!layer->getGroup(9998)->containsObject(object))layer->addToGroup(object,9998,false);
 if(!layer->getOptimizedGroup(9998)->containsObject(object))layer->getOptimizedGroup(9998)->addObject(object);
 if(object->m_outerSectionIndex<0)layer->addToSection(object);
 if(!object->getParent())object->activateObject();
 _putenv_s("VULKANIZER_RESIDENT_GROUP","9998");
 layer->m_effectManager->createMoveCommand({16,0},9998,1.5f,0,1,false,false,false,false,1,1,0,0);
 log::info("VULKANIZER_RESIDENT_FIXTURE native_object_id={} group=9998 move_x=16 duration=1.5 type={} class={} members={} optimized={} parent={} saved_level_modified=false",key,static_cast<int>(object->m_objectType),static_cast<int>(object->m_classType),layer->getGroup(9998)->count(),layer->getOptimizedGroup(9998)->count(),object->getParent()!=nullptr);
}
}
