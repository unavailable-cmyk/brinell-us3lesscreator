# Vulkanizer Architecture V2 foundation

## Current milestone

V2 has an independently executable shared-translation prototype in `src/v2`. The controlled executable compiles its own scene, schedules native-resolved translation events, uploads chain parameters and draws persistent textured geometry. Shared objects use this execution path exclusively. The core has no dependencies on Geode, the rotation scheduler, OrderedVisualPlan, the legacy executor or resident-decoration experiment.

The existing game implementation remains the development recovery path. Production GD level extraction, trigger-hook routing and ownership-aware Cocos atlas suppression are pending. Live GD still selects the existing implementation. Promotion to the primary game path requires validating those adapters against native gameplay, sections, dependencies and actual level drawing. Peaceful and the POCO X7 Pro still require full-level measurements.

## Compilation and ownership

`Scene::compile` receives resolved drawing order, baked quad geometry, material keys, memberships, initial visibility and dependency declarations. Compilation assigns immutable scene-local object slots, a unique scene generation and canonical translation-chain indices. Duplicate memberships collapse. Sorted membership sets establish chain identity. Native action sequence numbers establish execution order.

The scene owns geometry, dependency paths, group-to-chain reverse mappings and native member routes. Material keys identify the adapter's complete native texture, blend, shader and clipping state. The GL shader supports texture multiplied by vertex color. Objects requiring other shader behavior must receive the authoritative-CPU classification from the adapter.

Path A owns decorations declared compatible with shared translation. Each membership combination has one accumulated double-precision translation and one 16-byte GPU palette entry. Objects retain individual baked geometry and base positions.

Path B has explicit `independentGpu` execution and `independentTransform` dependency classifications. Its persistent evaluator is a later milestone. Independent-transform candidates currently receive native ownership during compilation.

Path C uses `NativeSink` for native movement, materialization, visibility and reset, and `RenderSink` for drawing at the original ordered position. The host retains authoritative gameplay state. Mixed groups contain disjoint shared-chain and native routes. A native callback receives only native-owned IDs for that operation.

The controlled compiler accepts explicit dependency declarations. A Geode adapter must determine these from gameplay consumers before compiling a real level, including pivots, targets, collision, sections, follow/lock behavior, visibility effects and other mods' CPU reads.

## Runtime and invalidation

`translate(group, delta, order, nativeSink)` resolves the precompiled reverse mapping and evaluates each affected chain once in native event order. It coalesces dirty chains until rendering. Shared-only submissions inspect chain indices and translations. Native members are visited only by the fallback callback. State persists without action histories or object snapshots.

`updates()` and `acknowledgeUpdates()` form a synchronous single-consumer contract on the owning simulation/render thread. Updates remain dirty until successful palette upload. A replacement renderer can attach from the current retained palette. The GL adapter rejects scenes with a generation different from its attached scene.

`acquireNative` materializes current position and visibility before an authoritative CPU read, then transfers ownership and updates routes. `setGroups` performs the same boundary before membership mutation. Other consumers retain their shared chains. Mapping allocations finish before materialization, so allocation failure preserves prior ownership. Native callbacks complete synchronously without exceptions or scene re-entry.

`retire` completes handoff and removes routes before the adapter deletes an object. Its slot remains reserved and its handle becomes invalid. Reset preserves retirements and dependency promotions, restores initial visibility and positions, clears translations and restarts native action numbering. Recompilation creates a fresh generation. New dynamic objects can remain native until recompilation.

Visibility changes invalidate the cached ordered draw list. Rebuilding visits scene slots on that event. Compilation, reset, handoff, membership mutation and retirement are explicit non-routine work. Unchanged frames reuse the draw list. Routine translation and rendering avoid scene-object traversal. Draw submission scales with the ordered material, palette-page and native boundaries required by the scene.

## Rendering and portability

The GL 3.3 adapter owns a static vertex buffer, VAO, palette UBO and texture-color shader. It uploads geometry on attachment and updates dirty palette entries thereafter. The vertex shader consumes translations directly. A CPU shared evaluator is sufficient for this milestone, so compute dispatch count is zero.

The palette uses std140 vec4 entries, 256 entries per 4096-byte page. Additional chains use aligned pages and ordered draw ranges. A GPU reference test covers 1000 chains across four pages. Ranges merge only adjacent compatible geometry. Native ranges and material changes retain their original positions.

The mobile alternative uses GLES 3.0 UBOs and GLSL ES 300, with highp vertex arithmetic and the same vec4 layout. It requires adapted version/precision declarations and a platform GL resolver. Compute shaders, SSBOs, shader doubles and indirect drawing are unnecessary for Path A. Android implementation and POCO measurements remain pending.

The adapter owns GL state while rendering. A Cocos adapter must restore or invalidate host state caches before returning. Destroy the renderer before its GL context. Context replacement requires a fresh renderer and attachment. Atlas mutation, custom materials and unsupported visual changes require handoff or recompilation before use.

## Numerical scope

Chain offsets accumulate in double precision in action order. The draw palette and baked geometry use floats. Reassociating a base position and accumulated translation can differ from native per-object double additions. Binary-fraction fixtures compare exactly against per-object native-order additions and CPU-materialized reference geometry. The fractional-action test allows less than 1e-8 world-unit position error over 4096 actions at the tested coordinates. A 100000-action test verifies persistence and handoff.

General numerical eligibility for long-running real levels, very large coordinates and cancellation-heavy movement remains part of production adapter validation. CPU-required gameplay objects retain native arithmetic and authority.

## Reproduction and measurements

Use the configured native test build or configure `tests` with the project's toolchain and Vulkan header dependency. Build the three targets in Release.

```powershell
cmake --build build-native --config Release --target v2-scene-test v2-chain-benchmark v2-gl-benchmark --parallel 6
ctest --test-dir build-native -C Release -R '^v2-' --output-on-failure
./scripts/benchmark-v2.ps1
```

The script accepts `-BuildDirectory`, `-Configuration`, `-OpenGLProvider` and `-OutputDirectory`. It defaults to the system GL driver. An explicit Zink DLL supports an additional provider run. The GL benchmark is an explicit executable because it requires a working hardware context. Regular CTest includes the portable scene checks and CPU scaling benchmark.

The GPU benchmark covers 1, 1000, 10000 and 100000 decorations, each with single and overlapping memberships. It measures 240 frames after 16 warmup frames at 512 by 320 pixels using textured one-pixel quads. Separate fixtures cover overlapping alpha/additive materials, dynamic visibility, reset, mixed native/shared drawing, stale handles and paging. CPU-only results also sweep active chain count with 100000 objects held fixed.

CPU submission includes scheduling, palette uploads and drawing. GL elapsed-time queries surround palette update and draw commands. Query results and framebuffer readbacks are collected after the timed batch. Clears occur outside timing. These measurements exclude native simulation, Cocos traversal, presentation, Peaceful's materials and full-screen fill cost.

Routine upload bytes include palette entries and matrix/sampler uniforms. Initial bytes cover owned geometry and palette buffers. The fixture also uploads a 16-byte texture once. Native material callback uploads belong to the host. Reported draw counts count actual V2 DrawArrays calls. Scene counters record chain evaluations and event-driven draw-list visits.

The completed Release run used the Ryzen 5 5600XT, RX 7600 and system GL driver. The overlapping-membership cases produced the following medians. Each frame submitted one draw and zero compute dispatches.

| Decorations | Chains | CPU submission us | GPU ms | Palette bytes/frame | Total state bytes/frame |
|---|---|---|---|---|---|
| 1 | 1 | 0.5 | 0.00356 | 16 | 84 |
| 1000 | 3 | 0.5 | 0.00640 | 48 | 116 |
| 10000 | 3 | 0.5 | 0.01532 | 48 | 116 |
| 100000 | 3 | 0.7 | 0.09208 | 48 | 116 |

At fixed 100000-object count, the CPU-only sweep measured approximately 0.0204, 1.304 and 13.450 us per submission for 1, 100 and 1000 active chains. Chain evaluations across 240 frames were exactly 240, 24000 and 240000. The scale fixtures recorded zero routine object visits, position differences and mismatched image channels. Clock resolution and the synthetic workload limit interpretation of the small CPU timings.

The review includes `v2-cpu-benchmark.json`, `v2-gl-benchmark.json`, `v2-benchmark-run.json` and `v2-validation.json`. Run metadata identifies the processor, renderer, configuration, executable hashes and measured source hashes. Windows Application Control blocked the final CPU/GPU benchmark executables after the measured run. Final scene correctness passes, including retirement and membership mutation. The manifest lists every source difference since the measured snapshot, and the validation report records the final verification status. Previous Peaceful recovery-path summaries are retained.
