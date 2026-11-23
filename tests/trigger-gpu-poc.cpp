#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/backend/TriggerTransformShader.hpp"
#include "../src/backend/TriggerTransformTypes.hpp"

namespace {
using vulkanizer::gpu::Action;
using vulkanizer::gpu::ObjectState;
using vulkanizer::gpu::Span;
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;
constexpr GLenum GL_MAJOR_VERSION = 0x821B;
constexpr GLenum GL_MINOR_VERSION = 0x821C;
constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
constexpr GLenum GL_LINK_STATUS = 0x8B82;
constexpr GLenum GL_COMPUTE_SHADER = 0x91B9;
constexpr GLenum GL_SHADER_STORAGE_BUFFER = 0x90D2;
constexpr GLenum GL_DYNAMIC_COPY = 0x88EA;
constexpr GLbitfield GL_SHADER_STORAGE_BARRIER_BIT = 0x2000;
constexpr GLbitfield GL_BUFFER_UPDATE_BARRIER_BIT = 0x0200;
constexpr GLenum GL_TIME_ELAPSED = 0x88BF;
constexpr GLenum GL_CURRENT_QUERY = 0x8865;
constexpr GLenum GL_QUERY_RESULT = 0x8866;
constexpr GLenum GL_SYNC_GPU_COMMANDS_COMPLETE = 0x9117;
constexpr GLenum GL_ALREADY_SIGNALED = 0x911A;
constexpr GLenum GL_CONDITION_SATISFIED = 0x911C;
constexpr GLenum GL_TIMEOUT_EXPIRED = 0x911B;
constexpr GLenum GL_WAIT_FAILED = 0x911D;
constexpr GLbitfield GL_SYNC_FLUSH_COMMANDS_BIT = 0x00000001;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLuint64 = std::uint64_t;
using GLsync = void*;

using CreateContext = HGLRC (WINAPI*)(HDC);
using MakeCurrent = BOOL (WINAPI*)(HDC, HGLRC);
using DeleteContext = BOOL (WINAPI*)(HGLRC);
using GetWglProcAddress = PROC (WINAPI*)(LPCSTR);
using CreateContextAttribs = HGLRC (WINAPI*)(HDC, HGLRC, int const*);

void require(bool condition, char const* operation) {
    if (!condition)
        throw std::runtime_error(std::string(operation) + " failed; Win32=" + std::to_string(GetLastError()));
}

template <class T>
T exported(HMODULE module, char const* name) {
    auto value = GetProcAddress(module, name);
    if (!value) throw std::runtime_error(std::string("Missing OpenGL export: ") + name);
    return reinterpret_cast<T>(value);
}

template <class T>
T glProc(HMODULE module, GetWglProcAddress getProc, char const* name) {
    auto value = GetProcAddress(module, name);
    if (!value) value = getProc(name);
    auto address = reinterpret_cast<std::uintptr_t>(value);
    if (address <= 3 || address == static_cast<std::uintptr_t>(-1))
        throw std::runtime_error(std::string("Missing OpenGL 4.3 entry point: ") + name);
    return reinterpret_cast<T>(value);
}

struct WglContext {
    HMODULE library = nullptr;
    HINSTANCE module = GetModuleHandleW(nullptr);
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    bool registered = false;
    MakeCurrent makeCurrent = nullptr;
    DeleteContext deleteContext = nullptr;
    GetWglProcAddress getProc = nullptr;

    void initialize(std::filesystem::path const& provider) {
        library = LoadLibraryExW(provider.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        require(library != nullptr, "LoadLibraryExW(Zink opengl32.dll)");
        auto create = exported<CreateContext>(library, "wglCreateContext");
        makeCurrent = exported<MakeCurrent>(library, "wglMakeCurrent");
        deleteContext = exported<DeleteContext>(library, "wglDeleteContext");
        getProc = exported<GetWglProcAddress>(library, "wglGetProcAddress");

        WNDCLASSW klass{};
        klass.style = CS_OWNDC;
        klass.lpfnWndProc = DefWindowProcW;
        klass.hInstance = module;
        klass.lpszClassName = L"VulkanizerTriggerComputePoc";
        require(RegisterClassW(&klass) != 0, "RegisterClassW");
        registered = true;
        window = CreateWindowExW(0, klass.lpszClassName, L"Vulkanizer hidden trigger compute PoC",
            WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, module, nullptr);
        require(window != nullptr, "CreateWindowExW");
        dc = GetDC(window);
        require(dc != nullptr, "GetDC");

        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof(format);
        format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA;
        format.cColorBits = 32;
        format.cDepthBits = 24;
        format.cStencilBits = 8;
        auto index = ChoosePixelFormat(dc, &format);
        require(index != 0, "ChoosePixelFormat");
        require(SetPixelFormat(dc, index, &format) != FALSE, "SetPixelFormat");

        auto bootstrap = create(dc);
        require(bootstrap != nullptr, "wglCreateContext(bootstrap)");
        if (makeCurrent(dc, bootstrap) == FALSE) {
            deleteContext(bootstrap);
            throw std::runtime_error("wglMakeCurrent(bootstrap) failed");
        }
        auto createAttribs = reinterpret_cast<CreateContextAttribs>(getProc("wglCreateContextAttribsARB"));
        if (!createAttribs) {
            makeCurrent(nullptr, nullptr);
            deleteContext(bootstrap);
            throw std::runtime_error("WGL_ARB_create_context is unavailable; compute needs OpenGL 4.3");
        }
        int attributes[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
        context = createAttribs(dc, nullptr, attributes);
        if (!context) {
            makeCurrent(nullptr, nullptr);
            deleteContext(bootstrap);
            throw std::runtime_error("Unable to create an OpenGL 4.3 core context");
        }
        if (makeCurrent(dc, context) == FALSE) {
            deleteContext(context);
            context = nullptr;
            makeCurrent(dc, bootstrap);
            makeCurrent(nullptr, nullptr);
            deleteContext(bootstrap);
            throw std::runtime_error("wglMakeCurrent(compute) failed");
        }
        require(deleteContext(bootstrap) != FALSE, "wglDeleteContext(bootstrap)");
    }

    bool release() {
        bool ok = true;
        if (context) {
            ok = makeCurrent(nullptr, nullptr) != FALSE && ok;
            ok = deleteContext(context) != FALSE && ok;
            context = nullptr;
        }
        if (dc && window) { ok = ReleaseDC(window, dc) != 0 && ok; dc = nullptr; }
        if (window) { ok = DestroyWindow(window) != FALSE && ok; window = nullptr; }
        if (registered) { ok = UnregisterClassW(L"VulkanizerTriggerComputePoc", module) != FALSE && ok; registered = false; }
        if (library) { ok = FreeLibrary(library) != FALSE && ok; library = nullptr; }
        return ok;
    }
    ~WglContext() { release(); }
};

struct GlApi {
    using GetStringFn = GLubyte const* (APIENTRY*)(GLenum);
    using GetIntegervFn = void (APIENTRY*)(GLenum, GLint*);
    using CreateShaderFn = GLuint (APIENTRY*)(GLenum);
    using ShaderSourceFn = void (APIENTRY*)(GLuint, GLsizei, GLchar const* const*, GLint const*);
    using CompileShaderFn = void (APIENTRY*)(GLuint);
    using GetShaderivFn = void (APIENTRY*)(GLuint, GLenum, GLint*);
    using GetShaderInfoLogFn = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
    using CreateProgramFn = GLuint (APIENTRY*)();
    using AttachShaderFn = void (APIENTRY*)(GLuint, GLuint);
    using LinkProgramFn = void (APIENTRY*)(GLuint);
    using GetProgramivFn = void (APIENTRY*)(GLuint, GLenum, GLint*);
    using GetProgramInfoLogFn = void (APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
    using UseProgramFn = void (APIENTRY*)(GLuint);
    using GetUniformLocationFn = GLint (APIENTRY*)(GLuint, GLchar const*);
    using Uniform1uiFn = void (APIENTRY*)(GLint, GLuint);
    using GenBuffersFn = void (APIENTRY*)(GLsizei, GLuint*);
    using BindBufferFn = void (APIENTRY*)(GLenum, GLuint);
    using BufferDataFn = void (APIENTRY*)(GLenum, GLsizeiptr, void const*, GLenum);
    using BufferSubDataFn = void (APIENTRY*)(GLenum, std::ptrdiff_t, GLsizeiptr, void const*);
    using BindBufferBaseFn = void (APIENTRY*)(GLenum, GLuint, GLuint);
    using DispatchComputeFn = void (APIENTRY*)(GLuint, GLuint, GLuint);
    using MemoryBarrierFn = void (APIENTRY*)(GLbitfield);
    using GetBufferSubDataFn = void (APIENTRY*)(GLenum, std::ptrdiff_t, GLsizeiptr, void*);
    using DeleteBuffersFn = void (APIENTRY*)(GLsizei, GLuint const*);
    using DeleteProgramFn = void (APIENTRY*)(GLuint);
    using DeleteShaderFn = void (APIENTRY*)(GLuint);
    using GetErrorFn = GLenum (APIENTRY*)();
    using DeleteQueriesFn = void (APIENTRY*)(GLsizei, GLuint const*);
    using GenQueriesFn = void (APIENTRY*)(GLsizei, GLuint*);
    using BeginQueryFn = void (APIENTRY*)(GLenum, GLuint);
    using EndQueryFn = void (APIENTRY*)(GLenum);
    using GetQueryivFn = void (APIENTRY*)(GLenum, GLenum, GLint*);
    using GetQueryObjectui64vFn = void (APIENTRY*)(GLuint, GLenum, GLuint64*);
    using FenceSyncFn = GLsync (APIENTRY*)(GLenum, GLbitfield);
    using ClientWaitSyncFn = GLenum (APIENTRY*)(GLsync, GLbitfield, GLuint64);
    using DeleteSyncFn = void (APIENTRY*)(GLsync);

    GetStringFn getString;
    GetIntegervFn getIntegerv;
    CreateShaderFn createShader;
    ShaderSourceFn shaderSource;
    CompileShaderFn compileShader;
    GetShaderivFn getShaderiv;
    GetShaderInfoLogFn getShaderInfoLog;
    CreateProgramFn createProgram;
    AttachShaderFn attachShader;
    LinkProgramFn linkProgram;
    GetProgramivFn getProgramiv;
    GetProgramInfoLogFn getProgramInfoLog;
    UseProgramFn useProgram;
    GetUniformLocationFn getUniformLocation;
    Uniform1uiFn uniform1ui;
    GenBuffersFn genBuffers;
    BindBufferFn bindBuffer;
    BufferDataFn bufferData;
    BufferSubDataFn bufferSubData;
    BindBufferBaseFn bindBufferBase;
    DispatchComputeFn dispatchCompute;
    MemoryBarrierFn memoryBarrier;
    GetBufferSubDataFn getBufferSubData;
    DeleteBuffersFn deleteBuffers;
    DeleteProgramFn deleteProgram;
    DeleteShaderFn deleteShader;
    GetErrorFn getError;
    DeleteQueriesFn deleteQueries;
    GenQueriesFn genQueries;
    BeginQueryFn beginQuery;
    EndQueryFn endQuery;
    GetQueryivFn getQueryiv;
    GetQueryObjectui64vFn getQueryObjectui64v;
    FenceSyncFn fenceSync;
    ClientWaitSyncFn clientWaitSync;
    DeleteSyncFn deleteSync;

    explicit GlApi(WglContext& context) {
#define LOAD_GL(field, type, name) field = glProc<type>(context.library, context.getProc, name)
        LOAD_GL(getString, GetStringFn, "glGetString");
        LOAD_GL(getIntegerv, GetIntegervFn, "glGetIntegerv");
        LOAD_GL(createShader, CreateShaderFn, "glCreateShader");
        LOAD_GL(shaderSource, ShaderSourceFn, "glShaderSource");
        LOAD_GL(compileShader, CompileShaderFn, "glCompileShader");
        LOAD_GL(getShaderiv, GetShaderivFn, "glGetShaderiv");
        LOAD_GL(getShaderInfoLog, GetShaderInfoLogFn, "glGetShaderInfoLog");
        LOAD_GL(createProgram, CreateProgramFn, "glCreateProgram");
        LOAD_GL(attachShader, AttachShaderFn, "glAttachShader");
        LOAD_GL(linkProgram, LinkProgramFn, "glLinkProgram");
        LOAD_GL(getProgramiv, GetProgramivFn, "glGetProgramiv");
        LOAD_GL(getProgramInfoLog, GetProgramInfoLogFn, "glGetProgramInfoLog");
        LOAD_GL(useProgram, UseProgramFn, "glUseProgram");
        LOAD_GL(getUniformLocation, GetUniformLocationFn, "glGetUniformLocation");
        LOAD_GL(uniform1ui, Uniform1uiFn, "glUniform1ui");
        LOAD_GL(genBuffers, GenBuffersFn, "glGenBuffers");
        LOAD_GL(bindBuffer, BindBufferFn, "glBindBuffer");
        LOAD_GL(bufferData, BufferDataFn, "glBufferData");
        LOAD_GL(bufferSubData, BufferSubDataFn, "glBufferSubData");
        LOAD_GL(bindBufferBase, BindBufferBaseFn, "glBindBufferBase");
        LOAD_GL(dispatchCompute, DispatchComputeFn, "glDispatchCompute");
        LOAD_GL(memoryBarrier, MemoryBarrierFn, "glMemoryBarrier");
        LOAD_GL(getBufferSubData, GetBufferSubDataFn, "glGetBufferSubData");
        LOAD_GL(deleteBuffers, DeleteBuffersFn, "glDeleteBuffers");
        LOAD_GL(deleteProgram, DeleteProgramFn, "glDeleteProgram");
        LOAD_GL(deleteShader, DeleteShaderFn, "glDeleteShader");
        LOAD_GL(getError, GetErrorFn, "glGetError");
        LOAD_GL(deleteQueries, DeleteQueriesFn, "glDeleteQueries");
        LOAD_GL(genQueries, GenQueriesFn, "glGenQueries");
        LOAD_GL(beginQuery, BeginQueryFn, "glBeginQuery");
        LOAD_GL(endQuery, EndQueryFn, "glEndQuery");
        LOAD_GL(getQueryiv, GetQueryivFn, "glGetQueryiv");
        LOAD_GL(getQueryObjectui64v, GetQueryObjectui64vFn, "glGetQueryObjectui64v");
        LOAD_GL(fenceSync, FenceSyncFn, "glFenceSync");
        LOAD_GL(clientWaitSync, ClientWaitSyncFn, "glClientWaitSync");
        LOAD_GL(deleteSync, DeleteSyncFn, "glDeleteSync");
#undef LOAD_GL
    }
};

void checkGl(GlApi const& gl, char const* stage) {
    GLenum error = gl.getError();
    if (error != GL_NO_ERROR) {
        constexpr char digits[] = "0123456789abcdef";
        std::string formatted(4, '0');
        for (int i = 3; i >= 0; --i) {
            formatted[static_cast<std::size_t>(i)] = digits[error & 0xfu];
            error >>= 4;
        }
        throw std::runtime_error(std::string("OpenGL error at ") + stage + ": 0x" + formatted);
    }
}

template <class T>
GLuint makeStorageBuffer(GlApi const& gl, GLuint binding, std::vector<T> const& data) {
    GLuint buffer = 0;
    gl.genBuffers(1, &buffer);
    gl.bindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    gl.bufferData(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(data.size() * sizeof(T)), data.data(), GL_DYNAMIC_COPY);
    gl.bindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
    return buffer;
}

GLuint compileProgram(GlApi const& gl) {
    GLuint shader = gl.createShader(GL_COMPUTE_SHADER);
    GLchar const* source = vulkanizer::gpu::triggerTransformComputeSource;
    GLint sourceLength = static_cast<GLint>(std::char_traits<char>::length(source));
    gl.shaderSource(shader, 1, &source, &sourceLength);
    gl.compileShader(shader);
    GLint compiled = GL_FALSE;
    gl.getShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        std::array<GLchar, 4096> log{};
        GLsizei length = 0;
        gl.getShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
        gl.deleteShader(shader);
        throw std::runtime_error(std::string("Compute shader compile failed: ") + log.data());
    }
    GLuint program = gl.createProgram();
    gl.attachShader(program, shader);
    gl.linkProgram(program);
    GLint linked = GL_FALSE;
    gl.getProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        std::array<GLchar, 4096> log{};
        GLsizei length = 0;
        gl.getProgramInfoLog(program, static_cast<GLsizei>(log.size()), &length, log.data());
        gl.deleteProgram(program);
        gl.deleteShader(shader);
        throw std::runtime_error(std::string("Compute program link failed: ") + log.data());
    }
    gl.deleteShader(shader);
    return program;
}

float quarterTurnCosine(unsigned value) {
    constexpr std::array<float, 4> values{1.f, 0.f, -1.f, 0.f};
    return values[value & 3u];
}
float quarterTurnSine(unsigned value) {
    constexpr std::array<float, 4> values{0.f, 1.f, 0.f, -1.f};
    return values[value & 3u];
}

void applyAction(ObjectState& object, Action const& action) {
    if (action.kind == 0u) {
        if ((action.flags & 4u) == 0u)
            object.x = object.x + action.dx;
        object.y = object.y + action.dy;
    } else if ((action.flags & 2u) != 0u) {
                float baseX = static_cast<float>(object.x - static_cast<double>(action.objectOffsetX));
                float baseY = static_cast<float>(object.y - static_cast<double>(action.objectOffsetY));
                float relativeX = baseX - static_cast<float>(action.centerX);
                float relativeY = baseY - static_cast<float>(action.centerY);
                volatile float x0 = action.matrixA * relativeX;
                volatile float x1 = action.matrixC * relativeY;
                volatile float y0 = action.matrixB * relativeX;
                volatile float y1 = action.matrixD * relativeY;
                float rotatedX = (x1 + x0) + action.matrixTx;
                float rotatedY = (y1 + y0) + action.matrixTy;
                float translatedX = rotatedX + static_cast<float>(action.dx);
                float translatedY = rotatedY + static_cast<float>(action.dy);
                double deltaX = static_cast<double>(translatedX - baseX);
                double deltaY = static_cast<double>(translatedY - baseY);
                object.x = object.x + deltaX;
                object.y = object.y + deltaY;
                if ((action.flags & 1u) == 0u)
                    object.rotation = object.rotation + action.rotationDelta;
    } else {
                float relativeX = static_cast<float>(object.x - action.centerX);
                float relativeY = static_cast<float>(object.y - action.centerY);
                float xCos = action.cosine * relativeX;
                float ySin = action.sine * relativeY;
                float xSin = action.sine * relativeX;
                float yCos = action.cosine * relativeY;
                float deltaX = (xCos - ySin) - relativeX;
                float deltaY = (xSin + yCos) - relativeY;
                object.x = object.x + static_cast<double>(deltaX);
                object.y = object.y + static_cast<double>(deltaY);
                if ((action.flags & 1u) == 0u)
                    object.rotation = object.rotation + action.rotationDelta;
    }
}

void applyCpu(std::vector<ObjectState>& objects, std::vector<Action> const& actions,
              std::vector<Span> const& spans, std::vector<std::uint32_t> const& indices) {
    for (std::size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex) {
        auto const span = spans[objectIndex];
        for (std::uint32_t i = 0; i < span.count; ++i)
            applyAction(objects[objectIndex], actions[indices[span.offset + i]]);
    }
}

bool sameBits(double left, double right);
bool sameBits(float left, float right);

enum class ScaleCase { Move, Rotation, Combined };
struct ScaleFilter {
    ScaleCase kind;
    std::size_t objects;
    bool explicitFence;
    bool reuseBuffers;
};

char const* caseName(ScaleCase kind) {
    switch (kind) {
        case ScaleCase::Move: return "move";
        case ScaleCase::Rotation: return "rotation_pivot";
        case ScaleCase::Combined: return "ordered_move_rotation";
    }
    return "unknown";
}
std::uint32_t actionsPerObject(ScaleCase kind) {
    return kind == ScaleCase::Combined ? 4u : 1u;
}

Action makeScaleAction(std::size_t objectIndex, std::uint32_t step, ScaleCase kind) {
    Action action{};
    bool rotate = kind == ScaleCase::Rotation ||
        (kind == ScaleCase::Combined && (step & 1u) != 0u);
    if (!rotate) {
        action.kind = 0;
        action.flags = (objectIndex % 19u == 0u) ? 4u : 0u;
        action.dx = static_cast<double>(static_cast<int>((objectIndex + step) % 7u) - 3) * 0.25;
        action.dy = static_cast<double>(static_cast<int>((objectIndex * 3u + step) % 9u) - 4) * 0.125;
        return action;
    }

    // One fixed pivot, independent of the transformed object set. This models
    // a separate pivot group resolved by the CPU before the GPU dispatch.
    constexpr double pivotX = 17.25;
    constexpr double pivotY = -9.5;
    constexpr float cosine = 0.f;
    constexpr float sine = 1.f;
    action.kind = 1;
    action.flags = 2u | ((objectIndex % 13u == 0u) ? 1u : 0u);
    action.dx = (step & 2u) ? -0.25 : 0.5;
    action.dy = (step & 2u) ? 0.375 : -0.125;
    action.centerX = pivotX;
    action.centerY = pivotY;
    action.cosine = cosine;
    action.sine = sine;
    action.rotationDelta = (step & 2u) ? -90.f : 90.f;
    action.matrixA = cosine;
    action.matrixB = sine;
    action.matrixC = -sine;
    action.matrixD = cosine;
    // Cocos affine transform for a quarter-turn about (pivotX, pivotY).
    action.matrixTx = static_cast<float>(pivotX + pivotY);
    action.matrixTy = static_cast<float>(pivotY - pivotX);
    action.objectOffsetX = static_cast<float>(static_cast<int>(objectIndex % 5u) - 2) * 0.125f;
    action.objectOffsetY = static_cast<float>(static_cast<int>(objectIndex % 7u) - 3) * 0.0625f;
    return action;
}

struct ScaleWorkload {
    std::vector<ObjectState> initial;
    std::vector<Action> actions;
    std::vector<Span> spans;
    std::vector<std::uint32_t> indices;
};

std::vector<ObjectState> makeScaleInitialStates(std::size_t objectCount) {
    std::vector<ObjectState> initial;
    initial.reserve(objectCount);
    for (std::size_t object = 0; object < objectCount; ++object) {
        initial.push_back({
            static_cast<double>(static_cast<int>((object * 17u) % 1009u) - 504) + 0.375,
            static_cast<double>(static_cast<int>((object * 29u) % 1013u) - 506) - 0.625,
            static_cast<float>((object % 8u) * 45u), 0.f
        });
    }
    return initial;
}

ScaleWorkload prepareScaleWorkload(std::vector<ObjectState> const& initial, ScaleCase kind) {
    ScaleWorkload workload;
    std::size_t objectCount = initial.size();
    auto perObject = actionsPerObject(kind);
    workload.initial = initial;
    workload.actions.reserve(objectCount * perObject);
    workload.spans.reserve(objectCount);
    workload.indices.reserve(objectCount * perObject);
    for (std::size_t object = 0; object < objectCount; ++object) {
        auto offset = static_cast<std::uint32_t>(workload.actions.size());
        for (std::uint32_t step = 0; step < perObject; ++step) {
            workload.actions.push_back(makeScaleAction(object, step, kind));
            workload.indices.push_back(offset + step);
        }
        workload.spans.push_back({offset, perObject});
    }
    return workload;
}

void applyCpuContiguous(std::vector<ObjectState>& objects,
                        std::vector<Action> const& actions, ScaleCase kind) {
    auto perObject = actionsPerObject(kind);
    for (std::size_t object = 0; object < objects.size(); ++object)
        for (std::uint32_t step = 0; step < perObject; ++step)
            applyAction(objects[object], actions[object * perObject + step]);
}

void applyCpuContiguous(ObjectState* objects, std::size_t objectCount,
                        std::vector<Action> const& actions, ScaleCase kind) {
    auto perObject = actionsPerObject(kind);
    for (std::size_t object = 0; object < objectCount; ++object)
        for (std::uint32_t step = 0; step < perObject; ++step)
            applyAction(objects[object], actions[object * perObject + step]);
}

struct ProxyObject {
    ObjectState state{};
    std::uint64_t callbackCount = 0;
    bool dirty = false;
};

void applyStateAndCallbackProxy(ObjectState const* results, std::size_t objectCount,
                                ProxyObject* authoritative) {
    for (std::size_t i = 0; i < objectCount; ++i) {
        authoritative[i].state = results[i];
        authoritative[i].dirty = true;
        ++authoritative[i].callbackCount; // proxy for one bulk helper's section/state callback
    }
}

double timeStateAndCallbackProxy(ObjectState const* results, std::size_t objectCount,
                                 ProxyObject* authoritative) {
    auto start = std::chrono::steady_clock::now();
    applyStateAndCallbackProxy(results, objectCount, authoritative);
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
}

struct GpuSample {
    double prepareMs = 0;
    double uploadMs = 0;
    double submitMs = 0;
    double computeMs = 0;
    double waitMs = 0;
    double readbackMs = 0;
    double applyMs = 0;
    double totalMs = 0;
    std::uint64_t bufferAllocations = 0;
};

double medianMetric(std::vector<GpuSample> const& samples, double GpuSample::*field) {
    std::vector<double> values;
    values.reserve(samples.size());
    for (auto const& sample : samples) values.push_back(sample.*field);
    std::sort(values.begin(), values.end());
    if (values.empty()) return 0;
    auto middle = values.size() / 2;
    return (values.size() & 1u) ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

struct ScaleGpu {
    GlApi& gl;
    GLuint program = 0;
    std::array<GLuint, 4> buffers{};
    std::array<std::size_t, 4> capacity{};
    GLuint query = 0;

    explicit ScaleGpu(GlApi& api) : gl(api), program(compileProgram(api)) {
        gl.genBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        gl.genQueries(1, &query);
    }
    ~ScaleGpu() {
        gl.deleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (query) gl.deleteQueries(1, &query);
        gl.deleteProgram(program);
    }

    GpuSample run(std::vector<ObjectState> const& initial, ScaleCase kind, bool reuseBuffers, bool explicitFence,
                  std::vector<ProxyObject>& authoritative) {
        using Clock = std::chrono::steady_clock;
        auto objectCount = initial.size();
        auto totalStart = Clock::now();
        auto prepareStart = Clock::now();
        auto workload = prepareScaleWorkload(initial, kind);
        std::vector<ObjectState> results = workload.initial;
        auto prepareEnd = Clock::now();
        GpuSample sample{};
        sample.prepareMs = std::chrono::duration<double, std::milli>(prepareEnd - prepareStart).count();

        std::array<void const*, 4> data{results.data(), workload.actions.data(),
            workload.spans.data(), workload.indices.data()};
        std::array<std::size_t, 4> bytes{results.size() * sizeof(ObjectState),
            workload.actions.size() * sizeof(Action), workload.spans.size() * sizeof(Span),
            workload.indices.size() * sizeof(std::uint32_t)};
        auto uploadStart = Clock::now();
        for (GLuint binding = 0; binding < buffers.size(); ++binding) {
            gl.bindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[binding]);
            if (!reuseBuffers) {
                gl.bufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(bytes[binding]),
                              data[binding], GL_DYNAMIC_COPY);
                ++sample.bufferAllocations;
            } else {
                if (bytes[binding] > capacity[binding]) {
                    std::size_t grown = std::max<std::size_t>(capacity[binding], 256);
                    while (grown < bytes[binding]) grown *= 2;
                    gl.bufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(grown),
                                  nullptr, GL_DYNAMIC_COPY);
                    capacity[binding] = grown;
                    ++sample.bufferAllocations;
                }
                gl.bufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(bytes[binding]),
                                 data[binding]);
            }
            gl.bindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffers[binding]);
        }
        auto uploadEnd = Clock::now();
        sample.uploadMs = std::chrono::duration<double, std::milli>(uploadEnd - uploadStart).count();
        checkGl(gl, "scaling buffer upload");

        gl.useProgram(program);
        gl.uniform1ui(gl.getUniformLocation(program, "objectCount"), static_cast<GLuint>(objectCount));
        gl.beginQuery(GL_TIME_ELAPSED, query);
        auto submitStart = Clock::now();
        gl.dispatchCompute(static_cast<GLuint>((objectCount + 63u) / 64u), 1, 1);
        gl.endQuery(GL_TIME_ELAPSED);
        gl.memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        auto submitEnd = Clock::now();
        sample.submitMs = std::chrono::duration<double, std::milli>(submitEnd - submitStart).count();
        checkGl(gl, "scaling compute submission");

        if (explicitFence) {
            auto fence = gl.fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (!fence) throw std::runtime_error("glFenceSync failed during scaling benchmark");
            auto waitStart = Clock::now();
            GLenum result = gl.clientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, 500'000'000ull);
            auto waitEnd = Clock::now();
            gl.deleteSync(fence);
            sample.waitMs = std::chrono::duration<double, std::milli>(waitEnd - waitStart).count();
            if (result != GL_ALREADY_SIGNALED && result != GL_CONDITION_SATISFIED)
                throw std::runtime_error(result == GL_TIMEOUT_EXPIRED
                    ? "GPU fence exceeded the 500 ms benchmark safety timeout"
                    : "GPU fence wait failed during scaling benchmark");
            checkGl(gl, "scaling fence wait");
        }

        auto readbackStart = Clock::now();
        gl.bindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
        gl.getBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(bytes[0]), results.data());
        auto readbackEnd = Clock::now();
        sample.readbackMs = std::chrono::duration<double, std::milli>(readbackEnd - readbackStart).count();
        checkGl(gl, "scaling result readback");
        sample.applyMs = timeStateAndCallbackProxy(results.data(), results.size(), authoritative.data());
        GLuint64 elapsedNs = 0;
        gl.getQueryObjectui64v(query, GL_QUERY_RESULT, &elapsedNs);
        sample.computeMs = static_cast<double>(elapsedNs) / 1'000'000.0;
        sample.totalMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
        checkGl(gl, "scaling benchmark completion");
        return sample;
    }
};

double mean(std::vector<double> const& values) {
    double total = 0;
    for (auto value : values) total += value;
    return values.empty() ? 0 : total / static_cast<double>(values.size());
}

void runScaleBench(GlApi& gl, std::optional<ScaleFilter> filter = std::nullopt) {
    constexpr std::array<std::size_t, 4> sizes{1, 100, 1000, 10000};
    constexpr unsigned repeats = 3;
    ScaleGpu gpu(gl);
    bool emitted = false;
    for (auto kind : {ScaleCase::Move, ScaleCase::Rotation, ScaleCase::Combined}) {
        if (filter && kind != filter->kind) continue;
        for (auto size : sizes) {
            if (filter && size != filter->objects) continue;
            auto initial = makeScaleInitialStates(size);
            auto reference = prepareScaleWorkload(initial, kind);
            auto entriesPerIteration = size * actionsPerObject(kind);
            auto cpuIterations = std::max<std::size_t>(1,
                (100'000 + entriesPerIteration - 1) / entriesPerIteration);
            std::vector<ObjectState> cpuPool;
            cpuPool.reserve(size * cpuIterations);
            std::vector<ProxyObject> cpuAuthoritative(size * cpuIterations);
            for (std::size_t iteration = 0; iteration < cpuIterations; ++iteration) {
                cpuPool.insert(cpuPool.end(), initial.begin(), initial.end());
                for (std::size_t object = 0; object < size; ++object)
                    cpuAuthoritative[iteration * size + object].state = initial[object];
            }
            std::vector<double> cpuMath, cpuApply;
            for (unsigned repeat = 0; repeat < repeats; ++repeat) {
                for (std::size_t iteration = 0; iteration < cpuIterations; ++iteration)
                    std::copy(initial.begin(), initial.end(), cpuPool.begin() + iteration * size);
                auto mathStart = std::chrono::steady_clock::now();
                for (std::size_t iteration = 0; iteration < cpuIterations; ++iteration)
                    applyCpuContiguous(cpuPool.data() + iteration * size, size, reference.actions, kind);
                auto mathEnd = std::chrono::steady_clock::now();
                cpuMath.push_back(std::chrono::duration<double, std::milli>(mathEnd - mathStart).count()
                                  / static_cast<double>(cpuIterations));
                auto applyStart = std::chrono::steady_clock::now();
                for (std::size_t iteration = 0; iteration < cpuIterations; ++iteration)
                    applyStateAndCallbackProxy(cpuPool.data() + iteration * size, size,
                                               cpuAuthoritative.data() + iteration * size);
                auto applyEnd = std::chrono::steady_clock::now();
                cpuApply.push_back(std::chrono::duration<double, std::milli>(applyEnd - applyStart).count()
                                   / static_cast<double>(cpuIterations));
            }
            const auto cpuMathAvg = mean(cpuMath);
            const auto cpuApplyAvg = mean(cpuApply);

            for (auto [reuse, explicitFence] : {
                    std::pair{true, true}, std::pair{true, false}, std::pair{false, true}}) {
                    if (filter && (reuse != filter->reuseBuffers ||
                                   explicitFence != filter->explicitFence)) continue;
                    std::vector<ProxyObject> gpuAuthoritative(size);
                    for (std::size_t i = 0; i < size; ++i) gpuAuthoritative[i].state = reference.initial[i];
                    (void)gpu.run(initial, kind, reuse, explicitFence, gpuAuthoritative); // warm-up and buffer growth
                    std::vector<GpuSample> samples;
                    samples.reserve(repeats);
                    bool parity = true;
                    for (unsigned repeat = 0; repeat < repeats; ++repeat) {
                        auto sample = gpu.run(initial, kind, reuse, explicitFence, gpuAuthoritative);
                        auto const* expected = cpuPool.data();
                        for (std::size_t i = 0; i < size; ++i) {
                            if (!sameBits(expected[i].x, gpuAuthoritative[i].state.x) ||
                                !sameBits(expected[i].y, gpuAuthoritative[i].state.y) ||
                                !sameBits(expected[i].rotation, gpuAuthoritative[i].state.rotation)) {
                                parity = false;
                                break;
                            }
                        }
                        if (!parity) throw std::runtime_error("scale workload CPU/GPU state parity failed");
                        samples.push_back(sample);
                    }
                    double allocationTotal = 0;
                    for (auto const& sample : samples) allocationTotal += sample.bufferAllocations;
                    double allocationAvg = allocationTotal / static_cast<double>(samples.size());
                    std::cout << "TRIGGER_GPU_SCALE_PASS case=" << caseName(kind)
                        << " sync=" << (explicitFence ? "explicit_fence" : "implicit_readback")
                        << " buffers=" << (reuse ? "reused" : "reallocated")
                        << " objects=" << size
                        << " action_entries=" << size * actionsPerObject(kind)
                        << " callbacks_per_iteration=" << size
                        << " cpu_iterations_per_sample=" << cpuIterations
                        << " cpu_action_evaluations_per_sample=" << cpuIterations * entriesPerIteration
                        << " measured_repeats=" << repeats
                        << " batches_per_iteration=1 readbacks_per_iteration=1 exact_parity=" << (parity ? 1 : 0)
                        << " cpu_math_ms=" << cpuMathAvg
                        << " cpu_state_apply_callback_proxy_ms=" << cpuApplyAvg
                        << " cpu_total_ms=" << cpuMathAvg + cpuApplyAvg
                        << " gpu_prepare_ms=" << medianMetric(samples, &GpuSample::prepareMs)
                        << " gpu_upload_ms=" << medianMetric(samples, &GpuSample::uploadMs)
                        << " gpu_dispatch_submit_ms=" << medianMetric(samples, &GpuSample::submitMs)
                        << " gpu_compute_ms=" << medianMetric(samples, &GpuSample::computeMs)
                        << " gpu_fence_wait_ms=" << medianMetric(samples, &GpuSample::waitMs)
                        << " gpu_readback_ms=" << medianMetric(samples, &GpuSample::readbackMs)
                        << " readback_timing=" << (explicitFence ? "post_fence_transfer" : "transfer_plus_implicit_wait")
                        << " gpu_state_apply_callback_proxy_ms=" << medianMetric(samples, &GpuSample::applyMs)
                        << " hybrid_total_ms=" << medianMetric(samples, &GpuSample::totalMs)
                        << " buffer_allocations_avg=" << allocationAvg
                        << " gpu_timing_summary=median_of_3;compute_overlaps_fence_wait"
                        << " callback_note=synthetic_one_per_object;not_Cocos_callbacks"
                        << " action_note=" << (kind == ScaleCase::Combined
                            ? "ordered_actions_final_state_only_not_cross_trigger_batching" : "single_helper_shape")
                        << '\n';
                    emitted = true;
            }
        }
    }
    if (!emitted) throw std::runtime_error("No scaling benchmark case matched the requested filter");
    gl.useProgram(0);
}

void makeWorkload(std::vector<ObjectState>& objects, std::vector<Action>& actions,
                  std::vector<Span>& spans, std::vector<std::uint32_t>& indices) {
    constexpr std::size_t objectCount = 16384;
    constexpr unsigned triggerCount = 32;
    objects.reserve(objectCount);
    spans.resize(objectCount);
    std::vector<std::vector<std::uint32_t>> perObject(objectCount);
    for (std::size_t i = 0; i < objectCount; ++i) {
        objects.push_back({static_cast<double>(static_cast<int>(i % 101) - 50),
            static_cast<double>(static_cast<int>((i * 17) % 127) - 63),
            static_cast<float>((i % 4) * 90), 0});
    }
    objects[0] = {2.0, 0.0, 0.0f, 0.0f};

    actions.reserve(triggerCount);
    for (unsigned trigger = 0; trigger < triggerCount; ++trigger) {
        Action action{};
        action.kind = (trigger % 3u == 0u) ? 1u : 0u;
        action.flags = (trigger % 7u == 0u) ? 1u : 0u;
        action.dx = static_cast<double>(static_cast<int>(trigger % 5u) - 2);
        action.dy = static_cast<double>(static_cast<int>((trigger * 3u) % 7u) - 3);
        action.centerX = static_cast<double>(static_cast<int>(trigger % 9u) - 4);
        action.centerY = static_cast<double>(static_cast<int>((trigger * 5u) % 11u) - 5);
        action.cosine = quarterTurnCosine(trigger);
        action.sine = quarterTurnSine(trigger);
        action.rotationDelta = static_cast<float>((trigger % 2u == 0u) ? 90 : -90);
        if (action.kind == 1u) {
            action.flags |= 2u; // exercise the in-game resolved Cocos affine path
            action.matrixA = action.cosine;
            action.matrixB = action.sine;
            action.matrixC = -action.sine;
            action.matrixD = action.cosine;
            action.matrixTx = 0.25f;
            action.matrixTy = -0.5f;
            action.objectOffsetX = 0.375f;
            action.objectOffsetY = -0.125f;
            action.centerX += 0.25;
            action.centerY -= 0.5;
        }
        actions.push_back(action);

        // Object zero is a deliberate non-commuting order case: rotate, then move.
        // Other objects receive overlapping trigger groups in global trigger order.
        for (std::size_t object = 0; object < objectCount; ++object) {
            bool selected = object == 0 ? trigger < 2u
                : ((object * 13u + trigger * 7u) % 11u < 7u);
            if (selected) perObject[object].push_back(trigger);
        }
    }
    actions[0].kind = 1u;
    actions[0].flags = 2u;
    actions[0].centerX = 0;
    actions[0].centerY = 0;
    actions[0].dx = 0;
    actions[0].dy = 0;
    actions[0].cosine = 0;
    actions[0].sine = 1;
    actions[0].rotationDelta = 90;
    actions[0].matrixA = 0;
    actions[0].matrixB = 1;
    actions[0].matrixC = -1;
    actions[0].matrixD = 0;
    actions[0].matrixTx = 0;
    actions[0].matrixTy = 0;
    actions[0].objectOffsetX = 0;
    actions[0].objectOffsetY = 0;
    actions[1].kind = 0u;
    actions[1].dx = 1;
    actions[1].dy = 0;

    for (std::size_t object = 0; object < objectCount; ++object) {
        spans[object].offset = static_cast<std::uint32_t>(indices.size());
        spans[object].count = static_cast<std::uint32_t>(perObject[object].size());
        indices.insert(indices.end(), perObject[object].begin(), perObject[object].end());
    }
}

bool sameBits(double left, double right) {
    return std::bit_cast<std::uint64_t>(left) == std::bit_cast<std::uint64_t>(right);
}
bool sameBits(float left, float right) {
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 7) {
        std::wcerr << L"Usage: trigger-gpu-poc.exe <absolute Zink opengl32.dll> "
            L"[--scale-only <move|rotation_pivot|ordered_move_rotation> <objects> "
            L"<explicit_fence|implicit_readback> <reused|reallocated>]\n";
        return 2;
    }
    try {
        std::optional<ScaleFilter> filter;
        if (argc == 7) {
            if (std::wstring(argv[2]) != L"--scale-only")
                throw std::runtime_error("Unknown benchmark option");
            ScaleCase kind;
            std::wstring requestedCase(argv[3]);
            if (requestedCase == L"move") kind = ScaleCase::Move;
            else if (requestedCase == L"rotation_pivot") kind = ScaleCase::Rotation;
            else if (requestedCase == L"ordered_move_rotation") kind = ScaleCase::Combined;
            else throw std::runtime_error("Unknown scaling benchmark case");
            auto objects = static_cast<std::size_t>(std::stoull(argv[4]));
            if (objects == 0 || objects > 1000000)
                throw std::runtime_error("Scaling benchmark object count must be in 1..1000000");
            std::wstring requestedSync(argv[5]);
            bool explicitFence;
            if (requestedSync == L"explicit_fence") explicitFence = true;
            else if (requestedSync == L"implicit_readback") explicitFence = false;
            else throw std::runtime_error("Unknown scaling benchmark synchronization mode");
            std::wstring requestedBuffers(argv[6]);
            bool reuseBuffers;
            if (requestedBuffers == L"reused") reuseBuffers = true;
            else if (requestedBuffers == L"reallocated") reuseBuffers = false;
            else throw std::runtime_error("Unknown scaling benchmark buffer mode");
            filter = ScaleFilter{kind, objects, explicitFence, reuseBuffers};
        }

        std::filesystem::path provider(argv[1]);
        if (!provider.is_absolute() || !std::filesystem::exists(provider))
            throw std::runtime_error("The Mesa Zink opengl32.dll path must be absolute and exist");

        WglContext context;
        context.initialize(provider);
        GlApi gl(context);
        auto const* rendererBytes = gl.getString(GL_RENDERER);
        auto const* versionBytes = gl.getString(GL_VERSION);
        if (!rendererBytes || !versionBytes) throw std::runtime_error("OpenGL renderer/version query failed");
        std::string renderer(reinterpret_cast<char const*>(rendererBytes));
        std::string version(reinterpret_cast<char const*>(versionBytes));
        if (renderer.find("zink") == std::string::npos && renderer.find("Zink") == std::string::npos)
            throw std::runtime_error("The compute context is not using Zink: " + renderer);
        GLint major = 0, minor = 0;
        gl.getIntegerv(GL_MAJOR_VERSION, &major);
        gl.getIntegerv(GL_MINOR_VERSION, &minor);
        if (major < 4 || (major == 4 && minor < 3))
            throw std::runtime_error("The Zink context does not expose OpenGL 4.3 compute");

        std::vector<ObjectState> initial, cpu, gpu;
        std::vector<Action> actions;
        std::vector<Span> spans;
        std::vector<std::uint32_t> indices;
        makeWorkload(initial, actions, spans, indices);
        cpu = initial;
        applyCpu(cpu, actions, spans, indices);

        GLuint program = compileProgram(gl);
        gl.useProgram(program);
        gl.uniform1ui(gl.getUniformLocation(program, "objectCount"), static_cast<GLuint>(initial.size()));
        std::array<GLuint, 4> buffers{
            makeStorageBuffer(gl, 0, initial),
            makeStorageBuffer(gl, 1, actions),
            makeStorageBuffer(gl, 2, spans),
            makeStorageBuffer(gl, 3, indices),
        };
        gl.dispatchCompute(static_cast<GLuint>((initial.size() + 63) / 64), 1, 1);
        gl.memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        gl.bindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
        gpu.resize(initial.size());
        gl.getBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GLsizeiptr>(initial.size() * sizeof(ObjectState)), gpu.data());
        if (gl.getError() != GL_NO_ERROR) throw std::runtime_error("OpenGL reported an error after compute/readback");
        if (cpu.empty() || !sameBits(cpu[0].x, 1.0) || !sameBits(cpu[0].y, 2.0))
            throw std::runtime_error("CPU order-case reference did not produce the expected (1, 2) position");
        std::size_t exactMatches = 0;
        for (std::size_t i = 0; i < cpu.size(); ++i) {
            if (sameBits(cpu[i].x, gpu[i].x) && sameBits(cpu[i].y, gpu[i].y) &&
                sameBits(cpu[i].rotation, gpu[i].rotation)) {
                ++exactMatches;
            } else {
                throw std::runtime_error("GPU result differs from ordered CPU reference at object " + std::to_string(i));
            }
        }
        require(SwapBuffers(context.dc) != FALSE, "SwapBuffers(hidden evidence surface)");
        std::uint64_t eventCount = indices.size();
        std::cout << std::fixed << std::setprecision(6) << std::unitbuf;
        std::cout << "TRIGGER_GPU_POC_PASS pid=" << GetCurrentProcessId()
                  << " hwnd=0x" << std::hex << reinterpret_cast<std::uintptr_t>(context.window) << std::dec
                  << " renderer=" << renderer << " version=" << version
                  << " objects=" << initial.size() << " actions=" << actions.size()
                  << " object_events=" << eventCount << " exact_matches=" << exactMatches
                  << " order_probe=(1,2)\n";
        runScaleBench(gl, filter);
        require(SwapBuffers(context.dc) != FALSE, "SwapBuffers(after scaling evidence)");
        gl.deleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        gl.deleteProgram(program);
        require(context.release(), "WGL resource cleanup");
        return 0;
    } catch (std::exception const& error) {
        std::cerr << "TRIGGER_GPU_POC_FAILED " << error.what() << '\n';
        return 1;
    }
}
