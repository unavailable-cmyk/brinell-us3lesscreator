#pragma once
#include "TriggerTransformTypes.hpp"
#include <span>
#include <cstdint>

namespace vulkanizer {
// Borrowed, synchronous view of the scheduler output. No GD pointers, host
// transform snapshots, transport or readback are required by this contract.
// Handles are scene-local indices, not persistent IDs. Mutation of the backing
// vectors invalidates the view; an asynchronous consumer must own a copy/lease.
struct OrderedVisualPlan {
    std::span<const gpu::Action> actions;
    std::span<const gpu::Span> objectSpans;
    std::span<const std::uint32_t> actionIndices;
    std::span<const std::uint32_t> objectHandles;

    template<class Step> void forObject(std::size_t object, Step&& step) const {
        const auto span=objectSpans[object];
        for(std::uint32_t j=0;j<span.count;++j)
            step(actions[actionIndices[span.offset+j]]);
    }
    template<class State,class Step>
    void evaluate(std::span<State> states,Step&& step) const {
        for(std::size_t i=0;i<states.size();++i)
            forObject(i,[&](const gpu::Action& action){step(states[i],action);});
    }
};
}
