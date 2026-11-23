#pragma once
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace vulkanizer::v2 {
// Immutable chain membership is compiled in ascending native object order.
// Sparse word masks avoid sorting millions of candidates at each boundary.
struct MirrorWord { std::uint32_t index; std::uint64_t bits; };
inline void appendMirrorObject(std::vector<MirrorWord>& mask, std::uint32_t object) {
    const auto index = object / 64u;
    if (mask.empty() || mask.back().index != index) mask.push_back({index, 0});
    mask.back().bits |= std::uint64_t{1} << (object % 64u);
}

class VisibilityMirrorWorklist {
    std::vector<std::uint8_t> queued;
    std::vector<std::uint32_t> pending, draining;
    std::vector<std::uint64_t> candidates;

    static constexpr std::uint32_t noCandidate = UINT32_MAX;
    static constexpr unsigned lookaheadCandidates = 8;
    static constexpr std::size_t maxLookaheadWords = 16;

    static std::uint32_t selectBit(std::uint64_t bits, unsigned rank) {
        while (bits && rank) { bits &= bits - 1; --rank; }
        return bits ? static_cast<std::uint32_t>(std::countr_zero(bits)) : noCandidate;
    }

    std::uint32_t candidateAhead(std::size_t currentWord, std::uint64_t remaining) const {
        unsigned rank = lookaheadCandidates - 1;
        auto count = static_cast<unsigned>(std::popcount(remaining));
        if (count > rank)
            return static_cast<std::uint32_t>(currentWord * 64 + selectBit(remaining, rank));
        rank -= count;
        const auto end = std::min(candidates.size(), currentWord + 1 + maxLookaheadWords);
        for (auto word = currentWord + 1; word < end; ++word) {
            const auto bits = candidates[word];
            count = static_cast<unsigned>(std::popcount(bits));
            if (count > rank)
                return static_cast<std::uint32_t>(word * 64 + selectBit(bits, rank));
            rank -= count;
        }
        return noCandidate;
    }

    template<bool Prefetch, class Callback, class PrefetchCallback>
    void consumeImpl(Callback&& callback, PrefetchCallback&& prefetch) {
        // Ascending bit order exactly matches the old full object scan.
        for (std::size_t word = 0; word < candidates.size(); ++word) {
            auto bits = candidates[word]; candidates[word] = 0;
            while (bits) {
                const auto offset = std::countr_zero(bits);
                bits &= bits - 1;
                if constexpr (Prefetch) {
                    const auto ahead = candidateAhead(word, bits);
                    if (ahead != noCandidate) prefetch(ahead);
                }
                callback(static_cast<std::uint32_t>(word * 64 + offset));
            }
        }
        draining.clear();
    }
public:
    void initialize(std::size_t chainCount, std::size_t objectCount) {
        queued.assign(chainCount, 0);
        pending.clear(); draining.clear();
        pending.reserve(chainCount); draining.reserve(chainCount);
        candidates.assign((objectCount + 63) / 64, 0);
    }
    bool mark(std::uint32_t chain) {
        if (queued[chain]) return false;
        pending.push_back(chain); queued[chain] = 1;
        return true;
    }
    bool empty() const { return pending.empty(); }
    std::span<const std::uint32_t> begin() {
        pending.swap(draining);
        for (auto chain : draining) queued[chain] = 0;
        return draining;
    }
    void merge(std::span<const MirrorWord> mask) {
        for (const auto& word : mask) candidates[word.index] |= word.bits;
    }
    void mergeObject(std::uint32_t id) {
        candidates[id / 64u] |= std::uint64_t{1} << (id % 64u);
    }
    template<class Callback> void consume(Callback&& callback) {
        consumeImpl<false>(static_cast<Callback&&>(callback), [](std::uint32_t) {});
    }
    template<class Callback, class PrefetchCallback>
    void consume(Callback&& callback, PrefetchCallback&& prefetch) {
        consumeImpl<true>(static_cast<Callback&&>(callback), static_cast<PrefetchCallback&&>(prefetch));
    }
};
}
