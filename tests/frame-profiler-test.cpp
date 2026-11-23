#include "diagnostics/CpuCallbackScope.hpp"
#include "diagnostics/FrameProfiler.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace {
using namespace vulkanizer;

bool close(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}

FramePhaseSample makeSample() {
    FramePhaseSample sample{};
    sample.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::timerUpdate)].estimateWeight = 8;
    return sample;
}

FramePhaseSample makeMeasuredFrame() {
    auto sample = makeSample();
    sample.nodeVisitMs = .25;
    sample.nodeVisitRoots = 1;
    sample.nodeVisitCalls = 4;
    sample.spriteDrawMs = .16;
    sample.spriteDrawCalls = 16;
    sample.spriteDrawTimedCalls = 1;
    sample.batchDrawMs = .10;
    sample.batchDrawCalls = 1;
    sample.atlasDrawMs = .08;
    sample.atlasDrawCalls = 8;
    sample.atlasDrawTimedCalls = 1;
    sample.atlasQuads = 128;
    sample.spriteTransformEstimatedMs = .64;
    sample.spriteTransformCalls = 64;
    sample.spriteTransformTimedCalls = 1;
    sample.timerUpdateEstimatedMs = .16;
    sample.timerUpdateCalls = 8;
    sample.timerUpdateTimedCalls = 1;
    sample.clockPairs = 10;

    auto set = [&](SchedulerCallbackPhase id, std::uint64_t calls,
                   double inclusive, double exclusive, double maxInclusive, double maxExclusive) {
        auto& callback = sample.callbacks[static_cast<std::size_t>(id)];
        callback.calls = calls;
        callback.timedCalls = calls;
        callback.inclusiveTotalMs = inclusive;
        callback.exclusiveTotalMs = exclusive;
        callback.maxInclusiveMs = maxInclusive;
        callback.maxExclusiveMs = maxExclusive;
    };
    set(SchedulerCallbackPhase::schedulerUpdate, 1, 3.0, .5, 3.0, .5);
    set(SchedulerCallbackPhase::actionManagerUpdate, 1, 1.0, 1.0, 1.0, 1.0);
    set(SchedulerCallbackPhase::gameLayerUpdate, 1, 2.0, .5, 2.0, .5);
    set(SchedulerCallbackPhase::processCommands, 1, .75, .75, .75, .75);
    auto& timer = sample.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::timerUpdate)];
    timer.calls = 8;
    timer.timedCalls = 1;
    timer.inclusiveTotalMs = .16;
    timer.exclusiveTotalMs = .12;
    timer.maxInclusiveMs = .02;
    timer.maxExclusiveMs = .015;
    return sample;
}

int fail(int code, std::string_view message) {
    std::cerr << "FAIL " << code << ": " << message << '\n';
    return code;
}
} // namespace

int main(int argc, char** argv) {
    static_assert(FrameProfiler::capacity == 256);
    static_assert(frameProfilerAbiVersion >= 4);

    // Controlled nested scopes verify total, exclusive, maximum, calls, and scope balance.
    {
        CpuCallbackScopeRecorder recorder;
        FramePhaseSample sample = makeSample();
        CpuCallbackScopeRecorder::Token scheduler, action;
        const auto t0 = CpuCallbackScopeRecorder::Clock::time_point{};
        recorder.begin(scheduler, SchedulerCallbackPhase::schedulerUpdate, t0, sample);
        recorder.begin(action, SchedulerCallbackPhase::actionManagerUpdate, t0 + std::chrono::milliseconds(1), sample);
        recorder.end(action, t0 + std::chrono::milliseconds(4), sample);
        recorder.end(scheduler, t0 + std::chrono::milliseconds(5), sample);
        const auto& outer = sample.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::schedulerUpdate)];
        const auto& child = sample.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::actionManagerUpdate)];
        if (!recorder.balanced() || sample.scopeStackErrors != 0 || sample.clockPairs != 2) return fail(1, "nested scope tracking");
        if (outer.calls != 1 || !close(outer.inclusiveTotalMs, 5) || !close(outer.exclusiveTotalMs, 2) ||
            !close(outer.maxInclusiveMs, 5) || !close(outer.maxExclusiveMs, 2)) return fail(2, "outer inclusive/exclusive measurement");
        if (child.calls != 1 || !close(child.inclusiveTotalMs, 3) || !close(child.exclusiveTotalMs, 3)) return fail(3, "child callback measurement");
    }

    // Synthetic high-FPS workload validates full-size percentiles and callback aggregation.
    {
        FrameProfiler profiler;
        std::optional<FrameSummary> summary;
        for (std::size_t i = 0; i < FrameProfiler::highFpsWindowFrames; ++i) {
            const double frameMs = static_cast<double>(i % 8 + 8);
            auto update = profiler.add(frameMs, frameMs * .5, .25, .1, .01, 1, 1, 4,
                                       makeMeasuredFrame(), 100);
            if (!update.captureValid) return fail(4, "valid synthetic frame was rejected");
            summary = std::move(update.summary);
            if (i + 1 < FrameProfiler::highFpsWindowFrames && summary)
                return fail(5, "high-FPS window ended early");
        }
        if (!summary || summary->frames != 256 || summary->targetFrames != 256 || summary->partial)
            return fail(6, "high-FPS adaptive window");
        if (!close(summary->meanMs, 11.5) || !close(summary->p50Ms, 11.0) ||
            !close(summary->p95Ms, 15.0) || !close(summary->p99Ms, 15.0) || !close(summary->maxFrameMs, 15.0))
            return fail(7, "dynamic frame percentiles");
        const auto& scheduler = summary->callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::schedulerUpdate)];
        if (scheduler.calls != 256 || scheduler.timedCalls != 256 ||
            !close(scheduler.inclusiveTotalMs, 768) || !close(scheduler.exclusiveTotalMs, 128) ||
            !close(scheduler.maxInclusiveMs, 3) || !close(scheduler.maxExclusiveMs, .5))
            return fail(8, "callback count, totals, or max aggregation");
        const auto& timer = summary->callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::timerUpdate)];
        if (timer.calls != 2048 || timer.timedCalls != 256 || timer.estimateWeight != 8 ||
            !close(timer.inclusiveTotalMs, 40.96) || !close(timer.maxInclusiveMs, .02))
            return fail(9, "sampled timer estimates and exact invocation count");
        if (summary->clockPairs != 2560 || !close(summary->clockPairOverheadNs, 100) ||
            !close(summary->estimatedClockOverheadMs, .256))
            return fail(10, "profiler timer overhead estimate");
        if (summary->cocosSamples != 256 || summary->swapCalls != 256 ||
            summary->nodeVisitCallsPerFrame != 4 || summary->spriteTransformSamplesPerFrame != 1)
            return fail(11, "render sample counters");
    }

    // Adaptive tiers and a sudden slowdown must produce a clean partial high-rate
    // window, then a 16-frame low-rate window containing only slow frames.
    {
        FrameProfiler profiler;
        for (int i = 0; i < 8; ++i) {
            auto update = profiler.add(8, 4, .1, 0, 0, 1, 0, {}, makeSample(), 50);
            if (!update.captureValid || update.summary) return fail(12, "normal frames before slowdown");
        }
        auto transition = profiler.add(100, 60, .1, 0, 0, 1, 0, {}, makeSample(), 50);
        if (!transition.captureValid || !transition.summary || !transition.summary->partial ||
            transition.summary->frames != 8 || transition.summary->targetFrames != 256)
            return fail(13, "slowdown flushes the previous high-rate window");
        std::optional<FrameSummary> low;
        for (int i = 1; i < 16; ++i) {
            auto update = profiler.add(100, 60, .1, 0, 0, 1, 0, {}, makeSample(), 50);
            if (!update.captureValid) return fail(14, "low-rate frame rejected");
            low = std::move(update.summary);
        }
        if (!low || low->frames != 16 || low->targetFrames != 16 || low->partial ||
            !close(low->fps, 10) || !close(low->maxFrameMs, 100))
            return fail(15, "low-rate 16-frame window");
    }

    // Medium tier, partial flush, and recovery back to high frame rate.
    {
        FrameProfiler profiler;
        std::optional<FrameSummary> medium;
        for (int i = 0; i < 64; ++i) {
            auto update = profiler.add(20, 10, .2, 0, 0, 1, 0, {}, makeSample(), 50);
            if (!update.captureValid) return fail(16, "medium-rate frame rejected");
            medium = std::move(update.summary);
        }
        if (!medium || medium->targetFrames != 64 || medium->frames != 64 || medium->partial ||
            !close(medium->fps, 50)) return fail(17, "medium-FPS window");
        for (int i = 0; i < 5; ++i) profiler.add(8, 4, .1, 0, 0, 1, 0, {}, makeSample(), 50);
        auto flushed = profiler.flush();
        if (!flushed || !flushed->partial || flushed->frames != 5) return fail(18, "partial shutdown flush");
    }

    // Corrupt counters and inconsistent timing invalidate a capture, with sticky status.
    {
        FrameProfiler profiler;
        auto update = profiler.add(-1, 5, 0, 0, 0, 0, 0, {}, makeSample(), 50);
        if (update.captureValid || update.invalidReason != "frame_duration_invalid")
            return fail(19, "negative frame interval invalidates capture");
    }
    {
        FrameProfiler profiler;
        auto update = profiler.add(10, std::numeric_limits<double>::quiet_NaN(),
                                   0, 0, 0, 0, 0, {}, makeSample(), 50);
        if (update.captureValid || update.invalidReason != "frame_phase_timing_invalid")
            return fail(20, "non-finite frame timing invalidates capture");
    }
    {
        FrameProfiler profiler;
        auto update = profiler.add(10, 5, 0, 0, 0, 4'097, 0, {}, makeSample(), 50);
        if (update.captureValid || update.invalidReason != "swap_counter_out_of_range")
            return fail(21, "impossible swap counter invalidates capture");
    }
    {
        FrameProfiler profiler;
        auto update = profiler.add(10, 5, 0, 0, 0, 0, 0,
                                   std::numeric_limits<unsigned>::max(), makeSample(), 50);
        if (update.captureValid || update.invalidReason != "cocos_draw_counter_out_of_range")
            return fail(22, "impossible Cocos draw counter invalidates capture");
    }
    {
        FrameProfiler profiler;
        auto update = profiler.add(2.5, 2, 0, 0, 0, 0, 0, {}, makeMeasuredFrame(), 50);
        if (update.captureValid || update.invalidReason != "callback_duration_exceeds_frame" ||
            update.invalidCallback != SchedulerCallbackPhase::schedulerUpdate ||
            !close(update.invalidCallbackMs, 3.0) || !close(update.invalidFrameMs, 2.5))
            return fail(27, "impossible callback duration reports callback identity and timing");
    }
    {
        FrameProfiler profiler;
        auto bad = makeSample();
        auto& callback = bad.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::gameLayerUpdate)];
        callback.calls = 1'466'150'000'000ull;
        auto update = profiler.add(10, 5, 0, 0, 0, 1, 0, {}, bad, 50);
        if (update.captureValid || !update.newlyInvalidated || update.invalidReason != "callback_count_out_of_range" ||
            update.invalidCallback != SchedulerCallbackPhase::gameLayerUpdate ||
            update.invalidCallbackCalls != 1'466'150'000'000ull)
            return fail(23, "corrupt callback counters invalidate capture");
        auto later = profiler.add(10, 5, 0, 0, 0, 1, 0, {}, makeSample(), 50);
        if (later.captureValid || later.newlyInvalidated || later.summary)
            return fail(24, "invalid capture remains invalid without emitting statistics");
    }
    {
        FrameProfiler profiler;
        auto bad = makeSample();
        auto& callback = bad.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::gameLayerUpdate)];
        callback.calls = 1;
        callback.timedCalls = 0;
        auto update = profiler.add(10, 5, 0, 0, 0, 1, 0, {}, bad, 50);
        if (update.captureValid || update.invalidReason != "unsampled_callback_timing_mismatch")
            return fail(25, "missing callback timing invalidates capture");
    }
    {
        FrameProfiler profiler;
        auto bad = makeSample();
        bad.scopeStackErrors = 1;
        auto update = profiler.add(10, 5, 0, 0, 0, 1, 0, {}, bad, 50);
        if (update.captureValid || update.invalidReason != "callback_scope_stack_corrupt")
            return fail(26, "scope stack corruption invalidates capture");
    }
    {
        CpuCallbackScopeRecorder recorder;
        auto sample = makeSample();
        CpuCallbackScopeRecorder::Token frame, rotation, addRotation;
        const auto t0 = CpuCallbackScopeRecorder::Clock::time_point{};
        recorder.begin(frame, SchedulerCallbackPhase::gameLayerUpdate, t0, sample);

        auto& rotationCounts = sample.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::processRotationActions)];
        rotationCounts.calls = 64;
        rotationCounts.workItems = 128;
        rotationCounts.estimateWeight = 64;
        recorder.begin(rotation, SchedulerCallbackPhase::processRotationActions,
                       t0 + std::chrono::milliseconds(1), sample, 64, false, 64);

        auto& addCounts = sample.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::rotationAddRotation)];
        addCounts.calls = 4096;
        addCounts.workItems = 4096;
        addCounts.estimateWeight = 64;
        recorder.begin(addRotation, SchedulerCallbackPhase::rotationAddRotation,
                       t0 + std::chrono::milliseconds(2), sample, 64, false, 1);
        recorder.end(addRotation, t0 + std::chrono::milliseconds(3), sample);
        recorder.end(rotation, t0 + std::chrono::milliseconds(5), sample);
        recorder.end(frame, t0 + std::chrono::milliseconds(10), sample);

        const auto& rotationTiming = sample.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::processRotationActions)];
        const auto& addTiming = sample.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::rotationAddRotation)];
        if (!recorder.balanced() || sample.scopeStackErrors != 0 || sample.clockPairs != 3)
            return fail(27, "sampled transform scopes balance");
        if (rotationTiming.calls != 64 || rotationTiming.timedCalls != 1 ||
            !close(rotationTiming.inclusiveTotalMs, 256) ||
            !close(rotationTiming.exclusiveTotalMs, 192))
            return fail(28, "sampled transform parent and exclusive estimate");
        if (addTiming.calls != 4096 || addTiming.timedCalls != 1 ||
            !close(addTiming.inclusiveTotalMs, 64) || addTiming.workItems != 4096)
            return fail(29, "sampled per-object count and estimate");
        FrameProfiler profiler;
        auto update = profiler.add(20, 10, 0, 0, 0, 1, 0, {}, sample, 50);
        if (!update.captureValid) return fail(30, "sampled transform capture validation");
    }

    if (argc == 2 && std::string_view(argv[1]) == "--json-fixture") {
        FrameProfiler profiler;
        std::optional<FrameSummary> summary;
        for (int i = 0; i < 16; ++i) {
            auto frame = makeMeasuredFrame();
            auto& move = frame.callbacks[static_cast<std::size_t>(SchedulerCallbackPhase::processMoveActions)];
            move.calls = 64;
            move.timedCalls = 1;
            move.estimateWeight = 64;
            move.inclusiveTotalMs = 2.0;
            move.exclusiveTotalMs = 1.5;
            move.maxInclusiveMs = .5;
            move.maxExclusiveMs = .375;
            move.workItems = 1024;
            auto update = profiler.add(100, 50, .1, 0, .01, 1, 0, {}, frame, 100);
            if (!update.captureValid) return fail(23, "JSON fixture frame rejected");
            summary = std::move(update.summary);
        }
        if (!summary) return fail(24, "JSON fixture did not complete its low-FPS window");
        std::cout << "FRAME_JSON_FIXTURE "
                  << serializeFrameSummary(*summary, "fixture\"\\\n", "Zink") << '\n';
        return 0;
    }

    std::cout << "Adaptive windows, frame alignment inputs, nested inclusive/exclusive callback timing, exact call counts, sampled maxima, counter invalidation, sticky-invalid captures, and profiler overhead accounting passed\n";
    return 0;
}

