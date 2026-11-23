# GD rendering through Vulkan: approach and acceptance gate

## Investigation result

| Approach | Windows feasibility | Decision |
|---|---|---|
| Native Cocos translation | Verified v6 hooks exist for `CCSprite::draw`, `CCSpriteBatchNode::draw`, both `CCTextureAtlas::drawNumberOfQuads` overloads, `CCDirector::drawScene`, `CCEGLView::initGlew/setupWindow/swapBuffers`. Sprite hooks still miss labels, clipping, render textures, custom GL and other mods. A correct implementation needs resource ownership, shader/state translation, ordering and original-window presentation. | Longer-term renderer design. Not a credible complete first integration from a few hooks. |
| Mesa Zink/WGL | Mesa ships Windows `opengl32` and `gallium_wgl` targets. `targets/wgl/wgl.c` selects `GALLIUM_DRIVER=zink` and calls `zink_create_screen`. `frontends/wgl/stw_device.c` recognizes Zink; `zink_kopper.c` creates Win32 Vulkan surfaces and presents swapchains. | Selected initial complete-command-stream route. Must activate before process/context creation. |
| Copy an OpenGL framebuffer into Vulkan / independent overlay | Can present GD-looking pixels, but the original drawing still happened through the native OpenGL backend. Extra synchronization/copying also obscures the objective. | Rejected as proof of GD's commands reaching Vulkan. |
| Zink through a global ICD/registry change | Unnecessary machine-wide scope for a mod experiment. | Not implemented. |

Import inspection of the exact installed GD/Cocos/GLEW binaries found `OPENGL32.dll`, GLEW dispatch variables, `wglGetProcAddress`, and GDI pixel-format/presentation functions. The game itself also imports `glDrawArrays`. These paths make an early WGL provider a broader interception boundary than a sprite subclass. The module's Geode hooks observe the original game context; they do not replace scene updates or gameplay timing.

The upstream `targets/libgl-gdi/stw_wgl.c` explicitly implements the WGL API as an `opengl32.dll` drop-in atop Mesa's ICD DDI. This supports the app-local-provider design. Actual success with GD's loader, context hints and graphics environment still needs runtime testing; source feasibility is not a successful game test.

## Loading and fallback

Windows normally searches an unpackaged application's folder before System32, subject to KnownDLLs, explicit loader flags and other documented rules. An environment variable set after WGL/context initialization cannot replace already loaded imports or migrate textures. `run-gd.ps1` therefore owns a restart-only session with an app-local, checksum-verified provider. The state journal records exactly the two staged filenames/hashes and the mod's original two settings. Recovery refuses unknown filenames, changed DLLs or a mismatched game/settings path. Existing providers are never overwritten.

The launcher changes only its child's environment: `GALLIUM_DRIVER=zink`, evidence/log paths and Mesa log level. It changes no registry, global DLL, security policy or executable. After the process exits, the managed local DLLs are removed and experimental flags restored. The OpenGL mode stages no provider and rejects leftover/custom local providers so it cannot silently call a translated session a native fallback. Interrupted sessions require explicit recovery while GD is closed. Failure to initialize Zink is handled by ending that session and using OpenGL mode; there is no unsafe in-process driver hot swap.

Disabling the Geode mod does not unload an already active WGL provider. Close the test session and recover its files before relying on normal launches. This is the lifetime boundary of the native compatibility component, separate from Geode's hook lifetime.

## Objective evidence

1. The Geode `drawScene` callback logs `GD_RENDER_ROUTE` from the current **game-thread WGL context**, including PID, `WindowFromDC(wglGetCurrentDC())`, renderer and provider path. The runtime preview has been removed, so it cannot contaminate this route.
2. `instrument-mesa.py` checks the pristine SHA-256 of three Mesa 26.2.6 files before changing them. It instruments 16 actual `VKCTX(CmdDraw*)` dispatch sites, preserving expression/control flow, and the existing Win32 surface creation / queue presentation calls in Kopper. The marker records original/patched hashes and refuses later unexpected edits.
3. Optional telemetry counts Vulkan draw API dispatches and writes JSONL after surface creation and queue presentation. Events include PID, HWND, return codes and cumulative presentation errors. Logging happens at the first present, every 120 presents, and the first eight failures; no GPU waits or readbacks are added. Atomic counting and development settings mean this build is not a performance reference.
4. `verify-gd-vulkan.py` requires a unique Zink game-context attestation and matching successful Vulkan surface creation plus draws and sustained presents for **the same PID/HWND**. It rejects a renderer string alone, a preview source, another window/process, missing draws/presents and recorded presentation errors.
5. The human then confirms that menus and representative gameplay content are visually correct. This remains separate from automated API-route verification.

Counters are inserted immediately before the real Vulkan draw dispatch expression; successful later presents demonstrate a surviving submission/presentation path. They do not count visible objects, assert every pixel is correct, or provide GPU execution time. Presentation success alone also cannot verify all shader/blend/clip semantics. No current result claims the GD-to-Vulkan acceptance gate passed.

## Source and toolchain

- Mesa source: official `mesa-26.2.6.tar.xz`, SHA-256 `b9436b3e1a8be76fbd4823dc884f90596ab7c6ac5e1867a305e66ffccfb31a92`.
- Windows-only Zink/WGL, hardware Vulkan from the existing GPU driver. No llvmpipe, lavapipe, bundled Vulkan ICD or fallback to a software backend.
- Source-built WinFlexBison v2.5.25 (`1176da2adaf972b2216b1ac8dd611e128fe80bc5`); CMake 4 needs `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for this upstream build.
- Python 3.14.3 and Meson 1.12.1 through `python -m mesonbuild.mesonmain`. PyYAML 6.0.3 is installed in a project-local module directory. The successful build used MSVC 19.50 and Ninja 1.13.2.
- `build-zink.ps1` records all Meson options. Shader disk caching/compression are disabled in this first minimal recipe; no speedup claim is justified.
- The default evidence bundle counts Vulkan draw dispatches and presents; for clean performance captures, `build-zink.ps1 -BuildOnly -Uninstrumented` builds the same pinned Mesa release without those counters into `dist/zink-clean`.
- `run-gd.ps1 -ZinkDescriptors auto|lazy|db` selects Mesa Zink's documented descriptor manager. Use `auto` as the baseline and change one value per controlled run.
- Frame logs report entry-to-entry `CCDirector::drawScene` cadence, original `drawScene` wall time, nested `swapBuffers` wall time, and wrapper overhead. GPU timestamps are not collected.
- `run-gd.ps1 -Backend Zink -CaptureCpuProfile` starts an elevated WPR CPU verbose profile with sampled stacks, scopes the output to the session folder, stops it when GD exits, and runs `xperf -a profile -detail` to summarize samples by process/module. The ETL is system-wide, so use only a short representative segment.

## Current verification and blockers

- Exact-version Geode integration **compiled and packaged** with `geode build`.
- Earlier bootstrap **loaded in-game** on Loader 6.0.0-alpha.1 / GD 2.2091; earlier render hooks executed during Stereo Madness. Those results predate this translation integration and do not prove Zink.
- Instrumented Mesa **configured, compiled and linked successfully** into `dist/zink/opengl32.dll` and `gallium_wgl.dll`. Both are AMD64 PE DLLs. Required GL/WGL exports and runtime dependencies were inspected.
- Windows blocked the native compiler sanity executable. The successful build used `integration/mesa/windows-x64-build-only.ini` with Meson's documented `needs_exe_wrapper=true` cross-build property. Compile/link checks completed and target execution checks were deferred. Windows policy stayed active.
- Evidence C code compiled independently. Ten synthetic evidence-correlation tests and session safety tests passed. WGL API fixtures have a distinct result status, and the GD validator rejects their log prefix.
- The hidden WGL checker compiled. Its first runtime attempt reached `LoadLibraryExW`, which failed with **4551**. Code Integrity event 3033 identifies `dist/zink/gallium_wgl.dll` and event 3077 identifies policy `VerifiedAndReputableDesktop`. The process exited before creating a GL context or issuing Vulkan draws.
- The prior mod binary also encountered error 4551. Runtime tests require the generated artifacts to be accepted through the machine's approved development trust workflow.
- GD Vulkan rendering and visual correctness await the manual session. Zink DLL deployment to GD remains pending.

## Automated WGL API check

`tests/wgl-zink-api.cpp` loads the built provider into its own process and creates a hidden 64x64 WGL window. It uses the standard GDI pixel-format and swap functions imported by GD, checks 14 Cocos/GLEW extension entry points, uploads a texture, draws 120 GL frames, compares two pixel readbacks and checks cleanup return values. `check-zink-api.ps1` runs it in a unique evidence folder and restores its process environment afterward.

The result `WGL_ZINK_API_CHECK_PASSED` requires matching Vulkan surface, draw and presentation records for that process/window. The separate `GD_GL_TO_VULKAN_CONFIRMED` result requires the Geode game-context attestation. The 2026-11-13 session `dist/sessions/20261113-195803-cc46563a` verified actual GD rendering on the original HWND with 3,184,558 Vulkan draw dispatches and 77,400 successful presents; no presentation errors were recorded. This does not imply the standalone WGL probe passed.

## Human test after the build becomes available

For trigger-phase attribution, enable **Frame diagnostics** in the Geode mod settings and choose Zink from the Windows launcher. The launcher's **Capture CPU profile for troubleshooting** and **Capture frame-time CSV for A/B benchmarks** options are separate measurements and are not needed for this run; leave them off to keep profiler overhead low. Play the same high-load section briefly, then exit GD normally so the session files are finalized. Phase hooks measure `CCScheduler::update`, its nested `CCActionManager::update`, outer `CCNode::visit` calls, and sprite/batch/atlas draws. Trigger diagnostics also time `processMoveActionsStep`, `moveObjects`, and `rotateObjects`, count `rotateObject` calls, and record the sizes of bulk object arrays. These hooks only measure or count calls; they never skip or reorder callbacks. Visual results remain human-reported.

## Primary references

- [Zink design and capability requirements](https://docs.mesa3d.org/drivers/zink.html)
- [Mesa Windows build instructions](https://docs.mesa3d.org/meson.html#windows)
- [Official 26.2.6 release/checksum and WGL fixes](https://docs.mesa3d.org/relnotes/26.2.6.html)
- [Microsoft DLL search order](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order)
- [Geode exact SDK release](https://github.com/geode-sdk/geode/releases/tag/v6.0.0-alpha.1)

The decisive implementation details above were checked directly in the checksum-verified Mesa source and installed/generated SDK headers, not inferred from third-party binaries.
