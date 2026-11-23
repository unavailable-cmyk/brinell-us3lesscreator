#pragma once
#include "diagnostics/FrameProfiler.hpp"
#include "vulkanizer/BackendCapabilityV1.hpp"
#include <atomic>
#include <chrono>
#include <optional>
#include <string>

namespace vulkanizer {
class RendererManager {
public:
    using Clock = std::chrono::steady_clock;
    static RendererManager& get();
    void initialize();
    api::BackendStateV1 backendCapabilityState() const {
        return m_backendCapabilityState.load(std::memory_order_acquire);
    }
    bool diagnosticsEnabled() const { return m_diagnostics && !m_captureInvalid; }
    void beforeFrame();
    void afterVanillaFrame();
    void recordSwapBuffers(double swapWallMs);
    void recordSystemSwapBuffers(double swapWallMs);
    void recordFrame(Clock::time_point originalStart, Clock::time_point originalEnd);
    void finishFrame(double hookWallMs) {
        if (m_pendingFrame.valid) m_pendingFrame.hookMs = hookWallMs;
    }
    void recordCocosDraws(unsigned draws) { if (diagnosticsEnabled()) m_cocosDraws = draws; }
    void shutdown();
    bool gpuTransformRequested() const { return m_gpuTransformRequested || m_gpuTransformAllRequested; }
    bool gpuTransformAllRequested() const { return m_gpuTransformAllRequested; }
    bool gpuTransformReady() const { return gpuTransformRequested() && m_experimental && m_zinkDetected; }
    int gpuTransformPilotGroup() const { return m_gpuTransformPilotGroup; }
private:
    struct PendingFrame {
        bool valid = false;
        bool gameplay = false;
        double frameMs = 0;
        double sceneMs = 0, swapMs = 0, systemSwapMs = 0, hookMs = 0;
        std::uint64_t swapCalls = 0, systemSwapCalls = 0;
        std::optional<unsigned> cocosDraws;
        FramePhaseSample phases{};
    };
    bool m_diagnostics = false, m_experimental = false;
    bool m_gpuTransformRequested = false, m_gpuTransformAllRequested = false;
    bool m_zinkDetected = false, m_firstFrame = true, m_stopped = false;
    bool m_vulkanProbePassed = false;
    unsigned m_backendValidationFrames = 0;
    std::atomic<api::BackendStateV1> m_backendCapabilityState{api::BackendStateV1::initializationPending};
    int m_gpuTransformPilotGroup = 0;
    bool m_captureInvalid = false;
    bool m_gameplayObserved = false;
    bool m_haveLastFrameEnd = false;
    Clock::time_point m_lastFrameEnd{};
    void* m_playLayer = nullptr;
    PendingFrame m_pendingFrame{};
    FrameProfiler m_profiler;
    double m_clockPairOverheadNs = 0;
    std::optional<unsigned> m_cocosDraws;
    double m_frameSwapWallMs = 0, m_frameSystemSwapWallMs = 0;
    size_t m_frameSwapCalls = 0, m_frameSystemSwapCalls = 0;
    std::string m_backend = "OpenGL API (Vulkan unverified)";
    void publishBackendCapability(api::BackendStateV1 state);
};
}
