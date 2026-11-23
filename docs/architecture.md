# Historical rendering investigation and preview prototype

This document records the earlier bootstrap/preview implementation. The current mod no longer starts a preview. See [GD Vulkan integration](gd-vulkan-integration.md) for the selected Zink/WGL route, current verification status and acceptance gate. The earlier preview results do not establish that GD rendered through Vulkan.

## Verified interception points

Evidence comes from Geode **6.0.0-alpha.1** source `101c923`, generated 2.2091 bindings at `8228e2b104e4e26c183ad7e5ef7442967c316077`, and Cocos2d headers shipped with that SDK. No guessed game offsets are used.

| Interface | Evidence / purpose | Prototype use |
|---|---|---|
| `void cocos2d::CCDirector::drawScene()` | SDK `cocos/CCDirector.h`; `Cocos2d.bro` marks class `link(win, android)`; generated `modify/CCDirector.hpp` uses `addresser::getNonVirtual` | Calls original exactly once, samples CPU wall time, first-frame GL inspection |
| `virtual void cocos2d::CCEGLView::swapBuffers()` | SDK `cocos/platform/win32/CCEGLView.h`; generated `modify/CCEGLView.hpp` uses `addresser::getVirtual` | Reads Cocos' draw counter before the original presentation call |
| `void cocos2d::CCDirector::purgeDirector()` | SDK and generated modify header | Requests preview cancellation and joins its thread before normal director teardown |
| `CCSprite::draw()`, `CCSpriteBatchNode::draw()`, `CCTextureAtlas::drawNumberOfQuads(...)` | SDK sprite/atlas declarations and generated Windows binding methods | Investigated, not intercepted: a per-sprite hook would add overhead and still miss other GL submission paths |
| `g_uNumberOfDraws` | SDK `include/ccMacros.h`, `CC_INCREMENT_GL_DRAWS` | Raw snapshot only; runtime testing found 1 at swap even in gameplay, so it is not interpreted as a frame draw-call count |

Export inspection of the older configured installation's Cocos DLL additionally confirmed the decorated `drawScene`, `swapBuffers` and global counter symbols. Exact-target in-game validation is recorded separately in verification.md. Geode owns hook registration/removal and addresses; there are no custom machine-code patches or offsets.

`drawScene` is broader than rendering: its CPU time can include scheduler/game updates and presentation waits. The mod never invokes an update independently, changes delta time, skips a draw, or replaces scene traversal. `swapBuffers` remains untouched apart from reading the draw counter. Menus and gameplay statistics windows are separated when the observed `PlayLayer` changes.

Sprites expose texture, quad, blend, shader, batch and transform information. `CCSpriteBatchNode`/`CCTextureAtlas` aggregate quads; `CCTexture2D` and `CCGLProgram` own OpenGL resources. The SDK's `ccGLStateCache.h` exposes cached program, texture, blend and vertex-attribute state operations. These interfaces show why replacing only `drawScene` cannot transparently translate the entire scene into Vulkan: labels, clipping, render textures, custom draws and other mods also use OpenGL resources/state. This conclusion is an architectural inference from the exposed interfaces, not a completed backend conversion.

## Chosen proof of concept

`RendererManager` is the Geode boundary. `VulkanBackend` has no Geode or OpenGL dependency and is shared by the mod and native smoke test. No empty generic backend interfaces are introduced before there is a second real game backend.

Startup probes the System32 Vulkan loader and enumerates physical-device API level, device-local heaps, 2D limits and indirect-draw support. Experimental rendering starts only after an original game frame. A `std::jthread` creates its own fixed-size Win32 preview window, instance, surface, graphics/present queue, device and swapchain, followed by image views, render pass, shader modules, graphics pipeline, framebuffers, synchronization objects and prerecorded command buffers. Each frame submits one `vkCmdDraw(3, 1, 0, 0)` and presents the colored triangle.

The preview uses its own surface; it never attaches Vulkan to GD's WGL window. Its message pump, driver waits and presentation run off the game thread. It polls a fence and acquires with timeout zero, skipping busy frames. Present semaphores belong to swapchain images to avoid reusing a semaphore while a presentation may still own it. Device idle is used only during worker teardown. Initialization failures and surface-out-of-date conditions clean up and stop the preview; this milestone deliberately requires restart instead of implementing swapchain recreation. Closing/minimizing the preview is handled without closing GD. There are no per-frame command-buffer allocations or synchronous GPU readbacks.

This is real Vulkan presentation in the mod process, **not game rendering through Vulkan**. Missing integration includes a render-command abstraction, resource uploads, camera/projection support, ordered sprite batches, clipping/blending equivalence, and a valid game-window presentation or interop strategy. The separate backend can later accept meshes and camera state without depending on gameplay triggers. None of the proposed scene-package/editor/addon ecosystem is implemented.

## Measurements

- Steady-clock entry-to-entry `CCDirector::drawScene` cadence: mean milliseconds and inverse-mean Hz, nearest-rank p50/p95/p99, fixed windows of 1024 samples. Separately measures original `drawScene` wall time, nested `CCEGLView::swapBuffers` wall time, and wrapper overhead. These values are not GPU execution time or monitor refresh measurements. No per-frame heap allocations in the profiler.
- Original `drawScene` CPU wall duration is explicitly inclusive of scheduler/presentation and other chained hooks. It is not isolated GPU or pure render time.
- Sampled wrapper CPU work excludes the original `drawScene`, includes summary logging, and is carried into the next sample. Clock calls, the final bookkeeping setter and initial singleton lookup prevent this being a perfect total hook-cost measurement. GPU work and the preview worker are outside this value; a Cocos-counter assignment runs inside the original chain.
- The raw Cocos counter is sampled before swapping. It remained 1 in the tested gameplay, indicating that this interception does not capture full-frame bookkeeping (a reset before presentation is a likely explanation, not yet proven). Frame draw-call counts are explicitly unavailable. Direct/custom/indirect GL calls can also bypass Cocos bookkeeping; with multiple swaps the last snapshot wins.
- Vulkan initialization time is measured on the preview thread. GPU timings are explicitly unavailable; no `glFinish`, Vulkan GPU readbacks or GPU timestamp claims are made.

## Normal-mode diagnostic fast path

When `frame-diagnostics` is off, `RendererManager` disables its `*Phase` Geode hooks during mod initialization. In particular, the high-frequency `CCNode::visit` diagnostic detour no longer runs once per node in normal play. With diagnostics enabled, hooks report the number of base `CCNode::visit` calls beneath the two top-level roots and attribute trigger work down to movement steps and bulk move/rotate helpers. Per-object rotation helper calls are counted without timing each call. This changes only Vulkanizer's measurement hooks; it does not skip or reorder Cocos traversal, scheduler callbacks, triggers, or draw operations. The setting is restart-required so the hook chain has one stable state for the process.

Use the same level, resolution, driver, VSync/FPS cap and mod set for later A/B tests. Keep diagnostics settings identical, disable the preview for performance testing, and capture several gameplay windows after loading. No Peaceful benchmark or speedup is established by this prototype.

## Zink and Bismuth

[Mesa's Zink documentation](https://docs.mesa3d.org/drivers/zink.html) describes an OpenGL Gallium driver emitting Vulkan commands. It is a useful independent experiment because existing GL code could remain intact. Its driver integration/capability requirements do not make it a drop-in Geode hook. This project installs no OpenGL DLL replacement and does not bundle or activate Zink. Whether it improves GD frame times remains unmeasured.

[Bismuth](https://github.com/iliasHDZ/Bismuth), inspected at `bcb316048f5e1103a7d86cf515e7a8250f4626f4`, replaces rendering organization with its own batching, compute dispatch and indirect GL draws (`src/renderer/ObjectBatch.cpp`). `src/renderer/DifferenceMode.cpp` hooks `CCDirector::drawScene` and can render/present multiple paths, while `hooks.cpp` also hooks the display-link loop and game-layer interfaces. Those paths can change both timing interpretation and Cocos-counter coverage. Its inspected manifest targets Geode 5.10.1 / GD 2.2081, so that source snapshot is not a verified v6-compatible binary. Vulkanizer logs a warning if `iliashdz.bismuth` is loaded, retains the original hook chain and makes no coexistence claim. No Bismuth implementation code is copied.

## References

- [Official v6.0.0-alpha.1 release](https://github.com/geode-sdk/geode/releases/tag/v6.0.0-alpha.1)
- [v6 migration guide source](https://github.com/geode-sdk/docs/blob/main/tutorials/migrate-v6.md), checked against the actual alpha headers
- [Pinned bindings](https://github.com/geode-sdk/bindings/tree/8228e2b104e4e26c183ad7e5ef7442967c316077/bindings/2.2091)
- [Khronos WSI guide](https://docs.vulkan.org/guide/latest/wsi.html) and [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.4.341)
