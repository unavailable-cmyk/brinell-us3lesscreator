#pragma once
#include <algorithm>
#include <cstdint>

namespace vulkanizer {
// Compare CPU wall time spent in the transform pipeline per logical visit,
// never frame rate. Alternating observations share the actual workload rather
// than an invented estimate of how much work culling saved.
struct AdaptiveCulling {
    bool enabled = true, baselineMeasured = false, haveCull = false;
    unsigned frames = 0, bypassFrames = 0;
    double elapsedMs = 0, cullCost = 0, baselineCost = 0;
    std::uint64_t startVisits = 0;
    bool started = false;
    void record(double elapsed, std::uint64_t visits, bool frame) {
        if (!started) { startVisits = visits; started = true; }
        elapsedMs += elapsed;
        if (!frame || ++frames < 120) return;
        const auto work = visits - startVisits;
        if (work >= 10000) {
            const double cost = elapsedMs / static_cast<double>(work);
            if (enabled) {
                cullCost = cost; haveCull = true;
                // Always obtain an actual batching-only measurement first.
                enabled = false; bypassFrames = 0;
            } else {
                baselineCost = cost; baselineMeasured = true;
                bypassFrames += frames;
                // Require a meaningful advantage before enabling culling.
                if (haveCull && cullCost < baselineCost * 0.90) { enabled = true; bypassFrames = 0; }
                else if (bypassFrames >= 960) { enabled = true; bypassFrames = 0; }
            }
        }
        frames = 0; elapsedMs = 0; startVisits = visits;
    }
};
}
