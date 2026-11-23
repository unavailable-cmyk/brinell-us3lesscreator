#include "FrameProfiler.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>

namespace vulkanizer {
namespace {
constexpr double maximumSampleMs = 600'000.0;
constexpr double timingEpsilonMs = 0.01;
constexpr std::uint64_t maximumCallbackCallsPerFrame = 100'000;
constexpr std::uint64_t maximumVisualCallsPerFrame = 100'000'000;
constexpr std::uint64_t maximumSwapCallsPerFrame = 4'096;

bool validTime(double value, bool allowZero = true) {
    return std::isfinite(value) && value >= (allowZero ? 0.0 : std::numeric_limits<double>::min()) &&
        value <= maximumSampleMs;
}

bool hasTiming(const CallbackFrameSample& callback) {
    return callback.inclusiveTotalMs != 0 || callback.exclusiveTotalMs != 0 ||
        callback.maxInclusiveMs != 0 || callback.maxExclusiveMs != 0;
}

bool isCountOnlyCallback(SchedulerCallbackPhase phase) {
    return phase == SchedulerCallbackPhase::rotateObject;
}

bool isSampledTransformCallback(SchedulerCallbackPhase phase) {
    switch (phase) {
        case SchedulerCallbackPhase::processMoveActions:
        case SchedulerCallbackPhase::moveObjects:
        case SchedulerCallbackPhase::moveUpdateObjectSection:
        case SchedulerCallbackPhase::processRotationActions:
        case SchedulerCallbackPhase::rotateObjects:
        case SchedulerCallbackPhase::rotationUpdateObjectSection:
        case SchedulerCallbackPhase::rotationAddRotation:
        case SchedulerCallbackPhase::rotationUpdateOrientedBox:
            return true;
        default:
            return false;
    }
}

std::string_view callbackWorkUnit(SchedulerCallbackPhase phase) {
    switch (phase) {
        case SchedulerCallbackPhase::processMoveActions: return "move_action_entries";
        case SchedulerCallbackPhase::processRotationActions: return "rotation_action_entries";
        case SchedulerCallbackPhase::moveObjects:
        case SchedulerCallbackPhase::rotateObjects: return "object_visits";
        case SchedulerCallbackPhase::moveUpdateObjectSection:
        case SchedulerCallbackPhase::rotationUpdateObjectSection:
        case SchedulerCallbackPhase::rotationAddRotation:
        case SchedulerCallbackPhase::rotationUpdateOrientedBox: return "object_callbacks";
        default: return "none";
    }
}

struct ValidationIssue {
    std::string_view reason{};
    SchedulerCallbackPhase callback = SchedulerCallbackPhase::count;
    double callbackMs = 0;
    double frameMs = 0;
    std::uint64_t callbackCalls = 0;

    ValidationIssue() = default;
    ValidationIssue(const char* why) : reason(why) {}
    ValidationIssue(std::string_view why) : reason(why) {}
    ValidationIssue(std::string_view why, SchedulerCallbackPhase phase,
                    double durationMs, double frameDurationMs, std::uint64_t invocations = 0)
      : reason(why), callback(phase), callbackMs(durationMs), frameMs(frameDurationMs), callbackCalls(invocations) {}
};

ValidationIssue validateCallback(const CallbackFrameSample& callback,
                                 SchedulerCallbackPhase phase, double frameMs) {
    // Per-object callbacks can legitimately run many times per object when
    // several ordered actions affect a 270k-object level. Keep the tighter
    // sanity limit for scheduler/helper invocations, not object visits.
    const bool perObjectCallback = phase == SchedulerCallbackPhase::moveUpdateObjectSection ||
        phase == SchedulerCallbackPhase::rotationUpdateObjectSection ||
        phase == SchedulerCallbackPhase::rotateObject ||
        phase == SchedulerCallbackPhase::rotationAddRotation ||
        phase == SchedulerCallbackPhase::rotationUpdateOrientedBox;
    const auto maximumCalls = perObjectCallback ? maximumVisualCallsPerFrame : maximumCallbackCallsPerFrame;
    if (callback.calls > maximumCalls)
        return {"callback_count_out_of_range", phase, 0, frameMs, callback.calls};
    if (callback.timedCalls > callback.calls) return "timed_calls_exceed_invocations";
    if (!validTime(callback.inclusiveTotalMs) || !validTime(callback.exclusiveTotalMs) ||
        !validTime(callback.maxInclusiveMs) || !validTime(callback.maxExclusiveMs))
        return "callback_timing_out_of_range";
    if (callback.estimateWeight == 0 || callback.estimateWeight > 64)
        return "callback_estimate_weight_out_of_range";
    if (phase == SchedulerCallbackPhase::timerUpdate) {
        if (callback.estimateWeight != 8) return "timer_sample_weight_mismatch";
    } else if (isCountOnlyCallback(phase)) {
        if (callback.estimateWeight != 1 || callback.timedCalls != 0 || hasTiming(callback))
            return "count_only_callback_has_timing";
    } else if (isSampledTransformCallback(phase)) {
        if (callback.calls == 0) {
            if (callback.estimateWeight != 1 || callback.timedCalls != 0 || hasTiming(callback))
                return "timing_without_callback_invocation";
        } else if (callback.estimateWeight != 64) {
            return "transform_sample_weight_mismatch";
        }
    } else if (callback.estimateWeight != 1 || callback.timedCalls != callback.calls) {
        return "unsampled_callback_timing_mismatch";
    }
    if (callback.workItems > maximumVisualCallsPerFrame)
        return "callback_work_item_count_out_of_range";
    if (callback.calls == 0 && callback.workItems != 0)
        return "work_items_without_callback_invocation";
    if (callback.calls == 0 && hasTiming(callback)) return "timing_without_callback_invocation";
    if (callback.timedCalls == 0 && hasTiming(callback)) return "timing_without_timed_invocation";
    if (callback.maxInclusiveMs > callback.inclusiveTotalMs + timingEpsilonMs ||
        callback.maxExclusiveMs > callback.exclusiveTotalMs + timingEpsilonMs)
        return "callback_max_exceeds_total";
    if (callback.exclusiveTotalMs > callback.inclusiveTotalMs + timingEpsilonMs ||
        callback.maxExclusiveMs > callback.maxInclusiveMs + timingEpsilonMs)
        return "exclusive_exceeds_inclusive";
    if (callback.maxInclusiveMs > frameMs + timingEpsilonMs)
        return {"callback_duration_exceeds_frame", phase, callback.maxInclusiveMs, frameMs};
    return {};
}

ValidationIssue validateSample(double frameMs, double sceneMs, double swapMs, double systemSwapMs,
                               double hookMs, std::uint64_t swapCalls, std::uint64_t systemSwapCalls,
                               std::optional<unsigned> cocosDraws, const FramePhaseSample& phases) {
    if (!validTime(frameMs, false)) return "frame_duration_invalid";
    if (!validTime(sceneMs) || !validTime(swapMs) || !validTime(systemSwapMs) || !validTime(hookMs))
        return "frame_phase_timing_invalid";
    if (swapCalls > maximumSwapCallsPerFrame || systemSwapCalls > maximumSwapCallsPerFrame)
        return "swap_counter_out_of_range";
    if (cocosDraws && *cocosDraws > maximumVisualCallsPerFrame)
        return "cocos_draw_counter_out_of_range";
    if ((swapCalls == 0 && swapMs != 0) || (systemSwapCalls == 0 && systemSwapMs != 0))
        return "swap_timing_without_call";
    if (!validTime(phases.nodeVisitMs) || !validTime(phases.spriteDrawMs) ||
        !validTime(phases.batchDrawMs) || !validTime(phases.atlasDrawMs) ||
        !validTime(phases.spriteTransformEstimatedMs) || !validTime(phases.timerUpdateEstimatedMs))
        return "render_phase_timing_invalid";
    if (phases.scopeStackErrors != 0) return "callback_scope_stack_corrupt";
    if (phases.nodeVisitRoots > maximumCallbackCallsPerFrame ||
        phases.nodeVisitCalls > maximumVisualCallsPerFrame ||
        phases.spriteDrawCalls > maximumVisualCallsPerFrame ||
        phases.spriteDrawTimedCalls > phases.spriteDrawCalls ||
        phases.batchDrawCalls > maximumVisualCallsPerFrame ||
        phases.atlasDrawCalls > maximumVisualCallsPerFrame ||
        phases.atlasDrawTimedCalls > phases.atlasDrawCalls ||
        phases.atlasQuads > maximumVisualCallsPerFrame ||
        phases.spriteTransformCalls > maximumVisualCallsPerFrame ||
        phases.spriteTransformTimedCalls > phases.spriteTransformCalls ||
        phases.timerUpdateCalls > maximumVisualCallsPerFrame ||
        phases.timerUpdateTimedCalls > phases.timerUpdateCalls ||
        phases.clockPairs > maximumVisualCallsPerFrame)
        return "render_phase_counter_out_of_range";
    if (phases.nodeVisitCalls < phases.nodeVisitRoots) return "node_visit_counter_mismatch";
    if ((phases.nodeVisitRoots == 0 && phases.nodeVisitMs != 0) ||
        (phases.spriteDrawTimedCalls == 0 && phases.spriteDrawMs != 0) ||
        (phases.batchDrawCalls == 0 && phases.batchDrawMs != 0) ||
        (phases.atlasDrawTimedCalls == 0 && phases.atlasDrawMs != 0) ||
        (phases.spriteTransformTimedCalls == 0 && phases.spriteTransformEstimatedMs != 0) ||
        (phases.timerUpdateTimedCalls == 0 && phases.timerUpdateEstimatedMs != 0))
        return "timing_without_render_phase_sample";
    for (std::size_t i = 0; i < schedulerCallbackPhaseCount; ++i) {
        const auto phase = static_cast<SchedulerCallbackPhase>(i);
        if (auto issue = validateCallback(phases.callbacks[i], phase, frameMs); !issue.reason.empty())
            return issue;
    }
    return {};
}

std::size_t targetFramesFor(double frameMs) {
    const double fps = 1000.0 / frameMs;
    if (fps > 60.0) return FrameProfiler::highFpsWindowFrames;
    if (fps >= 15.0) return FrameProfiler::normalFpsWindowFrames;
    return FrameProfiler::lowFpsWindowFrames;
}

std::size_t percentileIndex(std::size_t count, double percentile) {
    return std::min(count - 1, static_cast<std::size_t>(std::ceil(percentile * count)) - 1);
}

PhaseSummary summarizeSamples(const std::array<double, FrameProfiler::capacity>& samples,
                              std::size_t count, std::uint64_t calls, std::uint64_t quads = 0) {
    std::array<double, FrameProfiler::capacity> sorted = samples;
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(count));
    PhaseSummary phase;
    phase.meanMs = std::accumulate(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count), 0.0) /
        static_cast<double>(count);
    phase.p95Ms = sorted[percentileIndex(count, 0.95)];
    phase.p99Ms = sorted[percentileIndex(count, 0.99)];
    phase.maxMs = sorted[count - 1];
    phase.callsPerFrame = static_cast<double>(calls) / static_cast<double>(count);
    phase.quadsPerFrame = static_cast<double>(quads) / static_cast<double>(count);
    return phase;
}

void appendJsonString(std::ostringstream& out, std::string_view value) {
    static constexpr char hex[] = "0123456789abcdef";
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u00" << hex[c >> 4] << hex[c & 0x0f];
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
}

void appendPhaseJson(std::ostringstream& out, const PhaseSummary& phase) {
    out << "{\"mean_ms\":" << phase.meanMs
        << ",\"p95_ms\":" << phase.p95Ms
        << ",\"p99_ms\":" << phase.p99Ms
        << ",\"max_ms\":" << phase.maxMs
        << ",\"calls_per_frame\":" << phase.callsPerFrame << '}';
}
} // namespace

std::string_view callbackPhaseName(SchedulerCallbackPhase phase) {
    switch (phase) {
        case SchedulerCallbackPhase::schedulerUpdate: return "CCScheduler::update";
        case SchedulerCallbackPhase::actionManagerUpdate: return "CCActionManager::update";
        case SchedulerCallbackPhase::gameLayerUpdate: return "GJBaseGameLayer::update";
        case SchedulerCallbackPhase::processCommands: return "GJBaseGameLayer::processCommands";
        case SchedulerCallbackPhase::preUpdateVisibility: return "GJBaseGameLayer::preUpdateVisibility";
        case SchedulerCallbackPhase::playLayerUpdateVisibility: return "PlayLayer::updateVisibility";
        case SchedulerCallbackPhase::playLayerUpdateColor: return "PlayLayer::updateColor";
        case SchedulerCallbackPhase::processMoveActions: return "GJBaseGameLayer::processMoveActions";
        case SchedulerCallbackPhase::processMoveActionsStep: return "GJBaseGameLayer::processMoveActionsStep";
        case SchedulerCallbackPhase::moveObjects: return "GJBaseGameLayer::moveObjects";
        case SchedulerCallbackPhase::moveUpdateObjectSection: return "move::GJBaseGameLayer::updateObjectSection";
        case SchedulerCallbackPhase::processRotationActions: return "GJBaseGameLayer::processRotationActions";
        case SchedulerCallbackPhase::rotateObjects: return "GJBaseGameLayer::rotateObjects";
        case SchedulerCallbackPhase::rotateObject: return "GJBaseGameLayer::rotateObject";
        case SchedulerCallbackPhase::rotationUpdateObjectSection: return "rotation::GJBaseGameLayer::updateObjectSection";
        case SchedulerCallbackPhase::rotationAddRotation: return "rotation::GameObject::addRotation";
        case SchedulerCallbackPhase::rotationUpdateOrientedBox: return "rotation::GameObject::updateOrientedBox";
        case SchedulerCallbackPhase::processTransformActions: return "GJBaseGameLayer::processTransformActions";
        case SchedulerCallbackPhase::processAreaActions: return "GJBaseGameLayer::processAreaActions";
        case SchedulerCallbackPhase::processAreaEffects: return "GJBaseGameLayer::processAreaEffects";
        case SchedulerCallbackPhase::timerUpdate: return "CCTimer::update";
        case SchedulerCallbackPhase::count: break;
    }
    return "unknown";
}

std::string serializeFrameSummary(const FrameSummary& summary, std::string_view scene,
                                  std::string_view route) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3)
        << "{\"schema\":" << summary.schema
        << ",\"profiler_abi\":" << summary.profilerAbi
        << ",\"valid\":true,\"scene\":";
    appendJsonString(out, scene);
    out << ",\"route\":";
    appendJsonString(out, route);
    out << ",\"frames\":" << summary.frames
        << ",\"target_frames\":" << summary.targetFrames
        << ",\"partial\":" << (summary.partial ? "true" : "false")
        << ",\"frame_interval\":\"drawScene_end_to_drawScene_end\""
        << ",\"fps_mean\":" << std::setprecision(2) << summary.fps << std::setprecision(3)
        << ",\"frame_ms\":{\"mean\":" << summary.meanMs
        << ",\"p50\":" << summary.p50Ms
        << ",\"p95\":" << summary.p95Ms
        << ",\"p99\":" << summary.p99Ms
        << ",\"max\":" << summary.maxFrameMs << '}'
        << ",\"cpu_work_ms\":{\"draw_scene_inclusive_mean\":" << summary.sceneWallMs
        << ",\"draw_scene_outside_cocos_swap_mean\":" << summary.sceneOutsideSwapMs
        << ",\"hook_wrapper_mean\":" << summary.hookWallMs << '}'
        << ",\"swap_ms\":{\"cocos_mean\":" << summary.swapWallMs
        << ",\"system_mean\":" << summary.systemSwapWallMs
        << ",\"system_calls_total\":" << summary.systemSwapCalls << '}'
        << ",\"measurement\":{\"clock_pairs\":" << summary.clockPairs
        << ",\"clock_pair_overhead_ns\":" << std::setprecision(1) << summary.clockPairOverheadNs
        << ",\"estimated_clock_overhead_ms\":" << std::setprecision(4) << summary.estimatedClockOverheadMs
        << ",\"overhead_scope\":\"clock reads only\"},\"timing_model\":\"transform parent callbacks sampled 1/64; child operations timed inside selected parents; exact calls/work items; transform spans are estimated; exclusive subtracts measured nested spans\""
        << ",\"callbacks\":[";
    bool first = true;
    for (std::size_t i = 0; i < schedulerCallbackPhaseCount; ++i) {
        const auto& callback = summary.callbacks[i];
        if (callback.calls == 0 && callback.timedCalls == 0) continue;
        if (!first) out << ',';
        first = false;
        out << "{\"id\":";
        appendJsonString(out, callbackPhaseName(static_cast<SchedulerCallbackPhase>(i)));
        out << ",\"calls\":" << callback.calls
            << ",\"timed_calls\":" << callback.timedCalls
            << ",\"estimate_weight\":" << callback.estimateWeight
            << ",\"max_is_sampled\":" << (callback.estimateWeight > 1 ? "true" : "false")
            << ",\"inclusive_total_ms\":" << callback.inclusiveTotalMs
            << ",\"exclusive_total_ms\":" << callback.exclusiveTotalMs
            << ",\"max_inclusive_ms\":" << callback.maxInclusiveMs
            << ",\"max_exclusive_ms\":" << callback.maxExclusiveMs
            << ",\"work_items\":" << callback.workItems
            << ",\"work_unit\":";
        appendJsonString(out, callbackWorkUnit(static_cast<SchedulerCallbackPhase>(i)));
        out << '}';
    }
    out << "],\"render_phases\":{\"node_visit\":";
    appendPhaseJson(out, summary.nodeVisit);
    out << ",\"sprite_draw\":";
    appendPhaseJson(out, summary.spriteDraw);
    out << ",\"batch_draw\":";
    appendPhaseJson(out, summary.batchDraw);
    out << ",\"atlas_draw\":";
    appendPhaseJson(out, summary.atlasDraw);
    out << ",\"sprite_transform\":";
    appendPhaseJson(out, summary.spriteTransform);
    out << ",\"timer_update\":";
    appendPhaseJson(out, summary.timerUpdate);
    out << "},\"sample_counts\":{\"node_visit_calls_per_frame\":" << summary.nodeVisitCallsPerFrame
        << ",\"sprite_draw_timed_per_frame\":" << summary.spriteDrawTimedCallsPerFrame
        << ",\"atlas_draw_timed_per_frame\":" << summary.atlasDrawTimedCallsPerFrame
        << ",\"sprite_transform_timed_per_frame\":" << summary.spriteTransformSamplesPerFrame
        << ",\"timer_timed_per_frame\":" << summary.timerUpdateSamplesPerFrame
        << ",\"cocos_draws_per_sample\":" << summary.meanCocosDraws
        << ",\"cocos_draw_samples\":" << summary.cocosSamples
        << "},\"api_calls\":{\"swap_calls\":" << summary.swapCalls
        << ",\"system_swap_calls\":" << summary.systemSwapCalls << "}}";
    return out.str();
}

FrameProfilerUpdate FrameProfiler::invalidate(std::string_view reason,
                                              SchedulerCallbackPhase callback,
                                              double callbackMs, double frameMs,
                                              std::uint64_t callbackCalls) {
    m_captureValid = false;
    m_invalidReason = reason;
    m_invalidCallback = callback;
    m_invalidCallbackMs = callbackMs;
    m_invalidFrameMs = frameMs;
    clearWindow();
    return {std::nullopt, false, true, reason, callback, callbackMs, frameMs, callbackCalls};
}

FrameProfilerUpdate FrameProfiler::add(double frameMs, double sceneMs, double swapMs, double systemSwapMs,
                                       double hookMs, std::uint64_t swapCalls, std::uint64_t systemSwapCalls,
                                       std::optional<unsigned> cocosDraws, FramePhaseSample phases,
                                       double clockPairOverheadNs, AbiTag) {
    if (!m_captureValid) return {std::nullopt, false, false, m_invalidReason,
                                 m_invalidCallback, m_invalidCallbackMs, m_invalidFrameMs, 0};
    if (auto issue = validateSample(frameMs, sceneMs, swapMs, systemSwapMs, hookMs,
                                    swapCalls, systemSwapCalls, cocosDraws, phases);
        !issue.reason.empty())
        return invalidate(issue.reason, issue.callback, issue.callbackMs, issue.frameMs,
                          issue.callbackCalls);
    if (!validTime(clockPairOverheadNs)) return invalidate("clock_calibration_invalid");
    if (m_count != 0 && std::abs(clockPairOverheadNs - m_clockPairOverheadNs) > 0.001)
        return invalidate("clock_calibration_changed_mid_capture");

    const auto requestedFrames = targetFramesFor(frameMs);
    std::optional<FrameSummary> summary;
    if (m_count == 0) {
        m_targetFrames = requestedFrames;
        m_clockPairOverheadNs = clockPairOverheadNs;
    } else if (requestedFrames < m_targetFrames) {
        if (m_count >= minimumPartialFrames) summary = summarize(true);
        clearWindow();
        m_targetFrames = requestedFrames;
        m_clockPairOverheadNs = clockPairOverheadNs;
    }

    addFrame(frameMs, sceneMs, swapMs, systemSwapMs, hookMs, swapCalls, systemSwapCalls,
             cocosDraws, phases, clockPairOverheadNs);
    if (m_count >= m_targetFrames) {
        summary = summarize(false);
        clearWindow();
    }
    return {std::move(summary), true, false, {}};
}

void FrameProfiler::addFrame(double frameMs, double sceneMs, double swapMs, double systemSwapMs,
                             double hookMs, std::uint64_t swapCalls, std::uint64_t systemSwapCalls,
                             std::optional<unsigned> cocosDraws, const FramePhaseSample& phases,
                             double clockPairOverheadNs) {
    const auto index = m_count;
    m_intervals[index] = frameMs;
    m_nodeVisit[index] = phases.nodeVisitMs;
    m_spriteDraw[index] = phases.spriteDrawMs;
    m_batchDraw[index] = phases.batchDrawMs;
    m_atlasDraw[index] = phases.atlasDrawMs;
    m_spriteTransform[index] = phases.spriteTransformEstimatedMs;
    m_timerUpdate[index] = phases.timerUpdateEstimatedMs;
    ++m_count;

    m_scene += sceneMs;
    m_swap += swapMs;
    m_systemSwap += systemSwapMs;
    m_hook += hookMs;
    m_swapCalls += swapCalls;
    m_systemSwapCalls += systemSwapCalls;
    if (cocosDraws) {
        m_draws += *cocosDraws;
        ++m_cocosSamples;
    }
    m_clockPairs += phases.clockPairs;
    m_clockPairOverheadNs = clockPairOverheadNs;
    m_phaseTotals.nodeVisitRoots += phases.nodeVisitRoots;
    m_phaseTotals.nodeVisitCalls += phases.nodeVisitCalls;
    m_phaseTotals.spriteDrawCalls += phases.spriteDrawCalls;
    m_phaseTotals.spriteDrawTimedCalls += phases.spriteDrawTimedCalls;
    m_phaseTotals.batchDrawCalls += phases.batchDrawCalls;
    m_phaseTotals.atlasDrawCalls += phases.atlasDrawCalls;
    m_phaseTotals.atlasDrawTimedCalls += phases.atlasDrawTimedCalls;
    m_phaseTotals.atlasQuads += phases.atlasQuads;
    m_phaseTotals.spriteTransformCalls += phases.spriteTransformCalls;
    m_phaseTotals.spriteTransformTimedCalls += phases.spriteTransformTimedCalls;
    m_phaseTotals.timerUpdateCalls += phases.timerUpdateCalls;
    m_phaseTotals.timerUpdateTimedCalls += phases.timerUpdateTimedCalls;

    for (std::size_t i = 0; i < schedulerCallbackPhaseCount; ++i) {
        const auto& sample = phases.callbacks[i];
        auto& total = m_callbackTotals[i];
        total.calls += sample.calls;
        total.timedCalls += sample.timedCalls;
        total.inclusiveTotalMs += sample.inclusiveTotalMs;
        total.exclusiveTotalMs += sample.exclusiveTotalMs;
        total.maxInclusiveMs = std::max(total.maxInclusiveMs, sample.maxInclusiveMs);
        total.maxExclusiveMs = std::max(total.maxExclusiveMs, sample.maxExclusiveMs);
        total.estimateWeight = std::max(total.estimateWeight, sample.estimateWeight);
        total.workItems += sample.workItems;
    }
}

FrameSummary FrameProfiler::summarize(bool partial) const {
    FrameSummary result;
    result.frames = m_count;
    result.targetFrames = m_targetFrames;
    result.partial = partial;
    result.meanMs = std::accumulate(m_intervals.begin(), m_intervals.begin() + static_cast<std::ptrdiff_t>(m_count), 0.0) /
        static_cast<double>(m_count);
    result.fps = 1000.0 / result.meanMs;
    auto sorted = m_intervals;
    std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(m_count));
    result.p50Ms = sorted[percentileIndex(m_count, 0.50)];
    result.p95Ms = sorted[percentileIndex(m_count, 0.95)];
    result.p99Ms = sorted[percentileIndex(m_count, 0.99)];
    result.maxFrameMs = sorted[m_count - 1];
    const double frameCount = static_cast<double>(m_count);
    result.sceneWallMs = m_scene / frameCount;
    result.swapWallMs = m_swap / frameCount;
    result.systemSwapWallMs = m_systemSwap / frameCount;
    result.sceneOutsideSwapMs = std::max(0.0, result.sceneWallMs - result.swapWallMs);
    result.hookWallMs = m_hook / frameCount;
    result.cocosSamples = m_cocosSamples;
    result.meanCocosDraws = m_cocosSamples ? m_draws / static_cast<double>(m_cocosSamples) : 0;
    result.swapCalls = m_swapCalls;
    result.systemSwapCalls = m_systemSwapCalls;
    result.clockPairs = m_clockPairs;
    result.clockPairOverheadNs = m_clockPairOverheadNs;
    result.estimatedClockOverheadMs = static_cast<double>(m_clockPairs) * m_clockPairOverheadNs / 1'000'000.0;
    result.nodeVisit = summarizeSamples(m_nodeVisit, m_count, m_phaseTotals.nodeVisitRoots);
    result.spriteDraw = summarizeSamples(m_spriteDraw, m_count, m_phaseTotals.spriteDrawCalls);
    result.batchDraw = summarizeSamples(m_batchDraw, m_count, m_phaseTotals.batchDrawCalls);
    result.atlasDraw = summarizeSamples(m_atlasDraw, m_count, m_phaseTotals.atlasDrawCalls, m_phaseTotals.atlasQuads);
    result.spriteTransform = summarizeSamples(m_spriteTransform, m_count, m_phaseTotals.spriteTransformCalls);
    result.timerUpdate = summarizeSamples(m_timerUpdate, m_count, m_phaseTotals.timerUpdateCalls);
    result.nodeVisitCallsPerFrame = static_cast<double>(m_phaseTotals.nodeVisitCalls) / frameCount;
    result.spriteDrawTimedCallsPerFrame = static_cast<double>(m_phaseTotals.spriteDrawTimedCalls) / frameCount;
    result.spriteTransformSamplesPerFrame = static_cast<double>(m_phaseTotals.spriteTransformTimedCalls) / frameCount;
    result.atlasDrawTimedCallsPerFrame = static_cast<double>(m_phaseTotals.atlasDrawTimedCalls) / frameCount;
    result.timerUpdateSamplesPerFrame = static_cast<double>(m_phaseTotals.timerUpdateTimedCalls) / frameCount;
    result.callbacks = m_callbackTotals;
    return result;
}

std::optional<FrameSummary> FrameProfiler::flush() {
    if (!m_captureValid || m_count < minimumPartialFrames) {
        clearWindow();
        return {};
    }
    auto result = summarize(true);
    clearWindow();
    return result;
}

void FrameProfiler::clearWindow() {
    m_count = 0;
    m_targetFrames = 0;
    m_cocosSamples = 0;
    m_scene = m_swap = m_systemSwap = m_hook = m_draws = 0;
    m_clockPairOverheadNs = 0;
    m_clockPairs = 0;
    m_swapCalls = m_systemSwapCalls = 0;
    m_phaseTotals = {};
    m_callbackTotals = {};
}

void FrameProfiler::reset() {
    clearWindow();
    m_captureValid = true;
    m_invalidReason = {};
    m_invalidCallback = SchedulerCallbackPhase::count;
    m_invalidCallbackMs = 0;
    m_invalidFrameMs = 0;
}

} // namespace vulkanizer
