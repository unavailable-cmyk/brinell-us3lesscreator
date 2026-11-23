#pragma once
#include "GlRenderer.hpp"
#include <memory>
#include <span>

namespace vulkanizer::v2 {
// Timestamp queries are collected only after availability reports completion.
// They cover the interval from the first atlas draw to presentation submission.
class FrameGpuTimer {
public:
    explicit FrameGpuTimer(const GlRenderer::Resolve&);
    ~FrameGpuTimer();
    void collect();
    void begin();
    void end();
    void abandonContext();
    std::span<const double> samples() const;
    std::uint64_t skipped() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
