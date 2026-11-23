#pragma once
#include "v2/Scene.hpp"
#include <stdexcept>
#include <string>

namespace v2test {
using namespace vulkanizer::v2;
inline void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
inline Decoration decoration(double x = 0, double y = 0, std::vector<GroupId> groups = {7}) {
    Decoration result;
    result.position = {x, y};
    result.groups = std::move(groups);
    for (unsigned i = 0; i < 4; ++i) {
        result.quad[i].x = static_cast<float>(x) + static_cast<float>(i % 2);
        result.quad[i].y = static_cast<float>(y) + static_cast<float>(i / 2);
        result.quad[i].u = static_cast<float>(i % 2);
        result.quad[i].v = static_cast<float>(i / 2);
    }
    return result;
}
inline std::vector<Decoration> fixture(std::size_t count, bool overlap) {
    std::vector<Decoration> objects;
    objects.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto object = decoration(4 + i % 400, 3 + i / 400);
        if (overlap && i % 3 == 1) object.groups = {7, 2};
        if (overlap && i % 3 == 2) object.groups = {2};
        objects.push_back(std::move(object));
    }
    return objects;
}
struct Native : NativeSink {
    std::vector<Translation> positions;
    std::vector<bool> visible;
    std::vector<std::uint64_t> orders;
    std::uint64_t visits = 0, handoffs = 0;
    explicit Native(std::span<const Decoration> objects) {
        for (const auto& object : objects) {
            positions.push_back(object.position);
            visible.push_back(object.visible);
        }
        orders.reserve(1024);
    }
    void translate(std::span<const ObjectId> ids, Translation delta, std::uint64_t order) noexcept override {
        for (auto id : ids) { positions[id].x += delta.x; positions[id].y += delta.y; ++visits; }
        orders.push_back(order);
    }
    void materialize(ObjectId id, Translation p, bool visibility) noexcept override {
        positions[id] = p;
        visible[id] = visibility;
        ++handoffs;
    }
    void visibility(ObjectId id, bool visibility) noexcept override { visible[id] = visibility; }
    void reset(ObjectId id, Translation p, bool visibility) noexcept override {
        positions[id] = p;
        visible[id] = visibility;
    }
};
template<class F> inline void rejects(F&& f, const char* message) {
    bool threw = false;
    try { f(); } catch (const std::exception&) { threw = true; }
    require(threw, message);
}
}
