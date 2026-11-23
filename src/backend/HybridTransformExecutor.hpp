#pragma once

#include "TriggerTransformTypes.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace vulkanizer::gpu {

struct HybridBatchMetrics {
    double cpuPrepareMs = 0;
    double uploadMs = 0;
    double dispatchSubmitMs = 0;
    double gpuComputeMs = 0;
    double syncWaitMs = 0;
    double transferMs = 0;
    double totalMs = 0;
    double qpcStageWallMs = 0;
    std::uint64_t stageThreadCycles = 0, fenceThreadCycles = 0;
    std::uint64_t readbackBytes = 0;
    std::uint64_t bufferAllocations = 0;
    std::uint64_t uploadBytes = 0;
    std::uint64_t cachedInputBuffers = 0;
};

class HybridTransformExecutor {
public:
    static HybridTransformExecutor& get();
    void setReadOnlyCachingForBenchmark(bool enabled);

    bool evaluate(std::vector<ObjectState>& states,
                  std::vector<Action> const& actions,
                  std::vector<Span> const& spans,
                  std::vector<std::uint32_t> const& actionIndices,
                  HybridBatchMetrics& metrics,
                  std::string& error, bool measureCpu = false);

private:
    HybridTransformExecutor() = default;
    struct Impl;
    Impl* m_impl = nullptr;
};

} // namespace vulkanizer::gpu
