#pragma once

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace brinell::v2 {

struct Translation {
    double x = 0.0;
    double y = 0.0;
};

struct Decoration {
    std::uint32_t id = 0;
    std::uint32_t group = 0;
    Translation base;
    bool visible = true;
    bool eligible = true;
};

class Scene {
public:
    static Scene compile(std::span<const Decoration> decorations);

    Translation position(std::uint32_t id) const;
    void translate(std::uint32_t group, Translation delta);
    std::size_t sharedChainCount() const;
    std::size_t objectCount() const;

private:
    struct Chain {
        Translation translation;
        std::vector<std::uint32_t> objects;
    };

    std::vector<Decoration> m_objects;
    std::vector<Chain> m_chains;
    std::unordered_map<std::uint32_t, std::size_t> m_chainByGroup;
};

}
