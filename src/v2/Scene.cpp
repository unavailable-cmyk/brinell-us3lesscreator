#include "Scene.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace brinell::v2 {

namespace {
bool finite(Translation value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::abs(value.x) <= static_cast<double>(std::numeric_limits<float>::max()) &&
        std::abs(value.y) <= static_cast<double>(std::numeric_limits<float>::max());
}
}

Scene Scene::compile(std::span<const Decoration> decorations) {
    Scene scene;
    scene.m_objects.assign(decorations.begin(), decorations.end());

    for (const auto& decoration : scene.m_objects) {
        if (!finite(decoration.base))
            throw std::invalid_argument("Invalid decoration position");
        if (!decoration.eligible)
            continue;

        auto [found, inserted] = scene.m_chainByGroup.try_emplace(
            decoration.group, scene.m_chains.size());
        if (inserted)
            scene.m_chains.push_back(Chain{});
        scene.m_chains[found->second].objects.push_back(decoration.id);
    }

    return scene;
}

Translation Scene::position(std::uint32_t id) const {
    for (const auto& decoration : m_objects) {
        if (decoration.id != id)
            continue;
        if (!decoration.visible)
            return decoration.base;

        auto found = m_chainByGroup.find(decoration.group);
        if (found == m_chainByGroup.end() || !decoration.eligible)
            return decoration.base;
        const auto& chain = m_chains[found->second];
        return {decoration.base.x + chain.translation.x,
                decoration.base.y + chain.translation.y};
    }
    throw std::out_of_range("Unknown decoration id");
}

void Scene::translate(std::uint32_t group, Translation delta) {
    if (!finite(delta))
        throw std::invalid_argument("Invalid translation");

    auto found = m_chainByGroup.find(group);
    if (found == m_chainByGroup.end())
        return;

    auto& chain = m_chains[found->second];
    const Translation next{chain.translation.x + delta.x,
                           chain.translation.y + delta.y};
    if (!finite(next))
        throw std::overflow_error("Translation exceeds renderable range");
    chain.translation = next;
}

std::size_t Scene::sharedChainCount() const {
    return m_chains.size();
}

std::size_t Scene::objectCount() const {
    return m_objects.size();
}

}
