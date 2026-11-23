#include "RendererManager.hpp"
#include "backend/VulkanBackend.hpp"
#include "backend/HybridTransformPipeline.hpp"
#include "diagnostics/SwapApiProfiler.hpp"
#include "hooks/CpuRenderPhaseHooks.hpp"
#include "v2/LiveScene.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <fmt/format.h>
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string_view>

using namespace geode::prelude;
namespace vulkanizer {
namespace {
double ms(RendererManager::Clock::duration duration) { return std::chrono::duration<double, std::milli>(duration).count(); }
void report(std::string_view message) { log::info("{}", message); }
double calibrateClockPairOverheadNs() {
    constexpr size_t sampleCount = 31;
    constexpr size_t clockPairsPerSample = 256;
    std::array<double, sampleCount> samples{};
    for (auto& sample : samples) {
        auto start = RendererManager::Clock::now();
        auto end = start;
        for (size_t i = 0; i < clockPairsPerSample * 2; ++i)
            end = RendererManager::Clock::now();
        sample = std::chrono::duration<double, std::nano>(end - start).count() /
            static_cast<double>(clockPairsPerSample);
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

void logFrameSummary(const FrameSummary& summary, bool gameplay, std::string_view route) {
    log::info("VULKANIZER_FRAME_DIAGNOSTICS {}",
        serializeFrameSummary(summary, gameplay ? "gameplay" : "menu/other", route));
}

std::optional<bool> inspectOpenGL() {
    if (!wglGetCurrentContext()) { log::warn("No current OpenGL context at the frame hook"); return std::nullopt; }
    auto string = [](GLenum name) { auto* value = glGetString(name); return value ? reinterpret_cast<char const*>(value) : "unavailable"; };
    GLint viewport[4]{}, texture = 0;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    log::info("OpenGL vendor: {}; renderer: {}; version: {}", string(GL_VENDOR), string(GL_RENDERER), string(GL_VERSION));
    log::info("OpenGL snapshot: viewport=({},{},{},{}), texture2D={}, blending={}", viewport[0], viewport[1], viewport[2], viewport[3], texture, glIsEnabled(GL_BLEND) != 0);
    std::string renderer = string(GL_RENDERER);
    auto lower = renderer;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    bool zink = lower.find("zink") != std::string::npos;
    auto window = WindowFromDC(wglGetCurrentDC());
    wchar_t modulePath[32768]{};
    auto opengl = GetModuleHandleW(L"opengl32.dll");
    std::string module = "unavailable";
    if (opengl && GetModuleFileNameW(opengl, modulePath, 32768)) {
        int size = WideCharToMultiByte(CP_UTF8, 0, modulePath, -1, nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            module.resize(static_cast<size_t>(size));
            WideCharToMultiByte(CP_UTF8, 0, modulePath, -1, module.data(), size, nullptr, nullptr);
            module.pop_back();
        }
    }
    auto route = matjson::makeObject({
        {"schema", 1}, {"pid", static_cast<uint64_t>(GetCurrentProcessId())},
        {"hwnd", fmt::format("0x{:x}", reinterpret_cast<uintptr_t>(window))},
        {"renderer", renderer}, {"vendor", string(GL_VENDOR)}, {"opengl32", module},
        {"zink", zink}, {"preview", false}
    });
    log::info("GD_RENDER_ROUTE {}", route.dump(matjson::NO_INDENTATION));
    return zink;
}

bool isCpuRenderPhaseHook(std::string_view displayName) {
    static constexpr std::array<std::string_view, 19> names{
        "CCScheduler::update", "CCActionManager::update", "CCTimer::update",
        "GJBaseGameLayer::update", "GJBaseGameLayer::processCommands",
        "GJBaseGameLayer::preUpdateVisibility",
        "GJBaseGameLayer::processTransformActions",
        "GJBaseGameLayer::processAreaActions", "GJBaseGameLayer::processAreaEffects",
        "PlayLayer::updateColor", "CCNode::visit",
        "CCSprite::updateTransform", "CCSprite::draw", "CCSpriteBatchNode::draw",
        "CCTextureAtlas::drawNumberOfQuads", "GJBaseGameLayer::updateObjectSection",
        "GJBaseGameLayer::rotateObject", "GameObject::addRotation", "GameObject::updateOrientedBox"
    };
    for (const auto name : names) {
        if (displayName == name) return true;
        if (displayName.size() > name.size() && displayName.ends_with(name) &&
            displayName[displayName.size() - name.size() - 1] == ':') return true;
    }
    return false;
}

size_t disableCpuPhaseHooks() {
    size_t disabled = 0;
    for (auto* hook : Mod::get()->getHooks()) {
        if (v2::live::requested() &&
            (hook->getDisplayName().ends_with("CCTextureAtlas::drawNumberOfQuads") ||
             hook->getDisplayName().ends_with("CCSprite::updateTransform") ||
             hook->getDisplayName().ends_with("GJBaseGameLayer::preUpdateVisibility") ||
             hook->getDisplayName().ends_with("GJBaseGameLayer::updateObjectSection") ||
             hook->getDisplayName().ends_with("GameObject::addRotation"))) continue;
        // Functional resident draw/geometry hooks must survive diagnostic removal.
        if(Mod::get()->getSettingValue<bool>("experimental-resident-rendering") &&
            (hook->getDisplayName().ends_with("CCTextureAtlas::drawNumberOfQuads") ||
             hook->getDisplayName().ends_with("CCSprite::updateTransform")))continue;
        if ((std::getenv("VULKANIZER_GAMEPLAY_SPEED_AUDIT") || std::getenv("VULKANIZER_SCHEDULER_GAMEPLAY_TEST") || Mod::get()->getSettingValue<bool>("transform-cost-diagnostics")) &&
            hook->getDisplayName().ends_with("CCScheduler::update")) continue;
        if (!isCpuRenderPhaseHook(hook->getDisplayName()) || !hook->isEnabled()) continue;
        hook->setAutoEnable(false);
        auto result = hook->disable();
        if (!result) {
            log::warn("Could not remove disabled-mode diagnostic hook {}: {}", hook->getDisplayName(), result.unwrapErr());
            continue;
        }
        ++disabled;
    }
    return disabled;
}
}
RendererManager& RendererManager::get() { static RendererManager manager; return manager; }
void RendererManager::publishBackendCapability(api::BackendStateV1 state) {
    const auto previous = m_backendCapabilityState.exchange(state, std::memory_order_acq_rel);
    if (previous == state) return;
    char const* name = "unknown";
    switch (state) {
        case api::BackendStateV1::initializationPending: name = "initialization-pending"; break;
        case api::BackendStateV1::operational: name = "operational"; break;
        case api::BackendStateV1::openGLFallback: name = "opengl-fallback"; break;
        case api::BackendStateV1::initializationFailed: name = "initialization-failed"; break;
    }
    log::info("VULKANIZER_CAPABILITY_V1 state={}", name);
}
void RendererManager::initialize() {
    publishBackendCapability(api::BackendStateV1::initializationPending);
    m_experimental = Mod::get()->getSettingValue<bool>("experimental-renderer");
    const bool configuredDiagnostics = Mod::get()->getSettingValue<bool>("frame-diagnostics");
    m_diagnostics = !std::getenv("VULKANIZER_V2_PERFORMANCE_ONLY") && configuredDiagnostics;
    m_gpuTransformRequested = Mod::get()->getSettingValue<bool>("experimental-gpu-transform");
    m_gpuTransformAllRequested = Mod::get()->getSettingValue<bool>("experimental-gpu-transform-all");
    m_gpuTransformPilotGroup = Mod::get()->getSettingValue<int>("gpu-transform-pilot-group");
    if (Loader::get()->isModLoaded("iliashdz.bismuth")) {
        m_gpuTransformRequested = false;
        m_gpuTransformAllRequested = false;
        log::warn("Bismuth is loaded; Vulkanizer transform replacement is disabled to avoid competing renderer hooks");
    }
    m_zinkDetected = false;
    m_vulkanProbePassed = false;
    m_backendValidationFrames = 0;
    m_captureInvalid = false;
    m_firstFrame = true;
    m_stopped = false;
    m_haveLastFrameEnd = false;
    m_lastFrameEnd = {};
    m_playLayer = nullptr;
    m_pendingFrame = {};
    m_clockPairOverheadNs = calibrateClockPairOverheadNs();
    log::info("Experimental Zink session requested: {}; diagnostics: {}; GPU transform pilot: {}, group {}; all-object move/rotation: {}; checking GD context on first frame",
        m_experimental, m_diagnostics, m_gpuTransformRequested, m_gpuTransformPilotGroup, m_gpuTransformAllRequested);
    if (m_gpuTransformRequested && !m_gpuTransformAllRequested && m_gpuTransformPilotGroup <= 0)
        log::warn("GPU transform pilot is enabled but no group is selected; all move/rotation batches use vanilla processing");
    if (m_diagnostics) {
        m_profiler.reset();
        log::info("Vulkanizer CPU diagnostics schema={} profiler_abi={} windows=256/64/16 frames; high-frequency render timings sampled; clock-read overhead calibrated at {:.1f} ns/pair",
            frameDiagnosticsSchema, frameProfilerAbiVersion, m_clockPairOverheadNs);
    }
    if (m_diagnostics) {
        installSwapApiProfiler();
    } else if (!configuredDiagnostics) {
        const auto disabled = disableCpuPhaseHooks();
        log::info("Frame diagnostics are off; removed {} CPU phase hooks from the normal rendering path", disabled);
    }
    if (Loader::get()->isModLoaded("iliashdz.bismuth"))
        log::warn("Bismuth detected: rendering chain is modified; Cocos counter may omit its indirect draws. Compatibility/performance unverified.");
    // Capability probe only; no logical device, window, or game graphics changes.
    m_vulkanProbePassed = probeVulkan(report);
}
void RendererManager::beforeFrame() {
    m_cocosDraws.reset();
    m_frameSwapWallMs = 0;
    m_frameSystemSwapWallMs = 0;
    m_frameSwapCalls = 0;
    m_frameSystemSwapCalls = 0;
}
void RendererManager::recordSwapBuffers(double swapWallMs) {
    if (!diagnosticsEnabled()) return;
    m_frameSwapWallMs += swapWallMs;
    ++m_frameSwapCalls;
}
void RendererManager::recordSystemSwapBuffers(double swapWallMs) {
    if (!diagnosticsEnabled()) return;
    m_frameSystemSwapWallMs += swapWallMs;
    ++m_frameSystemSwapCalls;
}
void RendererManager::afterVanillaFrame() {
    if (m_stopped) return;
    if (!m_firstFrame) {
        if (backendCapabilityState() == api::BackendStateV1::operational &&
            ++m_backendValidationFrames >= 60) {
            m_backendValidationFrames = 0;
            if (!wglGetCurrentContext()) {
                m_zinkDetected = false;
                m_backend = "Vulkan backend unavailable (Geometry Dash WGL context was lost)";
                publishBackendCapability(api::BackendStateV1::initializationFailed);
                log::error("Vulkanizer active backend was revoked: Geometry Dash no longer has a current WGL context");
            }
        }
        return;
    }
    m_firstFrame = false;
    log::info("Vulkanizer CCDirector::drawScene hook executed; original drawScene preserved");
    const auto route = inspectOpenGL();
    if (!route) {
        m_zinkDetected = false;
        m_backend = "Backend initialization failed (no current Geometry Dash WGL context)";
        publishBackendCapability(api::BackendStateV1::initializationFailed);
        log::error("GD context route could not be inspected; VLE capability remains disabled");
        return;
    }
    const bool zink = *route;
    m_zinkDetected = zink;
    if (!zink) {
        m_backend = "OpenGL API (Vulkan fallback)";
        publishBackendCapability(api::BackendStateV1::openGLFallback);
    } else if (!m_vulkanProbePassed) {
        m_backend = "Zink route detected, but Vulkan initialization probe failed";
        publishBackendCapability(api::BackendStateV1::initializationFailed);
    } else {
        m_backend = "OpenGL -> Zink (Vulkan backend operational)";
        publishBackendCapability(api::BackendStateV1::operational);
    }
    log::info("GD context route: {}", m_backend);
    if (m_experimental && !zink)
        log::warn("Zink was requested but GD's context is not Zink. Keeping its current OpenGL driver; use the restart-only launcher to select Zink before process startup.");
    if (!m_experimental && zink)
        log::warn("Zink was selected outside the experimental session setting. Driver selection happened before mod load; return to the OpenGL launcher mode on restart.");
    if (gpuTransformRequested() && (!m_experimental || !zink))
        log::warn("Experimental GPU transforms are disabled for this session: they require both the Zink route and Experimental Zink session setting; vanilla move/rotation processing remains active");
}
void RendererManager::recordFrame(Clock::time_point originalStart, Clock::time_point originalEnd) {
    const auto phases = takeCpuRenderPhaseSample();
    auto* playLayer = PlayLayer::get();
    if (playLayer && !m_gameplayObserved) {
        m_gameplayObserved = true;
        log::info("Vulkanizer frame hook observed gameplay (PlayLayer present)");
    }
    const std::string_view route = m_backend.find("Zink") != std::string::npos ? "Zink" : "OpenGL";
    const bool sceneChanged = m_playLayer != playLayer;
    if (m_pendingFrame.valid) {
        auto update = m_profiler.add(m_pendingFrame.frameMs, m_pendingFrame.sceneMs,
            m_pendingFrame.swapMs, m_pendingFrame.systemSwapMs, m_pendingFrame.hookMs,
            m_pendingFrame.swapCalls, m_pendingFrame.systemSwapCalls, m_pendingFrame.cocosDraws,
            m_pendingFrame.phases, m_clockPairOverheadNs);
        const bool pendingGameplay = m_pendingFrame.gameplay;
        m_pendingFrame.valid = false;
        if (!update.captureValid) {
            m_captureInvalid = true;
            const auto callback = update.invalidCallback == SchedulerCallbackPhase::count
                ? std::string_view{"none"} : callbackPhaseName(update.invalidCallback);
            log::error("VULKANIZER_FRAME_DIAGNOSTICS_INVALID {{\"schema\":{},\"profiler_abi\":{},\"capture_valid\":false,\"reason\":\"{}\",\"callback\":\"{}\",\"callback_calls\":{},\"callback_ms\":{:.3f},\"frame_ms\":{:.3f},\"phase_hooks_detached\":false,\"api_collection_stopped\":true}}; measurement collection stopped; hooks remain installed and pass through",
                frameDiagnosticsSchema, frameProfilerAbiVersion, update.invalidReason, callback,
                update.invalidCallbackCalls, update.invalidCallbackMs, update.invalidFrameMs);
        } else if (update.summary) {
            logFrameSummary(*update.summary, pendingGameplay, route);
        }
    }
    if (m_captureInvalid) {
        m_pendingFrame.valid = false;
        m_lastFrameEnd = originalEnd;
        m_haveLastFrameEnd = true;
        return;
    }

    // A scene transition closes the prior scene's partial window. The next frame
    // starts a fresh capture so menu/loading frames never contaminate gameplay.
    if (sceneChanged) {
        if (auto summary = m_profiler.flush())
            logFrameSummary(*summary, m_pendingFrame.gameplay, route);
        m_profiler.reset();
        m_playLayer = playLayer;
        m_pendingFrame.valid = false;
        m_lastFrameEnd = originalEnd;
        m_haveLastFrameEnd = true;
        return;
    }

    // Callback scopes are collected from one end-of-drawScene sample to the next.
    // Use that same interval as the frame duration so pre-draw Cocos work cannot
    // be compared against a start-to-start interval that excludes it.
    if (!m_haveLastFrameEnd) {
        m_lastFrameEnd = originalEnd;
        m_haveLastFrameEnd = true;
        return;
    }

    m_pendingFrame.valid = true;
    m_pendingFrame.gameplay = playLayer != nullptr;
    m_pendingFrame.frameMs = ms(originalEnd - m_lastFrameEnd);
    m_pendingFrame.sceneMs = ms(originalEnd - originalStart);
    m_pendingFrame.swapMs = m_frameSwapWallMs;
    m_pendingFrame.systemSwapMs = m_frameSystemSwapWallMs;
    m_pendingFrame.hookMs = 0;
    m_pendingFrame.swapCalls = m_frameSwapCalls;
    m_pendingFrame.systemSwapCalls = m_frameSystemSwapCalls;
    m_pendingFrame.cocosDraws = m_cocosDraws;
    m_pendingFrame.phases = phases;
    m_lastFrameEnd = originalEnd;
}
void RendererManager::shutdown() {
    if (m_stopped) return;
    const auto capability = backendCapabilityState();
    if (capability == api::BackendStateV1::initializationPending ||
        capability == api::BackendStateV1::operational)
        publishBackendCapability(api::BackendStateV1::initializationFailed);
    m_stopped = true;
    if (!m_captureInvalid) {
        if (auto summary = m_profiler.flush()) {
            const std::string_view route = m_backend.find("Zink") != std::string::npos ? "Zink" : "OpenGL";
            logFrameSummary(*summary, m_playLayer != nullptr, route);
        }
    }
    uninstallSwapApiProfiler();
}
}

extern "C" VULKANIZER_CAPABILITY_EXPORT std::int32_t VULKANIZER_CAPABILITY_CALL
Vulkanizer_GetBackendCapabilityV1(vulkanizer::api::BackendCapabilityV1* capability) noexcept {
    if (!capability || capability->structSize < sizeof(vulkanizer::api::BackendCapabilityV1)) return 0;
    capability->abiVersion = VULKANIZER_BACKEND_CAPABILITY_ABI_VERSION;
    capability->state = static_cast<std::uint32_t>(vulkanizer::RendererManager::get().backendCapabilityState());
    capability->reserved = 0;
    return 1;
}
