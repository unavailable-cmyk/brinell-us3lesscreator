#include <Geode/Geode.hpp>
#include "core/RendererManager.hpp"

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("Vulkanizer initialized");
    log::info("VULKANIZER_BUILD {} {} transform_pipeline=cohort-culling-1", __DATE__, __TIME__);
    log::info("Vulkanizer version: {}", Mod::get()->getVersion().toVString());
    log::info("Geode runtime: {}; Geometry Dash: {}; SDK target: {}",
        Loader::get()->getVersion().toVString(), Loader::get()->getGameVersion(),
        GEODE_GD_VERSION_STRING);
    vulkanizer::RendererManager::get().initialize();
}
