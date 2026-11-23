#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vulkanizer {

inline constexpr std::uint32_t frameDiagnosticsSchema = 6;
inline constexpr std::uint32_t frameProfilerAbiVersion = 7;

template <std::uint32_t Version>
struct FrameProfilerAbiTag {};

enum class SchedulerCallbackPhase : std::size_t {
    schedulerUpdate,
    actionManagerUpdate,
    gameLayerUpdate,
    processCommands,
    preUpdateVisibility,
    playLayerUpdateVisibility,
    playLayerUpdateColor,
    processMoveActions,
    processMoveActionsStep,
    moveObjects,
    moveUpdateObjectSection,
    processRotationActions,
    rotateObjects,
    rotateObject,
    rotationUpdateObjectSection,
    rotationAddRotation,
    rotationUpdateOrientedBox,
    processTransformActions,
    processAreaActions,
    processAreaEffects,
    timerUpdate,
    count,
};

inline constexpr std::size_t schedulerCallbackPhaseCount =
    static_cast<std::size_t>(SchedulerCallbackPhase::count);

std::string_view callbackPhaseName(SchedulerCallbackPhase phase);
std::string serializeFrameSummary(const struct FrameSummary& summary,
                                  std::string_view scene, std::string_view route);

struct CallbackFrameSample {
    std::uint64_t calls = 0;
    std::uint64_t timedCalls = 0;
    double inclusiveTotalMs = 0;
    double exclusiveTotalMs = 0;
    double maxInclusiveMs = 0;
    double maxExclusiveMs = 0;
    std::uint32_t estimateWeight = 1;
    std::uint64_t workItems = 0;
};

struct FramePhaseSample {
    double nodeVisitMs = 0;
    double spriteDrawMs = 0;
    double batchDrawMs = 0;
    double atlasDrawMs = 0;
    double spriteTransformEstimatedMs = 0;
    double timerUpdateEstimatedMs = 0;
    std::uint64_t nodeVisitRoots = 0;
    std::uint64_t nodeVisitCalls = 0;
    std::uint64_t spriteDrawCalls = 0;
    std::uint64_t spriteDrawTimedCalls = 0;
    std::uint64_t batchDrawCalls = 0;
    std::uint64_t atlasDrawCalls = 0;
    std::uint64_t atlasDrawTimedCalls = 0;
    std::uint64_t atlasQuads = 0;
    std::uint64_t spriteTransformCalls = 0;
    std::uint64_t spriteTransformTimedCalls = 0;
    std::uint64_t timerUpdateCalls = 0;
    std::uint64_t timerUpdateTimedCalls = 0;
    std::uint64_t clockPairs = 0;
    std::uint64_t scopeStackErrors = 0;
    std::array<CallbackFrameSample, schedulerCallbackPhaseCount> callbacks{};
};

struct PhaseSummary {
    double meanMs = 0;
    double p95Ms = 0;
    double p99Ms = 0;
    double maxMs = 0;
    double callsPerFrame = 0;
    double quadsPerFrame = 0;
};

struct CallbackSummary {
    std::uint64_t calls = 0;
    std::uint64_t timedCalls = 0;
    double inclusiveTotalMs = 0;
    double exclusiveTotalMs = 0;
    double maxInclusiveMs = 0;
    double maxExclusiveMs = 0;
    std::uint32_t estimateWeight = 1;
    std::uint64_t workItems = 0;
};

struct FrameSummary {
    std::uint32_t schema = frameDiagnosticsSchema;
    std::uint32_t profilerAbi = frameProfilerAbiVersion;
    std::size_t frames = 0;
    std::size_t targetFrames = 0;
    bool partial = false;
    double meanMs = 0;
    double fps = 0;
    double p50Ms = 0;
    double p95Ms = 0;
    double p99Ms = 0;
    double maxFrameMs = 0;
    double sceneWallMs = 0;
    double swapWallMs = 0;
    double systemSwapWallMs = 0;
    double sceneOutsideSwapMs = 0;
    double hookWallMs = 0;
    double meanCocosDraws = 0;
    double clockPairOverheadNs = 0;
    double estimatedClockOverheadMs = 0;
    std::uint64_t clockPairs = 0;
    std::uint64_t cocosSamples = 0;
    std::uint64_t swapCalls = 0;
    std::uint64_t systemSwapCalls = 0;
    PhaseSummary nodeVisit;
    PhaseSummary spriteDraw;
    PhaseSummary batchDraw;
    PhaseSummary atlasDraw;
    PhaseSummary spriteTransform;
    PhaseSummary timerUpdate;
    double nodeVisitCallsPerFrame = 0;
    double spriteDrawTimedCallsPerFrame = 0;
    double spriteTransformSamplesPerFrame = 0;
    double atlasDrawTimedCallsPerFrame = 0;
    double timerUpdateSamplesPerFrame = 0;
    std::array<CallbackSummary, schedulerCallbackPhaseCount> callbacks{};
};

struct FrameProfilerUpdate {
    std::optional<FrameSummary> summary;
    bool captureValid = true;
    bool newlyInvalidated = false;
    std::string_view invalidReason{};
    SchedulerCallbackPhase invalidCallback = SchedulerCallbackPhase::count;
    double invalidCallbackMs = 0;
    double invalidFrameMs = 0;
    std::uint64_t invalidCallbackCalls = 0;
};

class FrameProfiler {
public:
    static constexpr std::size_t capacity = 256;
    static constexpr std::size_t highFpsWindowFrames = 256;
    static constexpr std::size_t normalFpsWindowFrames = 64;
    static constexpr std::size_t lowFpsWindowFrames = 16;
    static constexpr std::size_t minimumPartialFrames = 4;
    using AbiTag = FrameProfilerAbiTag<frameProfilerAbiVersion>;

    FrameProfilerUpdate add(double frameMs, double sceneMs, double swapMs, double systemSwapMs,
                            double hookMs, std::uint64_t swapCalls, std::uint64_t systemSwapCalls,
                            std::optional<unsigned> cocosDraws, FramePhaseSample phases = {},
                            double clockPairOverheadNs = 0, AbiTag = {});
    std::optional<FrameSummary> flush();
    void reset();

private:
    FrameProfilerUpdate invalidate(std::string_view reason,
                                   SchedulerCallbackPhase callback = SchedulerCallbackPhase::count,
                                   double callbackMs = 0, double frameMs = 0,
                                   std::uint64_t callbackCalls = 0);
    void clearWindow();
    void addFrame(double frameMs, double sceneMs, double swapMs, double systemSwapMs,
                  double hookMs, std::uint64_t swapCalls, std::uint64_t systemSwapCalls,
                  std::optional<unsigned> cocosDraws, const FramePhaseSample& phases,
                  double clockPairOverheadNs);
    FrameSummary summarize(bool partial) const;

    std::array<double, capacity> m_intervals{};
    std::array<double, capacity> m_nodeVisit{};
    std::array<double, capacity> m_spriteDraw{};
    std::array<double, capacity> m_batchDraw{};
    std::array<double, capacity> m_atlasDraw{};
    std::array<double, capacity> m_spriteTransform{};
    std::array<double, capacity> m_timerUpdate{};
    std::size_t m_count = 0;
    std::size_t m_targetFrames = 0;
    std::uint64_t m_cocosSamples = 0;
    double m_scene = 0;
    double m_swap = 0;
    double m_systemSwap = 0;
    double m_hook = 0;
    double m_draws = 0;
    double m_clockPairOverheadNs = 0;
    std::uint64_t m_clockPairs = 0;
    std::uint64_t m_swapCalls = 0;
    std::uint64_t m_systemSwapCalls = 0;
    FramePhaseSample m_phaseTotals{};
    std::array<CallbackSummary, schedulerCallbackPhaseCount> m_callbackTotals{};
    bool m_captureValid = true;
    std::string_view m_invalidReason{};
    SchedulerCallbackPhase m_invalidCallback = SchedulerCallbackPhase::count;
    double m_invalidCallbackMs = 0;
    double m_invalidFrameMs = 0;
};

} // namespace vulkanizer
