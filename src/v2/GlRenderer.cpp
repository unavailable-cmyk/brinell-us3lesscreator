#include "GlRenderer.hpp"
#include "SpikeAudit.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace vulkanizer::v2 {
namespace {
constexpr GLenum arrayBuffer = 0x8892, uniformBuffer = 0x8a11;
constexpr GLenum storageBuffer = 0x90d2, dispatchBuffer = 0x90ee;
constexpr GLbitfield shaderBarrier = 0x2000, commandBarrier = 0x0040, bufferUpdateBarrier = 0x0200;
constexpr std::uint32_t workgroupWidth = 64;
using Clock = std::chrono::steady_clock;
constexpr std::size_t pageBytes = paletteSize * sizeof(PaletteEntry);
const char* vertexSource = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 color;
layout(location=3) in float chain;
layout(std140) uniform Chains { vec4 translation[256]; };
uniform mat4 mvp;
out vec2 uv;
out vec4 tint;
void main() {
    vec3 p = position;
    p.xy += translation[int(chain)].xy;
    gl_Position = mvp * vec4(p, 1.0);
    uv = texcoord;
    tint = color;
})";
const char* fragmentSource = R"(#version 330 core
in vec2 uv;
in vec4 tint;
uniform sampler2D texture0;
out vec4 outputColor;
void main() { outputColor = texture(texture0, uv) * tint; }
)";
const char* residentVertex = R"(#version 430 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 color;
layout(location=4) in uint objectId;
struct Resident { dvec4 position; vec4 angles; ivec2 section; uint properties; uint pad; };
layout(std430,binding=0) readonly buffer State { Resident states[]; };
uniform mat4 mvp;
out vec2 uv;
out vec4 tint;
void main() {
    Resident state = states[objectId];
    vec3 p = position;
    if (state.angles.xy == state.angles.zw) {
        p.xy += vec2(state.position.xy - state.position.zw);
    } else {
        // Cocos uses clockwise rotation, with separate X/Y basis angles.
        vec2 oldAngle = radians(-state.angles.zw);
        vec2 newAngle = radians(-state.angles.xy);
        mat2 oldBasis = mat2(cos(oldAngle.y), sin(oldAngle.y), -sin(oldAngle.x), cos(oldAngle.x));
        mat2 newBasis = mat2(cos(newAngle.y), sin(newAngle.y), -sin(newAngle.x), cos(newAngle.x));
        vec2 local = position.xy - vec2(state.position.zw);
        vec2 delta = newAngle - oldAngle;
        mat2 rigid = mat2(cos(delta.x), sin(delta.x), -sin(delta.x), cos(delta.x));
        mat2 basis = abs(determinant(oldBasis)) > 0.00001 ? newBasis * inverse(oldBasis) : rigid;
        p.xy = vec2(state.position.xy) + basis * local;
    }
    gl_Position = mvp * vec4(p, 1.0);
    uv = texcoord;
    tint = color;
})";
const char* gatherSource = R"(#version 430 core
layout(local_size_x=64) in;
struct Move { dvec2 delta; uint group; uint stamp; uint flags; uint kind; dvec2 center; vec4 matrix; vec2 shift; float orientation; uint journalIndex; };
struct Resident { dvec4 position; vec4 angles; ivec2 section; uint properties; uint pad; };
layout(std430,binding=0) buffer State { Resident states[]; };
// One epoch plus maskStride words per group. Stride is fixed for a scene.
layout(std430,binding=1) buffer GroupMasks { uint groupMask[]; };
// Four words per chain (group begin/end, member begin/end), followed by group ids.
layout(std430,binding=2) readonly buffer Topology { uint topology[]; };
layout(std430,binding=4) buffer ChainMasks { uint chainMask[]; };
layout(std430,binding=5) readonly buffer Commands { Move commands[]; };
layout(std430,binding=6) buffer Retired { uint retired[]; };
// One job per 64-member block of a touched chain, not per object.
layout(std430,binding=7) buffer Work { uint x; uint y; uint z; uint count; uvec2 jobs[]; };
uniform uint epoch, commandCount, retireCount, chainCount;
uniform uint maskStride, activeWords;
uniform int gather;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (gather == 2) {
        if (i < commandCount) {
            uint object = commands[i].group & 0x7fffffffu;
            // Never reactivate a genuinely retired resident.
            if (retired[object] == 0u) states[object].position.xy = commands[i].delta;
        }
        return;
    }
    if (gather == 0) {
        if (i == 0u) { x = 0u; y = 1u; z = 1u; count = 0u; }
        if (i < retireCount) atomicExchange(retired[commands[commandCount + i].group], 1u);
        // Multiple commands can target a group. Atomic clears are idempotent,
        // and a dispatch barrier separates clearing from setting bits.
        if (i < commandCount) {
            uint first = commands[i].group * (maskStride + 1u);
            atomicExchange(groupMask[first], epoch);
            for (uint w = 0u; w < activeWords; ++w) atomicExchange(groupMask[first + 1u + w], 0u);
        }
        return;
    }
    if (gather == 1) {
        if (i < commandCount)
            atomicOr(groupMask[commands[i].group * (maskStride + 1u) + 1u + i / 32u], 1u << (i % 32u));
        return;
    }
    if (i >= chainCount) return;
    uint h = i * 4u;
    uint anyBit = 0u;
    for (uint w = 0u; w < activeWords; ++w) {
        uint bits = 0u;
        for (uint g = topology[h]; g < topology[h + 1u]; ++g) {
            uint first = topology[g] * (maskStride + 1u);
            if (groupMask[first] == epoch) bits |= groupMask[first + 1u + w];
        }
        chainMask[i * maskStride + w] = bits;
        anyBit |= bits;
    }
    if (anyBit == 0u) return;
    uint begin = topology[h + 2u], end = topology[h + 3u];
    uint blocks = (end - begin + 63u) / 64u;
    if (blocks == 0u) return;
    uint slot = atomicAdd(count, blocks);
    for (uint b = 0u; b < blocks; ++b) jobs[slot + b] = uvec2(i, begin + 64u * b);
    // GL guarantees 65535 workgroups per dimension. For large topologies
    // a second dimension covers the remainder; extra groups return early.
    atomicMax(x, min(slot + blocks, 65535u));
    atomicMax(y, (slot + blocks + 65534u) / 65535u);
})";
// Chain-major scheduling: stable object IDs preserve all state and draw ABI.
// Every workgroup reads one shared ordered mask and visits a disjoint member
// block. Ascending set-bit order is the original command order.
const char* applySource = R"(#version 430 core
layout(local_size_x=64) in;
struct Move { dvec2 delta; uint group; uint stamp; uint flags; uint kind; dvec2 center; vec4 matrix; vec2 shift; float orientation; uint journalIndex; };
struct Resident { dvec4 position; vec4 angles; ivec2 section; uint properties; uint pad; };
struct SectionEvent { dvec2 position; uint object; uint action; uvec2 reserved; };
layout(std430,binding=0) buffer State { Resident states[]; };
layout(std430,binding=1) buffer Events { uint eventCount; uint eventPad0; uint eventPad1; uint eventPad2; SectionEvent events[]; };
layout(std430,binding=2) readonly buffer Topology { uint topology[]; };
layout(std430,binding=3) readonly buffer Members { uint members[]; };
layout(std430,binding=4) readonly buffer ChainMasks { uint chainMask[]; };
layout(std430,binding=5) readonly buffer Commands { Move commands[]; };
layout(std430,binding=6) readonly buffer Retired { uint retired[]; };
layout(std430,binding=7) readonly buffer Work { uint x; uint y; uint z; uint count; uvec2 jobs[]; };
shared uint mask[64];
uniform uint maskStride, activeWords;
uniform uint sectionXBits, sectionYBits;
int sectionIndex(double value, float factor) {
    if (value <= 0.0LF) return 0;
    if (value >= 10000000.0LF) return int(10000000.0 * factor);
    return int(value * double(factor));
}
void main() {
    uint jobIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * 65535u;
    if (jobIndex >= count) return; // uniform across the workgroup
    uvec2 job = jobs[jobIndex];
    uint lane = gl_LocalInvocationID.x;
    if (lane < activeWords) mask[lane] = chainMask[job.x * maskStride + lane];
    barrier();
    uint i = job.y + lane;
    if (i >= topology[job.x * 4u + 3u]) return;
    uint object = members[i];
    if (retired[object] != 0u) return;
    Resident state = states[object];
    precise dvec2 position = state.position.xy;
    for (uint word = 0u; word < activeWords; ++word) {
      uint bits = mask[word];
      while (bits != 0u) {
        uint bit = uint(findLSB(bits));
        bits &= bits - 1u;
        Move movement = commands[word * 32u + bit];
        if (movement.kind == 1u) {
            precise vec2 relative = vec2(position - movement.center);
            precise dvec2 xa = double(relative.x) * dvec2(movement.matrix.xy);
            precise dvec2 yc = double(relative.y) * dvec2(movement.matrix.zw);
            precise vec2 point = vec2((yc + xa) + dvec2(movement.shift));
            precise vec2 moved = point + vec2(movement.delta);
            precise dvec2 delta = dvec2(moved) - position;
            position = position + delta;
            if ((state.properties & 1u) != 0u && movement.orientation != 0.0) {
                precise vec2 angle = state.angles.xy + vec2(movement.orientation);
                state.angles.xy = angle;
            }
        } else if (movement.kind == 2u) {
            uint angleMask = (movement.flags >> (((state.properties & 1u) != 0u) ? 5u : 3u)) & 3u;
            if ((angleMask & 1u) != 0u) {
                precise float angle = state.angles.x + movement.orientation;
                state.angles.x = angle;
            }
            if ((angleMask & 2u) != 0u) {
                precise float angle = state.angles.y + movement.orientation;
                state.angles.y = angle;
            }
        } else {
            if (movement.delta.x != 0.0LF) position.x += movement.delta.x;
            if (movement.delta.y != 0.0LF) position.y += movement.delta.y;
        }
        if (movement.kind != 2u && (movement.flags & 2u) != 0u && state.section.x >= 0) {
            ivec2 section = ivec2(sectionIndex(position.x, uintBitsToFloat(sectionXBits)),
                sectionIndex(position.y, uintBitsToFloat(sectionYBits)));
            if (any(notEqual(section, state.section))) {
                uint slot = atomicAdd(eventCount, 1u);
                events[slot] = SectionEvent(position, object, movement.journalIndex, uvec2(0));
                state.section = section;
            }
        }
      }
    }
    state.position.xy = position;
    states[object] = state;
})";
}
namespace {
struct alignas(32) ResidentGpuState {
    std::array<double, 4> position;
    std::array<float, 4> angles;
    std::int32_t sectionX, sectionY;
    std::uint32_t properties, pad = 0;
};
static_assert(sizeof(ResidentGpuState) == 64 && offsetof(ResidentGpuState, angles) == 32 &&
    offsetof(ResidentGpuState, sectionX) == 48);
}
struct GlRenderer::Impl {
#define PROC(R, N, A) R(APIENTRY* N)A = nullptr
    PROC(GLuint, CreateShader, (GLenum));
    PROC(void, ShaderSource, (GLuint, GLsizei, const char* const*, const GLint*));
    PROC(void, CompileShader, (GLuint));
    PROC(void, GetShaderiv, (GLuint, GLenum, GLint*));
    PROC(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*));
    PROC(void, DeleteShader, (GLuint));
    PROC(GLuint, CreateProgram, ());
    PROC(void, AttachShader, (GLuint, GLuint));
    PROC(void, LinkProgram, (GLuint));
    PROC(void, GetProgramiv, (GLuint, GLenum, GLint*));
    PROC(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*));
    PROC(void, DeleteProgram, (GLuint));
    PROC(void, UseProgram, (GLuint));
    PROC(GLint, GetUniformLocation, (GLuint, const char*));
    PROC(void, Uniform1i, (GLint, GLint));
    PROC(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const float*));
    PROC(GLuint, GetUniformBlockIndex, (GLuint, const char*));
    PROC(void, UniformBlockBinding, (GLuint, GLuint, GLuint));
    PROC(void, GenBuffers, (GLsizei, GLuint*));
    PROC(void, DeleteBuffers, (GLsizei, const GLuint*));
    PROC(void, BindBuffer, (GLenum, GLuint));
    PROC(void, BufferData, (GLenum, std::ptrdiff_t, const void*, GLenum));
    PROC(void, BufferSubData, (GLenum, std::ptrdiff_t, std::ptrdiff_t, const void*));
    PROC(void, BindBufferRange, (GLenum, GLuint, GLuint, std::ptrdiff_t, std::ptrdiff_t));
    PROC(void, GenVertexArrays, (GLsizei, GLuint*));
    PROC(void, DeleteVertexArrays, (GLsizei, const GLuint*));
    PROC(void, BindVertexArray, (GLuint));
    PROC(void, EnableVertexAttribArray, (GLuint));
    PROC(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*));
    PROC(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void*));
    PROC(void, Uniform1ui, (GLint, GLuint));
    PROC(void, DispatchCompute, (GLuint, GLuint, GLuint));
    PROC(void, DispatchComputeIndirect, (std::ptrdiff_t));
    PROC(void, MemoryBarrier, (GLbitfield));
    PROC(void, GetBufferSubData, (GLenum, std::ptrdiff_t, std::ptrdiff_t, void*));
    PROC(void, GetIntegerv, (GLenum, GLint*));
    PROC(void, GetIntegeri_v, (GLenum, GLuint, GLint*));
    PROC(void, GetInteger64i_v, (GLenum, GLuint, std::int64_t*));
    PROC(void, BindBufferBase, (GLenum, GLuint, GLuint));
    PROC(void, DrawArrays, (GLenum, GLint, GLsizei));
    PROC(GLenum, GetError, ());
#undef PROC
    GLuint program = 0, geometry = 0, palette = 0, vao = 0;
    GLuint residentProgram = 0, gatherProgram = 0, applyProgram = 0;
    std::array<GLuint, 8> storage{};
    GLuint sectionEvents = 0;
    std::size_t sectionCapacity = 0;
    std::vector<std::uint32_t> groupWidths;
    std::vector<gpu::ResidentTranslation> uploads;
    std::uint32_t epoch = 0;
    std::size_t residentObjectCount = 0;
    GLint gatherPhase = -1, gatherEpoch = -1, retireCount = -1, gatherCount = -1;
    GLint gatherChainCount = -1, applySectionX = -1, applySectionY = -1;
    GLint gatherMaskStride = -1, gatherActiveWords = -1, applyMaskStride = -1, applyActiveWords = -1;
    std::uint32_t commandCapacity = GlRenderer::defaultReplayCapacity;
    GLint residentMvp = -1, residentTexture = -1;
    GLint mvpLocation = -1, textureLocation = -1;
    std::size_t stride = pageBytes;
    std::size_t residentGroupCount = 0;
    std::size_t membershipChainCount = 0;
    std::uint64_t generation = 0;
    RenderCounters counters;

    explicit Impl(const Resolve& resolve) {
#define LOAD(N) N = reinterpret_cast<decltype(N)>(resolve("gl" #N)); if (!N) throw std::runtime_error("Missing GL entry point gl" #N)
        LOAD(CreateShader); LOAD(ShaderSource); LOAD(CompileShader); LOAD(GetShaderiv);
        LOAD(GetShaderInfoLog); LOAD(DeleteShader); LOAD(CreateProgram); LOAD(AttachShader);
        LOAD(LinkProgram); LOAD(GetProgramiv); LOAD(GetProgramInfoLog); LOAD(DeleteProgram);
        LOAD(UseProgram); LOAD(GetUniformLocation); LOAD(Uniform1i); LOAD(UniformMatrix4fv);
        LOAD(GetUniformBlockIndex); LOAD(UniformBlockBinding); LOAD(GenBuffers); LOAD(DeleteBuffers);
        LOAD(BindBuffer); LOAD(BufferData); LOAD(BufferSubData); LOAD(BindBufferRange);
        LOAD(GenVertexArrays); LOAD(DeleteVertexArrays); LOAD(BindVertexArray);
        LOAD(EnableVertexAttribArray); LOAD(VertexAttribPointer); LOAD(GetIntegerv); LOAD(DrawArrays); LOAD(GetError);
        LOAD(GetIntegeri_v); LOAD(GetInteger64i_v); LOAD(BindBufferBase);
#undef LOAD
#define OPTIONAL(N) N = reinterpret_cast<decltype(N)>(resolve("gl" #N))
        OPTIONAL(VertexAttribIPointer); OPTIONAL(Uniform1ui); OPTIONAL(DispatchCompute);
        OPTIONAL(DispatchComputeIndirect); OPTIONAL(MemoryBarrier); OPTIONAL(GetBufferSubData);
#undef OPTIONAL
    }
    void check(const char* operation) {
        const auto error = GetError();
        if (error) throw std::runtime_error(std::string(operation) + " GL error " + std::to_string(error));
    }
    struct StateGuard {
        Impl& gl;
        GLint program = 0, vao = 0, array = 0, uniform = 0, indexed = 0;
        std::int64_t start = 0, size = 0;
        explicit StateGuard(Impl& api) : gl(api) {
            gl.GetIntegerv(0x8b8d, &program);
            gl.GetIntegerv(0x85b5, &vao);
            gl.GetIntegerv(0x8894, &array);
            gl.GetIntegerv(0x8a28, &uniform);
            gl.GetIntegeri_v(0x8a28, 0, &indexed);
            gl.GetInteger64i_v(0x8a29, 0, &start);
            gl.GetInteger64i_v(0x8a2a, 0, &size);
        }
        ~StateGuard() {
            gl.UseProgram(program);
            gl.BindVertexArray(vao);
            gl.BindBuffer(arrayBuffer, array);
            if (indexed && size > 0) gl.BindBufferRange(uniformBuffer, 0, indexed, start, size);
            else gl.BindBufferBase(uniformBuffer, 0, indexed);
            gl.BindBuffer(uniformBuffer, uniform);
        }
    };
    struct StorageGuard {
        Impl& gl;
        GLint buffer = 0, indirect = 0, program = 0;
        struct Binding { GLint name = 0; std::int64_t start = 0, size = 0; };
        std::array<Binding, 8> bindings{};
        unsigned slots;
        explicit StorageGuard(Impl& api, unsigned slots = 8) : gl(api), slots(slots) {
            gl.GetIntegerv(0x90d3, &buffer);
            gl.GetIntegerv(0x90ef, &indirect);
            gl.GetIntegerv(0x8b8d, &program);
            for (GLuint i = 0; i < slots; ++i) {
                gl.GetIntegeri_v(0x90d3, i, &bindings[i].name);
                gl.GetInteger64i_v(0x90d4, i, &bindings[i].start);
                gl.GetInteger64i_v(0x90d5, i, &bindings[i].size);
            }
        }
        ~StorageGuard() {
            gl.UseProgram(program);
            for (GLuint i = 0; i < slots; ++i) {
                const auto& b = bindings[i];
                if (b.name && b.size > 0) gl.BindBufferRange(storageBuffer, i, b.name, b.start, b.size);
                else gl.BindBufferBase(storageBuffer, i, b.name);
            }
            gl.BindBuffer(storageBuffer, buffer);
            gl.BindBuffer(dispatchBuffer, indirect);
        }
    };
    GLuint shader(GLenum type, const char* source) {
        const auto id = CreateShader(type);
        ShaderSource(id, 1, &source, nullptr);
        CompileShader(id);
        GLint success = 0;
        GetShaderiv(id, 0x8b81, &success);
        if (!success) {
            char log[4096]{};
            GetShaderInfoLog(id, sizeof(log), nullptr, log);
            DeleteShader(id);
            throw std::runtime_error(std::string("V2 shader compilation failed ") + log);
        }
        return id;
    }
    void createProgram() {
        GLuint vertex = 0, fragment = 0;
        try {
            vertex = shader(0x8b31, vertexSource);
            fragment = shader(0x8b30, fragmentSource);
            program = CreateProgram();
            AttachShader(program, vertex);
            AttachShader(program, fragment);
            LinkProgram(program);
            GLint success = 0;
            GetProgramiv(program, 0x8b82, &success);
            if (!success) {
                char log[4096]{};
                GetProgramInfoLog(program, sizeof(log), nullptr, log);
                throw std::runtime_error(std::string("V2 program linking failed ") + log);
            }
            UniformBlockBinding(program, GetUniformBlockIndex(program, "Chains"), 0);
            mvpLocation = GetUniformLocation(program, "mvp");
            textureLocation = GetUniformLocation(program, "texture0");
        } catch (...) {
            if (vertex) DeleteShader(vertex);
            if (fragment) DeleteShader(fragment);
            throw;
        }
        DeleteShader(vertex);
        DeleteShader(fragment);
    }
    GLuint link(GLenum type, const char* source, const char* fragment = nullptr) {
        const auto primary = shader(type, source);
        GLuint secondary = 0, result = 0;
        try {
            if (fragment) secondary = shader(0x8b30, fragment);
            result = CreateProgram();
            AttachShader(result, primary);
            if (secondary) AttachShader(result, secondary);
            LinkProgram(result);
            GLint success = 0;
            GetProgramiv(result, 0x8b82, &success);
            if (!success) {
                char log[4096]{};
                GetProgramInfoLog(result, sizeof(log), nullptr, log);
                throw std::runtime_error(std::string("Resident shader link failed ") + log);
            }
        } catch (...) {
            if (result) DeleteProgram(result);
            if (secondary) DeleteShader(secondary);
            DeleteShader(primary);
            throw;
        }
        if (secondary) DeleteShader(secondary);
        DeleteShader(primary);
        return result;
    }
    ~Impl() {
        if (vao) DeleteVertexArrays(1, &vao);
        if (geometry) DeleteBuffers(1, &geometry);
        if (palette) DeleteBuffers(1, &palette);
        if (program) DeleteProgram(program);
        for (auto id : {residentProgram, gatherProgram, applyProgram}) if (id) DeleteProgram(id);
        DeleteBuffers(static_cast<GLsizei>(storage.size()), storage.data());
        if (sectionEvents) DeleteBuffers(1, &sectionEvents);
    }
};
GlRenderer::GlRenderer(Resolve resolve) : m_impl(std::make_unique<Impl>(resolve)) {
    m_impl->createProgram();
}
GlRenderer::~GlRenderer() = default;
void GlRenderer::attach(Scene& scene) {
    auto& gl = *m_impl;
    if (gl.residentProgram) throw std::logic_error("Resident renderer is bound to one scene");
    Impl::StateGuard preserve(gl);
    gl.generation = 0;
    GLint alignment = 0, maxBlock = 0;
    gl.GetIntegerv(0x8a34, &alignment);
    gl.GetIntegerv(0x8a30, &maxBlock);
    if (alignment <= 0 || maxBlock < pageBytes) throw std::runtime_error("V2 palette UBO unavailable");
    gl.stride = ((pageBytes + alignment - 1) / alignment) * alignment;
    if (!gl.geometry) gl.GenBuffers(1, &gl.geometry);
    if (!gl.palette) gl.GenBuffers(1, &gl.palette);
    if (!gl.vao) gl.GenVertexArrays(1, &gl.vao);
    gl.BindVertexArray(gl.vao);
    gl.BindBuffer(arrayBuffer, gl.geometry);
    const auto vertices = scene.geometry();
    gl.BufferData(arrayBuffer, vertices.size_bytes(), vertices.data(), 0x88e4);
    gl.EnableVertexAttribArray(0);
    gl.EnableVertexAttribArray(1);
    gl.EnableVertexAttribArray(2);
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
    gl.VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, chain)));
    // Bring the retained palette up to date even when this is a recovery attach.
    scene.updates();
    const auto palette = scene.palette();
    const auto pages = (palette.size() + paletteSize - 1) / paletteSize;
    std::vector<std::byte> initial(std::max<std::size_t>(pages, 1) * gl.stride);
    for (std::size_t id = 0; id < palette.size(); ++id)
        std::memcpy(initial.data() + (id / paletteSize) * gl.stride + (id % paletteSize) * sizeof(PaletteEntry),
            &palette[id], sizeof(PaletteEntry));
    gl.BindBuffer(uniformBuffer, gl.palette);
    gl.BufferData(uniformBuffer, initial.size(), initial.data(), 0x88e8);
    scene.draws();
    gl.check("V2 scene upload");
    scene.acknowledgeUpdates();
    gl.counters.initialUploadBytes += vertices.size_bytes() + initial.size();
    gl.generation = scene.generation();
}
void GlRenderer::attachResident(Scene& scene, std::span<const ResidentObject> objects, std::size_t groupCount,
    std::uint32_t commandCapacity) {
    auto& gl = *m_impl;
    if (commandCapacity < 32 || commandCapacity > temporalReplayCapacity || commandCapacity % 32)
        throw std::invalid_argument("Invalid resident replay capacity");
    gl.commandCapacity = commandCapacity;
    const auto maskStride = commandCapacity / 32;
    if (!gl.DispatchCompute || !gl.DispatchComputeIndirect || !gl.MemoryBarrier ||
        !gl.Uniform1ui || !gl.VertexAttribIPointer || !gl.GetBufferSubData)
        throw std::runtime_error("Resident GL compute requires GL 4.3 with double precision");
    if (objects.size() != scene.objectCount() || objects.empty() || !groupCount ||
        objects.size() > workgroupWidth * 65535u || groupCount > UINT32_MAX / (maskStride + 1u))
        throw std::invalid_argument("Invalid resident scene dimensions");
    attach(scene);
    Impl::StateGuard preserve(gl);
    Impl::StorageGuard preserveStorage(gl);
    gl.residentProgram = gl.link(0x8b31, residentVertex, fragmentSource);
    gl.gatherProgram = gl.link(0x91b9, gatherSource);
    gl.applyProgram = gl.link(0x91b9, applySource);
    gl.residentMvp = gl.GetUniformLocation(gl.residentProgram, "mvp");
    gl.residentTexture = gl.GetUniformLocation(gl.residentProgram, "texture0");
    gl.gatherPhase = gl.GetUniformLocation(gl.gatherProgram, "gather");
    gl.gatherEpoch = gl.GetUniformLocation(gl.gatherProgram, "epoch");
    gl.gatherCount = gl.GetUniformLocation(gl.gatherProgram, "commandCount");
    gl.retireCount = gl.GetUniformLocation(gl.gatherProgram, "retireCount");
    gl.gatherChainCount = gl.GetUniformLocation(gl.gatherProgram, "chainCount");
    gl.gatherMaskStride = gl.GetUniformLocation(gl.gatherProgram, "maskStride");
    gl.gatherActiveWords = gl.GetUniformLocation(gl.gatherProgram, "activeWords");
    gl.applyMaskStride = gl.GetUniformLocation(gl.applyProgram, "maskStride");
    gl.applyActiveWords = gl.GetUniformLocation(gl.applyProgram, "activeWords");
    gl.applySectionX = gl.GetUniformLocation(gl.applyProgram, "sectionXBits");
    gl.applySectionY = gl.GetUniformLocation(gl.applyProgram, "sectionYBits");

    std::vector<ResidentGpuState> positions;
    std::vector<std::uint32_t> members;
    // Unique membership signatures -> dense chain ids.
    std::map<std::vector<std::uint32_t>, std::uint32_t> signatureToChain;
    std::vector<std::vector<std::uint32_t>> chainGroups, chainMembers;
    positions.reserve(objects.size());
    gl.groupWidths.assign(groupCount, 0);
    gl.residentGroupCount = groupCount;
    for (ObjectId i = 0; i < objects.size(); ++i) {
        const auto& object = objects[i];
        const auto [x, y] = object.base;
        positions.push_back({{x, y, x, y}, {object.rotationX, object.rotationY, object.rotationX, object.rotationY},
            object.sectionX, object.sectionY, object.canRotate ? 1u : 0u});
        if (!std::is_sorted(object.groups.begin(), object.groups.end()) ||
            std::adjacent_find(object.groups.begin(), object.groups.end()) != object.groups.end())
            throw std::invalid_argument("Invalid resident object memberships");
        std::vector<std::uint32_t> signature;
        signature.reserve(object.groups.size());
        for (auto group : object.groups) {
            if (group >= groupCount) throw std::out_of_range("Unknown resident movement route");
            ++gl.groupWidths[group];
            signature.push_back(group);
        }
        auto [it, inserted] = signatureToChain.emplace(signature, static_cast<std::uint32_t>(chainGroups.size()));
        if (inserted) {
            chainGroups.push_back(signature);
            chainMembers.emplace_back();
        }
        chainMembers[it->second].push_back(i);
    }
    gl.membershipChainCount = chainGroups.size();
    std::vector<std::uint32_t> topology(4 * chainGroups.size());
    members.reserve(objects.size());
    std::size_t blockCapacity = 0;
    for (std::size_t chain = 0; chain < chainGroups.size(); ++chain) {
        const auto& groups = chainGroups[chain];
        const auto& ids = chainMembers[chain];
        if (groups.size() > UINT32_MAX - topology.size())
            throw std::length_error("Resident chain topology capacity exceeded");
        topology[4 * chain] = static_cast<std::uint32_t>(topology.size());
        topology.insert(topology.end(), groups.begin(), groups.end());
        topology[4 * chain + 1] = static_cast<std::uint32_t>(topology.size());
        topology[4 * chain + 2] = static_cast<std::uint32_t>(members.size());
        members.insert(members.end(), ids.begin(), ids.end());
        topology[4 * chain + 3] = static_cast<std::uint32_t>(members.size());
        if (!groups.empty()) blockCapacity += (ids.size() + workgroupWidth - 1) / workgroupWidth;
        gl.counters.membershipGroupIncidences += groups.size();
        gl.counters.maxGroupsPerMembershipChain = std::max<std::uint64_t>(
            gl.counters.maxGroupsPerMembershipChain, groups.size());
    }
    gl.counters.membershipChains = chainGroups.size();
    gl.counters.membershipBlocks = blockCapacity;

    gl.GenBuffers(static_cast<GLsizei>(gl.storage.size()), gl.storage.data());
    auto buffer = [&](unsigned slot, std::size_t bytes, const void* data) {
        gl.BindBuffer(storageBuffer, gl.storage[slot]);
        gl.BufferData(storageBuffer, std::max<std::size_t>(4, bytes), bytes ? data : nullptr, 0x88e8);
        gl.counters.initialUploadBytes += data ? bytes : 0;
    };
    buffer(0, positions.size() * sizeof(positions.front()), positions.data());
    std::vector<std::uint32_t> groupMasks(groupCount * (maskStride + 1u));
    buffer(1, groupMasks.size() * sizeof(std::uint32_t), groupMasks.data());
    buffer(2, topology.size() * sizeof(std::uint32_t), topology.data());
    buffer(3, members.size() * sizeof(std::uint32_t), members.data());
    buffer(4, chainGroups.size() * maskStride * sizeof(std::uint32_t), nullptr);
    std::vector<std::uint32_t> retiredFlags(objects.size());
    buffer(6, retiredFlags.size() * sizeof(std::uint32_t), retiredFlags.data());
    buffer(7, (4 + 2 * blockCapacity) * sizeof(std::uint32_t), nullptr);
    gl.BindVertexArray(gl.vao);
    gl.BindBuffer(arrayBuffer, gl.geometry);
    gl.EnableVertexAttribArray(4);
    gl.VertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, object)));
    gl.residentObjectCount = objects.size();
    gl.uploads.reserve(2 * commandCapacity);
    gl.check("Resident topology upload");
}
void GlRenderer::dispatchResident(std::span<const gpu::ResidentTranslation> commands, std::span<const ObjectId> retired,
    std::vector<gpu::ResidentSectionEvent>* events, float sectionX, float sectionY) {
    spike::Scope timing(spike::Phase::residentDispatch, commands.size());
    auto& gl = *m_impl;
    if (!gl.residentProgram) throw std::logic_error("Attach resident storage before executing commands");
    if (commands.empty() && retired.empty()) return;
    // Absolute positions from scoped native writes. Upload a contiguous,
    // deduplicated batch instead of one GL call per edited object.
    constexpr std::uint32_t rebaseBit = 0x80000000u;
    if (!commands.empty() && (commands.front().group & rebaseBit)) {
        if (!retired.empty() || commands.size() > gl.residentObjectCount)
            throw std::length_error("Resident rebase capacity exceeded");
        for (const auto& command : commands)
            if (!(command.group & rebaseBit) || (command.group & ~rebaseBit) >= gl.residentObjectCount)
                throw std::out_of_range("Unknown resident rebase");
        const auto started = Clock::now();
        Impl::StorageGuard preserve(gl);
        for (GLuint i = 0; i < gl.storage.size(); ++i) gl.BindBufferBase(storageBuffer, i, gl.storage[i]);
        gl.BindBuffer(storageBuffer, gl.storage[5]);
        gl.UseProgram(gl.gatherProgram);
        gl.Uniform1i(gl.gatherPhase, 2);
        // GL guarantees at least 65535 workgroups per dimension.
        constexpr std::size_t maxRebases = 65535u * workgroupWidth;
        for (std::size_t first = 0; first < commands.size(); first += maxRebases) {
            const auto count = std::min(maxRebases, commands.size() - first);
            const auto bytes = count * sizeof(gpu::ResidentTranslation);
            gl.BufferData(storageBuffer, bytes, commands.data() + first, 0x88e0);
            gl.Uniform1ui(gl.gatherCount, static_cast<GLuint>(count));
            gl.DispatchCompute(static_cast<GLuint>((count + workgroupWidth - 1) / workgroupWidth), 1, 1);
            gl.MemoryBarrier(shaderBarrier | bufferUpdateBarrier);
            ++gl.counters.dispatches;
            ++gl.counters.residentSubmissions;
            gl.counters.uploadBytes += bytes;
            gl.counters.residentCommandBytes += bytes;
        }
        gl.counters.residentUploadMs += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        gl.check("Resident absolute rebase");
        return;
    }
    if (commands.size() > gl.commandCapacity || retired.size() > gl.residentObjectCount || gl.epoch >= UINT32_MAX - 1)
        throw std::length_error("Resident dispatch capacity exceeded");
    const auto started = Clock::now();
    Impl::StorageGuard preserve(gl);
    gl.uploads.assign(commands.begin(), commands.end());
    for (auto id : retired) {
        if (id >= gl.residentObjectCount) throw std::out_of_range("Unknown retired resident");
        gl.uploads.push_back({0, 0, id, 0});
    }
    bool hasMembers = false;
    std::size_t eventCapacity = 0;
    const auto epoch = ++gl.epoch;
    for (const auto& command : commands) {
        if (command.flags & gpu::residentTrackSections) eventCapacity += gl.groupWidths.at(command.group);
        hasMembers |= gl.groupWidths.at(command.group) != 0;
    }
    if (eventCapacity && !events) throw std::logic_error("Missing resident section event consumer");
    for (GLuint i = 0; i < gl.storage.size(); ++i) gl.BindBufferBase(storageBuffer, i, gl.storage[i]);
    gl.BindBuffer(storageBuffer, gl.storage[5]);
    const auto bytes = gl.uploads.size() * sizeof(gpu::ResidentTranslation);
    gl.BufferData(storageBuffer, bytes, gl.uploads.data(), 0x88e0);
    gl.counters.uploadBytes += bytes;
    gl.counters.residentCommandBytes += bytes;
    gl.counters.residentUploadMs += std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    const auto blocks = [](std::uint32_t n) { return (n + workgroupWidth - 1) / workgroupWidth; };
    gl.UseProgram(gl.gatherProgram);
    gl.Uniform1ui(gl.gatherEpoch, epoch);
    gl.Uniform1ui(gl.gatherCount, static_cast<GLuint>(commands.size()));
    gl.Uniform1ui(gl.retireCount, static_cast<GLuint>(retired.size()));
    gl.Uniform1ui(gl.gatherChainCount, static_cast<GLuint>(gl.membershipChainCount));
    const auto activeWords = static_cast<GLuint>((commands.size() + 31u) / 32u);
    gl.Uniform1ui(gl.gatherMaskStride, gl.commandCapacity / 32u);
    gl.Uniform1ui(gl.gatherActiveWords, activeWords);
    gl.Uniform1i(gl.gatherPhase, 0);
    gl.DispatchCompute(std::max(1u, blocks(static_cast<GLuint>(std::max(commands.size(), retired.size())))), 1, 1);
    ++gl.counters.dispatches;
    ++gl.counters.residentSubmissions;
    gl.MemoryBarrier(shaderBarrier);
    if (hasMembers) {
        gl.Uniform1i(gl.gatherPhase, 1);
        gl.DispatchCompute(blocks(static_cast<GLuint>(commands.size())), 1, 1);
        gl.MemoryBarrier(shaderBarrier);
        gl.Uniform1i(gl.gatherPhase, 3);
        gl.DispatchCompute(blocks(static_cast<GLuint>(gl.membershipChainCount)), 1, 1);
        gl.MemoryBarrier(shaderBarrier | commandBarrier);

        gl.UseProgram(gl.applyProgram);
        gl.Uniform1ui(gl.applyMaskStride, gl.commandCapacity / 32u);
        gl.Uniform1ui(gl.applyActiveWords, activeWords);

        if (eventCapacity) {
            if (!gl.sectionEvents) gl.GenBuffers(1, &gl.sectionEvents);
            gl.BindBuffer(storageBuffer, gl.sectionEvents);
            if (eventCapacity > gl.sectionCapacity) {
                gl.BufferData(storageBuffer, 16 + eventCapacity * sizeof(gpu::ResidentSectionEvent), nullptr, 0x88e8);
                gl.sectionCapacity = eventCapacity;
            }
            const std::uint32_t zero = 0;
            gl.BufferSubData(storageBuffer, 0, sizeof(zero), &zero);
            gl.BindBufferBase(storageBuffer, 1, gl.sectionEvents);
            gl.Uniform1ui(gl.applySectionX, std::bit_cast<std::uint32_t>(sectionX));
            gl.Uniform1ui(gl.applySectionY, std::bit_cast<std::uint32_t>(sectionY));
        }
        gl.BindBuffer(dispatchBuffer, gl.storage[7]);
        gl.DispatchComputeIndirect(0);
        gl.MemoryBarrier(shaderBarrier | bufferUpdateBarrier);
        if (eventCapacity) {
            spike::Scope timing(spike::Phase::residentReadback);
            std::uint32_t count = 0;
            gl.BindBuffer(storageBuffer, gl.sectionEvents);
            gl.GetBufferSubData(storageBuffer, 0, sizeof(count), &count);
            if (count > eventCapacity) throw std::runtime_error("Resident section event overflow");
            events->resize(count);
            if (count) gl.GetBufferSubData(storageBuffer, 16, count * sizeof(events->front()), events->data());
        }
        gl.counters.dispatches += 3;
    }
    gl.check("Resident ordered compute");
}
std::vector<ResidentValidationState> GlRenderer::validateResidentPositions() {
    auto& gl = *m_impl;
    if (!gl.residentProgram) throw std::logic_error("No resident state to validate");
    std::vector<ResidentGpuState> readback(gl.residentObjectCount);
    Impl::StorageGuard preserve(gl, 0);
    gl.BindBuffer(storageBuffer, gl.storage[0]);
    const auto size = readback.size() * sizeof(readback.front());
    const auto start = Clock::now();
    gl.GetBufferSubData(storageBuffer, 0, size, readback.data());
    gl.counters.residentValidationWaitMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    gl.counters.residentValidationReadbackBytes += size;
    gl.check("Resident validation readback");
    std::vector<ResidentValidationState> result;
    result.reserve(readback.size());
    for (const auto& state : readback) result.push_back({{state.position[0], state.position[1]}, state.angles[0], state.angles[1]});
    return result;
}
void GlRenderer::updateResidentQuad(const Draw& draw, const std::array<Corner, 4>& quad, Translation origin,
    float rotationX, float rotationY) {
    auto& gl = *m_impl;
    if (!gl.residentProgram || draw.nativeObject >= gl.residentObjectCount || draw.vertexCount != 6)
        throw std::logic_error("Cannot refresh an invalid resident quad");
    Impl::StateGuard preserve(gl);
    Impl::StorageGuard preserveStorage(gl, 0);
    constexpr std::array<unsigned, 6> indices{0, 1, 2, 2, 1, 3};
    std::array<Vertex, 6> vertices;
    for (unsigned i = 0; i < vertices.size(); ++i) {
        const auto& corner = quad[indices[i]];
        vertices[i] = {corner.x, corner.y, corner.z, corner.u, corner.v, corner.color, 0, draw.nativeObject};
    }
    gl.BindBuffer(arrayBuffer, gl.geometry);
    gl.BufferSubData(arrayBuffer, draw.firstVertex * sizeof(Vertex), sizeof(vertices), vertices.data());
    const double base[]{origin.x, origin.y};
    gl.BindBuffer(storageBuffer, gl.storage[0]);
    const auto offset = sizeof(ResidentGpuState) * draw.nativeObject;
    gl.BufferSubData(storageBuffer, offset + 2 * sizeof(double), sizeof(base), base);
    const float angles[]{rotationX, rotationY};
    gl.BufferSubData(storageBuffer, offset + offsetof(ResidentGpuState, angles) + 2 * sizeof(float), sizeof(angles), angles);
    gl.counters.residentGeometryBytes += sizeof(vertices) + sizeof(base);
    gl.counters.uploadBytes += sizeof(vertices) + sizeof(base);
    gl.check("Resident quad refresh");
}
void GlRenderer::updateResidentSection(ObjectId object, int x, int y) {
    auto& gl = *m_impl;
    if (object >= gl.residentObjectCount) throw std::out_of_range("Unknown resident section");
    Impl::StorageGuard preserve(gl, 0);
    const std::int32_t section[]{x, y};
    gl.BindBuffer(storageBuffer, gl.storage[0]);
    gl.BufferSubData(storageBuffer, sizeof(ResidentGpuState) * object + offsetof(ResidentGpuState, sectionX),
        sizeof(section), section);
}
void GlRenderer::render(Scene& scene, const std::array<float, 16>& mvp, RenderSink& sink) {
    renderRanges(scene, scene.draws(), mvp, sink);
}
void GlRenderer::renderRanges(Scene& scene, std::span<const Draw> draws,
                             const std::array<float, 16>& mvp, RenderSink& sink) {
    auto& gl = *m_impl;
    Impl::StateGuard preserve(gl);
    std::optional<Impl::StorageGuard> preserveStorage;
    if (gl.generation != scene.generation()) throw std::logic_error("Attach the V2 scene before rendering");
    if (gl.residentProgram) preserveStorage.emplace(gl, 1);
    else {
        gl.BindBuffer(uniformBuffer, gl.palette);
        for (const auto& update : scene.updates()) {
            const auto offset = (update.chain / paletteSize) * gl.stride + (update.chain % paletteSize) * sizeof(PaletteEntry);
            gl.BufferSubData(uniformBuffer, offset, sizeof(PaletteEntry), &update.value);
            gl.counters.uploadBytes += sizeof(PaletteEntry);
            gl.counters.paletteUploadBytes += sizeof(PaletteEntry);
        }
        gl.check("V2 palette update");
        scene.acknowledgeUpdates();
    }
    for (const auto& draw : draws) {
        if (draw.path == ExecutionPath::nativeCpu) { sink.drawNative(draw.nativeObject); continue; }
        sink.bindMaterial(draw.material);
        // A native range can mutate GL state, so each shared range establishes
        // all state owned by this backend again at its original draw position.
        gl.UseProgram(gl.residentProgram ? gl.residentProgram : gl.program);
        gl.BindVertexArray(gl.vao);
        gl.UniformMatrix4fv(gl.residentProgram ? gl.residentMvp : gl.mvpLocation, 1, GL_FALSE, mvp.data());
        gl.Uniform1i(gl.residentProgram ? gl.residentTexture : gl.textureLocation, 0);
        gl.counters.drawUniformUploadBytes += sizeof(float) * 16 + sizeof(GLint);
        gl.counters.uploadBytes += sizeof(float) * 16 + sizeof(GLint);
        if (gl.residentProgram) gl.BindBufferBase(storageBuffer, 0, gl.storage[0]);
        else gl.BindBufferRange(uniformBuffer, 0, gl.palette, draw.page * gl.stride, pageBytes);
        if (draw.vertexCount > std::numeric_limits<GLsizei>::max()) throw std::length_error("V2 draw range too large");
        gl.DrawArrays(GL_TRIANGLES, draw.firstVertex, draw.vertexCount);
        ++gl.counters.drawCalls;
        if (gl.residentProgram && gl.epoch) ++gl.counters.residentMovedDrawCalls;
    }
    gl.check("V2 ordered rendering");
}
void GlRenderer::abandonContext() {
    auto& gl = *m_impl;
    // Resources belong to the lost context. Calling its deletion functions in
    // a replacement context could delete unrelated names owned by the host.
    gl.program = gl.geometry = gl.palette = gl.vao = 0;
    gl.residentProgram = gl.gatherProgram = gl.applyProgram = 0;
    gl.storage = {};
    gl.sectionEvents = 0;
    gl.generation = 0;
}
void GlRenderer::clearCounters() { m_impl->counters = {}; }
const RenderCounters& GlRenderer::counters() const { return m_impl->counters; }
}
