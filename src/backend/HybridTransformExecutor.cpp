#define NOMINMAX
#include "HybridTransformExecutor.hpp"
#include "TriggerTransformShader.hpp"

#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <cstring>
#include <vector>

namespace vulkanizer::gpu {
namespace {
using Clock = std::chrono::steady_clock;
constexpr GLenum glMajorVersion = 0x821B;
constexpr GLenum glMinorVersion = 0x821C;
constexpr GLenum glComputeShader = 0x91B9;
constexpr GLenum glShaderStorageBuffer = 0x90D2;
constexpr GLenum glShaderStorageBufferBinding = 0x90D3;
constexpr GLenum glDynamicCopy = 0x88EA;
constexpr GLenum glCompileStatus = 0x8B81;
constexpr GLenum glLinkStatus = 0x8B82;
constexpr GLenum glTimeElapsed = 0x88BF;
constexpr GLenum glCurrentQuery = 0x8865;
constexpr GLenum glQueryResult = 0x8866;
constexpr GLenum glCurrentProgram = 0x8B8D;
constexpr GLenum glSyncGpuCommandsComplete = 0x9117;
constexpr GLenum glAlreadySignaled = 0x911A;
constexpr GLenum glConditionSatisfied = 0x911C;
constexpr GLenum glTimeoutExpired = 0x911B;
constexpr GLenum glWaitFailed = 0x911D;
constexpr GLbitfield glSyncFlushCommandsBit = 0x00000001;
constexpr GLuint64 glClientWaitBudgetNs = 10'000'000;
constexpr GLbitfield glShaderStorageBarrierBit = 0x2000;
constexpr GLbitfield glBufferUpdateBarrierBit = 0x0200;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLuint64 = std::uint64_t;
using GLsync = void*;

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

template <class T>
T loadGl(char const* name) {
    PROC address = wglGetProcAddress(name);
    auto numeric = reinterpret_cast<std::uintptr_t>(address);
    if (!address || numeric <= 3 || numeric == static_cast<std::uintptr_t>(-1)) {
        auto module = GetModuleHandleW(L"opengl32.dll");
        if (module) address = GetProcAddress(module, name);
    }
    numeric = reinterpret_cast<std::uintptr_t>(address);
    if (!address || numeric <= 3 || numeric == static_cast<std::uintptr_t>(-1))
        return nullptr;
    return reinterpret_cast<T>(address);
}

struct Api {
    using GetIntegerv = void (APIENTRY*)(GLenum, GLint*);
    using GetIntegeriV = void (APIENTRY*)(GLenum, GLuint, GLint*);
    using CreateShader = GLuint (APIENTRY*)(GLenum);
    using ShaderSource = void (APIENTRY*)(GLuint, GLsizei, GLchar const* const*, GLint const*);
    using CompileShader = void (APIENTRY*)(GLuint);
    using GetShaderiv = void (APIENTRY*)(GLuint, GLenum, GLint*);
    using GetShaderInfoLog = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
    using CreateProgram = GLuint (APIENTRY*)();
    using AttachShader = void (APIENTRY*)(GLuint, GLuint);
    using LinkProgram = void (APIENTRY*)(GLuint);
    using GetProgramiv = void (APIENTRY*)(GLuint, GLenum, GLint*);
    using GetProgramInfoLog = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
    using UseProgram = void (APIENTRY*)(GLuint);
    using GetUniformLocation = GLint (APIENTRY*)(GLuint, GLchar const*);
    using Uniform1ui = void (APIENTRY*)(GLint, GLuint);
    using GenBuffers = void (APIENTRY*)(GLsizei, GLuint*);
    using BindBuffer = void (APIENTRY*)(GLenum, GLuint);
    using BufferData = void (APIENTRY*)(GLenum, GLsizeiptr, void const*, GLenum);
    using BufferSubData = void (APIENTRY*)(GLenum, std::ptrdiff_t, GLsizeiptr, void const*);
    using BindBufferBase = void (APIENTRY*)(GLenum, GLuint, GLuint);
    using DispatchCompute = void (APIENTRY*)(GLuint, GLuint, GLuint);
    using MemoryBarrier = void (APIENTRY*)(GLbitfield);
    using GetBufferSubData = void (APIENTRY*)(GLenum, std::ptrdiff_t, GLsizeiptr, void*);
    using GenQueries = void (APIENTRY*)(GLsizei, GLuint*);
    using BeginQuery = void (APIENTRY*)(GLenum, GLuint);
    using EndQuery = void (APIENTRY*)(GLenum);
    using GetQueryiv = void (APIENTRY*)(GLenum, GLenum, GLint*);
    using GetQueryObjectui64v = void (APIENTRY*)(GLuint, GLenum, GLuint64*);
    using FenceSync = GLsync (APIENTRY*)(GLenum, GLbitfield);
    using ClientWaitSync = GLenum (APIENTRY*)(GLsync, GLbitfield, GLuint64);
    using DeleteSync = void (APIENTRY*)(GLsync);

    GetIntegerv getIntegerv = nullptr;
    GetIntegeriV getIntegeriV = nullptr;
    CreateShader createShader = nullptr;
    ShaderSource shaderSource = nullptr;
    CompileShader compileShader = nullptr;
    GetShaderiv getShaderiv = nullptr;
    GetShaderInfoLog getShaderInfoLog = nullptr;
    CreateProgram createProgram = nullptr;
    AttachShader attachShader = nullptr;
    LinkProgram linkProgram = nullptr;
    GetProgramiv getProgramiv = nullptr;
    GetProgramInfoLog getProgramInfoLog = nullptr;
    UseProgram useProgram = nullptr;
    GetUniformLocation getUniformLocation = nullptr;
    Uniform1ui uniform1ui = nullptr;
    GenBuffers genBuffers = nullptr;
    BindBuffer bindBuffer = nullptr;
    BufferData bufferData = nullptr;
    BufferSubData bufferSubData = nullptr;
    BindBufferBase bindBufferBase = nullptr;
    DispatchCompute dispatchCompute = nullptr;
    MemoryBarrier memoryBarrier = nullptr;
    GetBufferSubData getBufferSubData = nullptr;
    GenQueries genQueries = nullptr;
    BeginQuery beginQuery = nullptr;
    EndQuery endQuery = nullptr;
    GetQueryiv getQueryiv = nullptr;
    GetQueryObjectui64v getQueryObjectui64v = nullptr;
    FenceSync fenceSync = nullptr;
    ClientWaitSync clientWaitSync = nullptr;
    DeleteSync deleteSync = nullptr;

    bool load(std::string& error) {
#define LOAD(member, type, name) member = loadGl<type>(name); if (!member) { error = "missing OpenGL entry point " name; return false; }
        LOAD(getIntegerv, GetIntegerv, "glGetIntegerv");
        LOAD(getIntegeriV, GetIntegeriV, "glGetIntegeri_v");
        LOAD(createShader, CreateShader, "glCreateShader");
        LOAD(shaderSource, ShaderSource, "glShaderSource");
        LOAD(compileShader, CompileShader, "glCompileShader");
        LOAD(getShaderiv, GetShaderiv, "glGetShaderiv");
        LOAD(getShaderInfoLog, GetShaderInfoLog, "glGetShaderInfoLog");
        LOAD(createProgram, CreateProgram, "glCreateProgram");
        LOAD(attachShader, AttachShader, "glAttachShader");
        LOAD(linkProgram, LinkProgram, "glLinkProgram");
        LOAD(getProgramiv, GetProgramiv, "glGetProgramiv");
        LOAD(getProgramInfoLog, GetProgramInfoLog, "glGetProgramInfoLog");
        LOAD(useProgram, UseProgram, "glUseProgram");
        LOAD(getUniformLocation, GetUniformLocation, "glGetUniformLocation");
        LOAD(uniform1ui, Uniform1ui, "glUniform1ui");
        LOAD(genBuffers, GenBuffers, "glGenBuffers");
        LOAD(bindBuffer, BindBuffer, "glBindBuffer");
        LOAD(bufferData, BufferData, "glBufferData");
        LOAD(bufferSubData, BufferSubData, "glBufferSubData");
        LOAD(bindBufferBase, BindBufferBase, "glBindBufferBase");
        LOAD(dispatchCompute, DispatchCompute, "glDispatchCompute");
        LOAD(memoryBarrier, MemoryBarrier, "glMemoryBarrier");
        LOAD(getBufferSubData, GetBufferSubData, "glGetBufferSubData");
        LOAD(fenceSync, FenceSync, "glFenceSync");
        LOAD(clientWaitSync, ClientWaitSync, "glClientWaitSync");
        LOAD(deleteSync, DeleteSync, "glDeleteSync");
#undef LOAD
        genQueries = loadGl<GenQueries>("glGenQueries");
        beginQuery = loadGl<BeginQuery>("glBeginQuery");
        endQuery = loadGl<EndQuery>("glEndQuery");
        getQueryiv = loadGl<GetQueryiv>("glGetQueryiv");
        getQueryObjectui64v = loadGl<GetQueryObjectui64v>("glGetQueryObjectui64v");
        return true;
    }
};

struct StateGuard {
    Api& gl;
    GLint program = 0;
    GLint genericSsbo = 0;
    std::array<GLint, 4> indexedSsbo{};

    explicit StateGuard(Api& api) : gl(api) {
        gl.getIntegerv(glCurrentProgram, &program);
        gl.getIntegerv(glShaderStorageBufferBinding, &genericSsbo);
        for (GLuint index = 0; index < indexedSsbo.size(); ++index)
            gl.getIntegeriV(glShaderStorageBufferBinding, index, &indexedSsbo[index]);
    }
    ~StateGuard() {
        for (GLuint index = 0; index < indexedSsbo.size(); ++index)
            gl.bindBufferBase(glShaderStorageBuffer, index, static_cast<GLuint>(indexedSsbo[index]));
        gl.bindBuffer(glShaderStorageBuffer, static_cast<GLuint>(genericSsbo));
        gl.useProgram(static_cast<GLuint>(program));
    }
};

GLuint compileProgram(Api& gl, std::string& error) {
    GLuint shader = gl.createShader(glComputeShader);
    if (!shader) { error = "glCreateShader returned zero"; return 0; }
    GLchar const* source = triggerTransformComputeSource;
    GLint sourceLength = static_cast<GLint>(std::char_traits<char>::length(source));
    gl.shaderSource(shader, 1, &source, &sourceLength);
    gl.compileShader(shader);
    GLint compiled = GL_FALSE;
    gl.getShaderiv(shader, glCompileStatus, &compiled);
    if (compiled != GL_TRUE) {
        std::array<GLchar, 4096> log{};
        GLsizei length = 0;
        gl.getShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
        error = std::string("compute shader compile failed: ") + log.data();
        return 0;
    }
    GLuint program = gl.createProgram();
    if (!program) { error = "glCreateProgram returned zero"; return 0; }
    gl.attachShader(program, shader);
    gl.linkProgram(program);
    GLint linked = GL_FALSE;
    gl.getProgramiv(program, glLinkStatus, &linked);
    if (linked != GL_TRUE) {
        std::array<GLchar, 4096> log{};
        GLsizei length = 0;
        gl.getProgramInfoLog(program, static_cast<GLsizei>(log.size()), &length, log.data());
        error = std::string("compute program link failed: ") + log.data();
        return 0;
    }
    return program;
}
} // namespace

struct HybridTransformExecutor::Impl {
    Api gl{};
    HGLRC context = nullptr;
    GLuint program = 0;
    std::array<GLuint, 4> buffers{};
    std::array<std::size_t, 4> bufferCapacity{};
    std::array<std::vector<std::byte>, 4> readOnlyInputs;
    bool cacheReadOnlyInputs = true;
    GLuint timerQuery = 0;
    GLint objectCountUniform = -1;
    bool ready = false;
    bool failed = false;
    // A timed-out compute may still be using the shared SSBOs. Once that happens,
    // never upload to or dispatch against those buffers again in this process.
    bool poisoned = false;
    std::string failure;

    void poison(std::string message) {
        ready = false;
        failed = true;
        poisoned = true;
        failure = std::move(message);
    }

    bool initialize(std::string& error) {
        if (poisoned) {
            error = failure;
            return false;
        }
        auto current = wglGetCurrentContext();
        if (!current) { error = "no current WGL context"; return false; }
        if (context != current) {
            // GL names belong to their creating context. A context switch drops
            // the old names without issuing deletes against the wrong context.
            context = current;
            program = 0;
            buffers.fill(0);
            bufferCapacity.fill(0);
            for (auto& cached : readOnlyInputs) cached.clear();
            timerQuery = 0;
            ready = false;
            failed = false;
            failure.clear();
        }
        if (ready) return true;
        if (failed) { error = failure; return false; }
        if (!gl.load(error)) { failed = true; failure = error; return false; }
        GLint major = 0, minor = 0;
        gl.getIntegerv(glMajorVersion, &major);
        gl.getIntegerv(glMinorVersion, &minor);
        if (major < 4 || (major == 4 && minor < 3)) {
            error = "current OpenGL context does not expose compute (4.3+)";
            failed = true;
            failure = error;
            return false;
        }
        auto renderer = reinterpret_cast<char const*>(glGetString(GL_RENDERER));
        if (!renderer || std::string_view(renderer).find("zink") == std::string_view::npos) {
            error = "current renderer is not Zink; hybrid transforms remain on vanilla CPU";
            failed = true;
            failure = error;
            return false;
        }
        program = compileProgram(gl, error);
        if (!program) { failed = true; failure = error; return false; }
        objectCountUniform = gl.getUniformLocation(program, "objectCount");
        if (objectCountUniform < 0) {
            error = "compute shader object count uniform missing";
            failed = true;
            failure = error;
            return false;
        }
        gl.genBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (std::any_of(buffers.begin(), buffers.end(), [](GLuint value) { return value == 0; })) {
            error = "could not allocate hybrid transform SSBOs";
            failed = true;
            failure = error;
            return false;
        }
        if (gl.genQueries && gl.beginQuery && gl.endQuery && gl.getQueryiv && gl.getQueryObjectui64v)
            gl.genQueries(1, &timerQuery);
        ready = true;
        return true;
    }
};

HybridTransformExecutor& HybridTransformExecutor::get() {
    static HybridTransformExecutor executor;
    if (!executor.m_impl) executor.m_impl = new Impl();
    return executor;
}

void HybridTransformExecutor::setReadOnlyCachingForBenchmark(bool enabled) {
    if (!m_impl) m_impl = new Impl();
    m_impl->cacheReadOnlyInputs = enabled;
}

bool HybridTransformExecutor::evaluate(std::vector<ObjectState>& states,
                                      std::vector<Action> const& actions,
                                      std::vector<Span> const& spans,
                                      std::vector<std::uint32_t> const& actionIndices,
                                      HybridBatchMetrics& metrics,
                                      std::string& error, bool measureCpu) {
    metrics = {};
    if (states.empty() || states.size() != spans.size() || actions.empty() || actionIndices.empty()) {
        error = "empty or inconsistent transform batch";
        return false;
    }
    for (auto const& span : spans) {
        if (span.offset > actionIndices.size() || span.count > actionIndices.size() - span.offset) {
            error = "transform span exceeds action-index buffer";
            return false;
        }
    }
    for (auto index : actionIndices) {
        if (index >= actions.size()) {
            error = "transform action index exceeds action buffer";
            return false;
        }
    }
    if (!m_impl->initialize(error)) return false;
    auto& impl = *m_impl;
    StateGuard guard(impl.gl);
    LARGE_INTEGER qpcStart{}, qpcFrequency{};
    if (measureCpu) { QueryPerformanceCounter(&qpcStart); QueryPerformanceFrequency(&qpcFrequency); }
    ULONG64 stageCyclesStart = 0, fenceCyclesStart = 0, fenceCyclesEnd = 0, stageCyclesEnd = 0;
    const bool haveStageCycles = measureCpu && QueryThreadCycleTime(GetCurrentThread(), &stageCyclesStart) != 0;
    auto totalStart = Clock::now();
    std::array<void const*, 4> data{
        states.data(), actions.data(), spans.data(), actionIndices.data()
    };
    std::array<std::size_t, 4> bytes{
        states.size() * sizeof(ObjectState), actions.size() * sizeof(Action),
        spans.size() * sizeof(Span), actionIndices.size() * sizeof(std::uint32_t)
    };
    auto uploadStart = Clock::now();
    for (GLuint binding = 0; binding < impl.buffers.size(); ++binding) {
        impl.gl.bindBuffer(glShaderStorageBuffer, impl.buffers[binding]);
        bool reallocated = false;
        if (bytes[binding] > impl.bufferCapacity[binding]) {
            std::size_t capacity = std::max<std::size_t>(impl.bufferCapacity[binding], 256);
            while (capacity < bytes[binding]) {
                if (capacity > std::numeric_limits<std::size_t>::max() / 2) {
                    capacity = bytes[binding];
                    break;
                }
                capacity *= 2;
            }
            impl.gl.bufferData(glShaderStorageBuffer, static_cast<GLsizeiptr>(capacity),
                               nullptr, glDynamicCopy);
            impl.bufferCapacity[binding] = capacity;
            ++metrics.bufferAllocations;
            reallocated = true;
        }
        // Only shader-read-only inputs may be reused. Object positions are
        // modified by compute, so binding zero is always uploaded afresh.
        auto& cached = impl.readOnlyInputs[binding];
        const bool unchanged = impl.cacheReadOnlyInputs && binding != 0 && !reallocated && cached.size() == bytes[binding] &&
            std::memcmp(cached.data(), data[binding], bytes[binding]) == 0;
        if (unchanged) {
            ++metrics.cachedInputBuffers;
        } else {
            impl.gl.bufferSubData(glShaderStorageBuffer, 0,
                                  static_cast<GLsizeiptr>(bytes[binding]), data[binding]);
            metrics.uploadBytes += bytes[binding];
            if (impl.cacheReadOnlyInputs && binding != 0) {
                const auto* first = static_cast<std::byte const*>(data[binding]);
                cached.assign(first, first + bytes[binding]);
            }
        }
        impl.gl.bindBufferBase(glShaderStorageBuffer, binding, impl.buffers[binding]);
    }
    auto uploadEnd = Clock::now();
    metrics.uploadMs = milliseconds(uploadStart, uploadEnd);

    GLint activeQuery = 0;
    bool timed = impl.timerQuery && impl.gl.getQueryiv && impl.gl.beginQuery && impl.gl.endQuery;
    if (timed) impl.gl.getQueryiv(glTimeElapsed, glCurrentQuery, &activeQuery);
    if (timed && activeQuery != 0) timed = false;
    if (timed) impl.gl.beginQuery(glTimeElapsed, impl.timerQuery);
    auto dispatchStart = Clock::now();
    impl.gl.useProgram(impl.program);
    impl.gl.uniform1ui(impl.objectCountUniform, static_cast<GLuint>(states.size()));
    impl.gl.dispatchCompute(static_cast<GLuint>((states.size() + 63) / 64), 1, 1);
    if (timed) impl.gl.endQuery(glTimeElapsed);
    impl.gl.memoryBarrier(glShaderStorageBarrierBit | glBufferUpdateBarrierBit);
    auto dispatchEnd = Clock::now();
    metrics.dispatchSubmitMs = milliseconds(dispatchStart, dispatchEnd);

    GLsync fence = impl.gl.fenceSync(glSyncGpuCommandsComplete, 0);
    if (!fence) {
        error = "glFenceSync failed; vanilla processing will handle the batch";
        impl.poison(error);
        return false;
    }
    auto syncStart = Clock::now();
    const bool haveFenceCycles = measureCpu && QueryThreadCycleTime(GetCurrentThread(), &fenceCyclesStart) != 0;
    GLenum waitResult = impl.gl.clientWaitSync(
        fence, glSyncFlushCommandsBit, glClientWaitBudgetNs);
    auto syncEnd = Clock::now();
    if (haveFenceCycles && QueryThreadCycleTime(GetCurrentThread(), &fenceCyclesEnd) && fenceCyclesEnd >= fenceCyclesStart)
        metrics.fenceThreadCycles = fenceCyclesEnd - fenceCyclesStart;
    metrics.syncWaitMs = milliseconds(syncStart, syncEnd);
    if (waitResult == glWaitFailed) {
        impl.gl.deleteSync(fence);
        error = "GPU completion fence wait failed; GPU executor disabled for this process to protect in-flight buffers";
        impl.poison(error);
        return false;
    }
    if (waitResult == glTimeoutExpired) {
        impl.gl.deleteSync(fence);
        error = "GPU completion fence exceeded the 10 ms frame safety budget; GPU executor disabled for this process to protect in-flight buffers";
        impl.poison(error);
        return false;
    }
    if (waitResult != glAlreadySignaled && waitResult != glConditionSatisfied) {
        impl.gl.deleteSync(fence);
        error = "GPU completion fence returned an unexpected status; GPU executor disabled for this process to protect in-flight buffers";
        impl.poison(error);
        return false;
    }
    auto transferStart = Clock::now();
    impl.gl.bindBuffer(glShaderStorageBuffer, impl.buffers[0]);
    impl.gl.getBufferSubData(glShaderStorageBuffer, 0,
        static_cast<GLsizeiptr>(bytes[0]), states.data());
    auto transferEnd = Clock::now();
    metrics.transferMs = milliseconds(transferStart, transferEnd);
    metrics.readbackBytes = static_cast<std::uint64_t>(bytes[0]);
    impl.gl.deleteSync(fence);
    if (timed) {
        GLuint64 elapsedNs = 0;
        impl.gl.getQueryObjectui64v(impl.timerQuery, glQueryResult, &elapsedNs);
        metrics.gpuComputeMs = static_cast<double>(elapsedNs) / 1'000'000.0;
    }
    metrics.totalMs = milliseconds(totalStart, Clock::now());
    LARGE_INTEGER qpcEnd{}; if (measureCpu) QueryPerformanceCounter(&qpcEnd);
    if (qpcFrequency.QuadPart > 0) metrics.qpcStageWallMs = 1000.0 * static_cast<double>(qpcEnd.QuadPart - qpcStart.QuadPart) / qpcFrequency.QuadPart;
    if (haveStageCycles && QueryThreadCycleTime(GetCurrentThread(), &stageCyclesEnd) && stageCyclesEnd >= stageCyclesStart)
        metrics.stageThreadCycles = stageCyclesEnd - stageCyclesStart;
    return true;
}

} // namespace vulkanizer::gpu
