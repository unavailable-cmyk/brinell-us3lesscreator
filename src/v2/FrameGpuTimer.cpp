#include "FrameGpuTimer.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <array>
#include <vector>
#include <stdexcept>

namespace vulkanizer::v2 {
struct FrameGpuTimer::Impl {
    void(APIENTRY* gen)(GLsizei, GLuint*) = nullptr;
    void(APIENTRY* remove)(GLsizei, const GLuint*) = nullptr;
    void(APIENTRY* counter)(GLuint, GLenum) = nullptr;
    void(APIENTRY* available)(GLuint, GLenum, GLint*) = nullptr;
    void(APIENTRY* result)(GLuint, GLenum, std::uint64_t*) = nullptr;
    struct Slot { GLuint begin = 0, end = 0; bool pending = false; };
    std::array<Slot, 8> slots;
    Slot* active = nullptr;
    std::vector<double> samples;
    std::uint64_t skipped = 0;
    explicit Impl(const GlRenderer::Resolve& resolve) {
#define LOAD(N, S) N = reinterpret_cast<decltype(N)>(resolve(S)); if (!N) throw std::runtime_error("GPU timestamps unavailable")
        LOAD(gen, "glGenQueries"); LOAD(remove, "glDeleteQueries"); LOAD(counter, "glQueryCounter");
        LOAD(available, "glGetQueryObjectiv"); LOAD(result, "glGetQueryObjectui64v");
#undef LOAD
        samples.reserve(4096);
        for (auto& slot : slots) { gen(1, &slot.begin); gen(1, &slot.end); }
    }
    ~Impl() { for (auto& slot : slots) { if (slot.begin) remove(1, &slot.begin); if (slot.end) remove(1, &slot.end); } }
};
FrameGpuTimer::FrameGpuTimer(const GlRenderer::Resolve& resolve) : m_impl(std::make_unique<Impl>(resolve)) {}
FrameGpuTimer::~FrameGpuTimer() = default;
void FrameGpuTimer::collect() {
    for (auto& slot : m_impl->slots) {
        if (!slot.pending) continue;
        GLint ready = 0;
        m_impl->available(slot.end, 0x8867, &ready);
        if (!ready) continue;
        std::uint64_t first = 0, last = 0;
        m_impl->result(slot.begin, 0x8866, &first);
        m_impl->result(slot.end, 0x8866, &last);
        if (last >= first) {
            auto& samples = m_impl->samples;
            if (samples.size() == 4096) samples.erase(samples.begin(), samples.begin() + 2048);
            samples.push_back((last - first) / 1e6);
        }
        slot.pending = false;
    }
}
void FrameGpuTimer::begin() {
    if (m_impl->active) return;
    for (auto& slot : m_impl->slots) {
        if (slot.pending) continue;
        m_impl->active = &slot;
        m_impl->counter(slot.begin, 0x8e28);
        return;
    }
    ++m_impl->skipped;
}
void FrameGpuTimer::end() {
    if (!m_impl->active) return;
    m_impl->counter(m_impl->active->end, 0x8e28);
    m_impl->active->pending = true;
    m_impl->active = nullptr;
}
void FrameGpuTimer::abandonContext() {
    for (auto& slot : m_impl->slots) { slot.begin = slot.end = 0; slot.pending = false; }
    m_impl->active = nullptr;
}
std::span<const double> FrameGpuTimer::samples() const { return m_impl->samples; }
std::uint64_t FrameGpuTimer::skipped() const { return m_impl->skipped; }
}
