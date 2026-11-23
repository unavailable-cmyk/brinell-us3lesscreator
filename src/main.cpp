#include <Geode/Geode.hpp>

using namespace geode::prelude;

$on_mod(Loaded) {
    log::info("Brinell initialized");
    log::info("BRINELL_BUILD {} {} cpu_optimization=enabled", __DATE__, __TIME__);
    log::info("Brinell version: {}", Mod::get()->getVersion().toVString());
}
