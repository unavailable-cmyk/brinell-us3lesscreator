#pragma once

#include "FrameProfiler.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace vulkanizer {

class CpuCallbackScopeRecorder {
public:
    using Clock = std::chrono::steady_clock;

    struct Token {
        SchedulerCallbackPhase phase = SchedulerCallbackPhase::count;
        Clock::time_point start{};
        double childInclusiveMs = 0;
        std::uint32_t estimateWeight = 1;
        std::uint32_t parentEstimateWeight = 1;
        Token* parent = nullptr;
        bool active = false;
    };

    void begin(Token& token, SchedulerCallbackPhase phase, Clock::time_point start,
               FramePhaseSample& sample, std::uint32_t estimateWeight = 1,
               bool countInvocation = true, std::uint32_t parentEstimateWeight = 1) {
        const auto index = static_cast<std::size_t>(phase);
        if (token.active || index >= schedulerCallbackPhaseCount || estimateWeight == 0 ||
            parentEstimateWeight == 0) {
            ++sample.scopeStackErrors;
            return;
        }
        token.phase = phase;
        token.start = start;
        token.childInclusiveMs = 0;
        token.estimateWeight = estimateWeight;
        token.parentEstimateWeight = parentEstimateWeight;
        token.parent = m_top;
        token.active = true;
        m_top = &token;

        auto& callback = sample.callbacks[index];
        if (countInvocation) ++callback.calls;
        ++callback.timedCalls;
        callback.estimateWeight = estimateWeight;
        ++sample.clockPairs;
        if (phase == SchedulerCallbackPhase::timerUpdate)
            ++sample.timerUpdateTimedCalls;
    }

    void end(Token& token, Clock::time_point end, FramePhaseSample& sample) {
        if (!token.active || m_top != &token) {
            ++sample.scopeStackErrors;
            return;
        }
        const double inclusiveMs = std::chrono::duration<double, std::milli>(end - token.start).count();
        const double exclusiveMs = std::max(0.0, inclusiveMs - token.childInclusiveMs);
        m_top = token.parent;
        token.active = false;
        if (token.parent)
            token.parent->childInclusiveMs += inclusiveMs * token.parentEstimateWeight;

        auto& callback = sample.callbacks[static_cast<std::size_t>(token.phase)];
        const double weight = static_cast<double>(token.estimateWeight);
        callback.inclusiveTotalMs += inclusiveMs * weight;
        callback.exclusiveTotalMs += exclusiveMs * weight;
        callback.maxInclusiveMs = std::max(callback.maxInclusiveMs, inclusiveMs);
        callback.maxExclusiveMs = std::max(callback.maxExclusiveMs, exclusiveMs);
        if (token.phase == SchedulerCallbackPhase::timerUpdate)
            sample.timerUpdateEstimatedMs += inclusiveMs * weight;
    }

    bool balanced() const { return m_top == nullptr; }

private:
    Token* m_top = nullptr;
};

} // namespace vulkanizer
