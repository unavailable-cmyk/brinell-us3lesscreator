#include "core/RendererManager.hpp"
#include "backend/HybridTransformPipeline.hpp"
#include "backend/RotationScheduler.hpp"
#include "v2/LiveScene.hpp"
#include "v2/SpikeAudit.hpp"
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>

using namespace geode::prelude;
using vulkanizer::RendererManager;

class $modify(VulkanizerRotationSelfTestMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        Loader::get()->queueInMainThread([] {
            if (vulkanizer::v2::live::startTest()) return;
            vulkanizer::runRotationSelfTestIfRequested();
        });
        return true;
    }
};

class $modify(VulkanizerDirector, cocos2d::CCDirector) {
    void drawScene() {
        // GD computes the director delta before drawScene. Use the same fixed
        // input in both diagnostic runs; do not change native physics substeps.
        if (const float dt = vulkanizer::v2::live::testFixedFrameDelta(); dt > 0.f) {
            m_fDeltaTime = dt;
            m_fActualDeltaTime = dt;
        }
        vulkanizer::v2::live::frameBegin();
        vulkanizer::beginHybridTransformFrame();
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            {
                vulkanizer::v2::spike::Scope timing(vulkanizer::v2::spike::Phase::director);
                CCDirector::drawScene();
            }
            manager.afterVanillaFrame();
            vulkanizer::pollSchedulerGameplayTest();
            vulkanizer::v2::live::frameEnd();
            return;
        }
        auto entry = RendererManager::Clock::now();
        manager.beforeFrame();
        auto originalStart = RendererManager::Clock::now();
        {
            vulkanizer::v2::spike::Scope timing(vulkanizer::v2::spike::Phase::director);
            CCDirector::drawScene();
        }
        auto originalEnd = RendererManager::Clock::now();
        manager.afterVanillaFrame();
        vulkanizer::pollSchedulerGameplayTest();
        manager.recordFrame(originalStart, originalEnd);
        auto exit = RendererManager::Clock::now();
        // Save completed wrapper work for the next sample, including summary logging.
        manager.finishFrame(std::chrono::duration<double, std::milli>(
            (originalStart - entry) + (exit - originalEnd)).count());
        vulkanizer::v2::live::frameEnd();
    }
    void purgeDirector() {
        if (auto* play = PlayLayer::get()) vulkanizer::v2::live::destroy(play, "director_purge");
        RendererManager::get().shutdown();
        CCDirector::purgeDirector();
    }
};

class $modify(VulkanizerPresentation, cocos2d::CCEGLView) {
    void swapBuffers() {
        vulkanizer::v2::spike::Scope timing(vulkanizer::v2::spike::Phase::presentation);
        vulkanizer::v2::live::beforeSwap();
        auto& manager = RendererManager::get();
        if (!manager.diagnosticsEnabled()) {
            CCEGLView::swapBuffers();
            return;
        }
        // Sample before presentation/reset. This is Cocos' counter, not every GL draw.
        manager.recordCocosDraws(::g_uNumberOfDraws);
        auto start = RendererManager::Clock::now();
        CCEGLView::swapBuffers();
        manager.recordSwapBuffers(std::chrono::duration<double, std::milli>(
            RendererManager::Clock::now() - start).count());
    }
};
