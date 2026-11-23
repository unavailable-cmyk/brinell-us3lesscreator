# Move and rotation trigger GPU-offload assessment

## Finding

The bulk geometric transform is parallelizable across objects. The update stream for one object must remain serial in vanilla order because translations and rotations around different centers do not generally commute. The scheduler, group/target resolution, trigger state transitions, and gameplay side effects cannot be replaced by an unordered GPU kernel.

Two designs are viable at different costs. A hybrid path can compute an existing bulk helper's object transforms on the GPU, wait for one batched readback, then preserve GD's CPU state and callbacks. A GPU-resident path can remove the readback and more CPU work, but it also requires changing Cocos atlas/buffer ownership and validating the draw path. The hybrid path is now the first in-game prototype; the atlas investigation remains available as the complementary GPU-resident option.

The existing capture attributed about 297 ms per pathological Peaceful frame to move and rotation processing. That is a strong target, but it is not 297 ms of pure arithmetic: the local executable shows group/action bookkeeping, double/float conversions, cached-state changes, `rotateObject`, and a layer virtual callback inside the object loops. The portion recoverable by a GPU rewrite must be measured after the CPU/GPU split exists.

## What the trigger path does

The official editor guide describes move actions as group-targeted transitions with duration/easing, player and camera locks, target/direction modes, optional dynamic target tracking, and instant/silent behavior. Rotation actions add center groups, degrees and full turns, aim/follow targets, dynamic tracking, boundary groups, and the lock-object-rotation option. The editor guide also documents ordered trigger activation and channels; a later trigger can override an earlier one if its channel/order is wrong. These options affect the action input stream and cannot be discarded by a transform kernel. See the [Move trigger](https://www.robtopgames.com/files/GDEditor.pdf) and [Rotate trigger](https://www.robtopgames.com/files/GDEditor.pdf) sections of the official editor guide.

Generated Geode 2.2091 bindings identify the callable interfaces but contain no implementation bodies. Inspection of the installed Steam 2.2091 executable at those binding RVAs shows:

- `processMoveActions` (RVA `0x336170`) and `processRotationActions` (`0x334450`) iterate active action records, claim/update group actions, resolve targets/centers, and call bulk object helpers.
- `moveObjects` (`0x3368d0`) iterates its `CCArray`, updates per-object position/cache state, and calls a layer virtual for each item.
- `rotateObjects` (`0x334da0`) iterates its `CCArray`, uses double world positions with float intermediate offsets, updates object transform/cache state, calls `rotateObject`, then calls a layer virtual for each object.
- `processMoveActionsStep` (`0x334220`) also sequences transform, rotation, movement, player/follow, and area action phases. Replacing a phase must preserve that call order and the current step/delta behavior.

This is reverse-engineering evidence from the local 2.2091 binary, not RobTop source. It establishes the broad structure and side effects, but not every branch of easing, spawn/remap, platformer, or dynamic-target behavior. The generated [Geode 2.2091 bindings](https://github.com/geode-sdk/bindings/tree/main/bindings/2.2091) provide signatures and addresses, not source semantics.

## Parallelism and ordering

| Work | Parallel safely? | Constraint |
|---|---|---|
| Trigger activation, channel/order changes, stop/pause/resume, spawn/remap | Generally no | Changes which action is active and what group IDs mean next. Preserve CPU execution order. |
| Easing/time progress and resolving player/camera/target/center inputs | Per action in some cases | Dynamic target/center values may depend on another object's update in the same step. Resolve from CPU-owned anchors or build dependencies first. |
| Move/rotate transforms for different objects | Yes | Objects with no cross-object input may run in parallel. |
| Multiple actions affecting one object | No, within that object | Apply that object's action entries serially in original order. Reordering or summing them changes results, especially when rotation and translation overlap. |
| Objects whose target/center is another transformed object | Conditional | Use a dependency graph and dispatch topological batches with memory barriers. Cycles or order-sensitive feedback require CPU evaluation or a serial fallback. |
| Per-object section/collision/player attachment and other gameplay callbacks | Keep on CPU until classified | The binary calls a layer virtual after each transform; skipping it may stale gameplay structures. |

One dispatch per trigger would preserve order but is a poor design for thousands of active triggers. A better shape is CSR-style object-to-action offsets plus an action table sorted in vanilla order. One GPU invocation owns one object and walks only that object's ordered list. The CPU updates action coefficients/state once per trigger; eligible objects are evaluated in parallel. If a dynamic target object must be updated before its dependents, submit multiple ordered batches with a compute/SSBO barrier between them. Static or CPU-owned anchors remove that dependency without a readback.

## Residency and synchronization

The player, collision/solid/hazard objects, trigger objects, target and center anchors, and any object used by section/culling or attachment callbacks should remain CPU-authoritative. Visual-only decoration can become GPU-authoritative only after it is proven not to affect collision, trigger activation, dynamic target queries, or another gameplay callback.

The no-readback path requires compute and rendering to use the same WGL context that GD already owns through Zink. Keep render-only transforms in an SSBO or a GPU vertex buffer and make the Cocos batch draw consume that buffer, with an OpenGL memory barrier before the draw. Zink documents an OpenGL 4.3 feature level and its compute requirements; the RX 7600 Vulkan device is a plausible target, but the actual GL version/features of GD's context have not yet been recorded. The isolated PoC requests GL 4.3 and verifies the selected renderer is Zink.

The CPU must still send compact action data and anchor values to the GPU. That is not an expensive GPU-to-CPU readback. Reading transformed positions back each frame would force completion/synchronization and likely erase the benefit. The current standalone Vulkan preview has a separate device/surface and must not be used for this path.

The Geode v6 Cocos headers expose a concrete atlas handoff candidate: `GameObject` derives from `CCSpritePlus`, which derives from `CCSprite`; sprites expose their `CCTextureAtlas` and atlas index; and `CCTextureAtlas` exposes its quad array, dirty flag, and vertex VBO. Geode's generated 2.2091 modify binding also allows a non-virtual hook on `CCTextureAtlas::mapBuffers()`. This gives an in-context place to inspect the CPU upload and issue a GPU transform before draw. It is a feasible integration seam, not yet a validated hook order.

The remaining Cocos work is to cache and maintain GameObject-to-atlas-index mappings across insertions/reorders, ensure CPU atlas uploads do not overwrite GPU results, and bypass CPU `CCSprite::updateTransform()` only for eligible render-only objects. The first pilot should leave all other sprites and all gameplay-sensitive objects on the original path. A setting without that live draw-path behavior would be a no-op switch and is not useful.

## Isolated proof of concept

`tests/trigger-gpu-poc.cpp` is a hidden-window WGL compute harness; it does not alter the mod or launch GD. It builds an ordered action tape for 16,384 objects and 32 move/rotate actions, runs the same evaluator on the CPU and in a Zink OpenGL compute shader, and requires bit-identical x/y/rotation outputs. Its data uses exact quarter-turn coefficients and integer coordinates so this focused equivalence check is not obscured by CPU/GPU trigonometric-library differences. One object has a known non-commuting rotate-then-move result `(1, 2)`.

The compute shader receives pre-resolved per-frame action inputs. Thus this PoC demonstrates that an ordered per-object move/rotation evaluator can run in parallel without changing the tested operation order. It does **not** establish exact parity for vanilla easing, dynamic targets, every trigger mode, Cocos side effects, or in-game rendering integration.

The optional Mesa instrumentation now counts Zink's actual `VKCTX(CmdDispatch)` and `VKCTX(CmdDispatchIndirect)` calls. `scripts/check-trigger-gpu-poc.ps1` requires the same probe process/window to have successful Vulkan surface/present events and at least one recorded Vulkan compute dispatch, alongside the exact CPU/GPU comparison.

### Verified result

Run on 2026-11-14 with the locally built, instrumented Mesa 26.2.6 bundle:

- Zink reported OpenGL `4.6 (Core Profile)` on the AMD Radeon RX 7600.
- All `16,384 / 16,384` objects matched the CPU reference bit-for-bit after 32 ordered actions and 333,620 per-object action entries.
- The non-commuting rotate-then-move case produced the expected `(1, 2)` position.
- Same-process, same-window Mesa evidence recorded one `vkCmdDispatch`, one successful `vkQueuePresentKHR`, and zero present errors.
- Evidence is saved in `dist/trigger-gpu-poc/20261114-183938-cb085c73/`.

This is a confirmed Zink-to-Vulkan compute proof, not an in-game result. The probe intentionally issued no draw (`draw_calls=0`) and used its own hidden test context/window; it did not render Geometry Dash content. It verifies that the RX 7600/Zink stack can execute the transform kernel and return exact outputs for this action subset. GD's own GL version/capabilities and a GPU-backed Cocos draw path still need integration work.

## Feasibility and next engineering gate

**Feasible for a tightly gated hybrid pilot; no safe wholesale replacement.** The prototype intercepts the existing bulk move/rotate helpers and accepts only explicitly selected decorative groups that pass per-batch checks. It preserves vanilla scheduling and callbacks and sends every unsupported batch through the original helper. The first manual check must establish that a simple real GD group passes the live CPU/GPU parity check and renders correctly. The existing atlas route remains a separate, more complex way to avoid CPU readback.

## Hybrid prototype: GPU arithmetic, CPU-authoritative state

`src/backend/HybridTransformPipeline.cpp` intercepts the existing bulk `moveObjects` and `rotateObjects` calls. Trigger activation, easing, group resolution, command order, and action timing remain in GD. The first pilot is explicitly selected by a group ID and accepts only unique objects that are single-group `GameObjectType::Decoration` items, with decoration flags set and no trigger, disabled, parent, custom rectangle, or audited center/dynamic-target dependency. An unsupported array falls back as a whole to the original helper.

For each eligible helper call, the mod uploads the live transforms and one resolved operation per object to the ordered evaluator, dispatches on GD's current Zink/OpenGL context, waits on a GPU fence, and performs one `glGetBufferSubData` of the complete transform array. It then applies the results to the original `GameObject`s in array order and retains the vanilla state changes and callbacks: movement counts, dirty flags/positions, `rotateObject`, `updateObjectSection`, and `updateDisabledObjectsLastPos`. A one-time live CPU reference check compares the GPU result against the vanilla arithmetic; those first-batch objects are counted and timed separately rather than reported as CPU work replaced. Any mismatch disables that transform path for the process and calls the original helper for the current batch.

This is a split at the actual bulk helper, not a rewrite of GD's trigger scheduler. It does replace per-object position arithmetic; the per-object callbacks and section/state work remain CPU-side. The rotation helper still applies its rotation side effects through GD's `rotateObject`. Batches are not combined across separate helper calls because GD consumes each helper's results synchronously and trigger order/dependencies must remain visible. Therefore the number of fences/readbacks may be high in trigger-heavy scenes, and the readback can erase the arithmetic savings. This first prototype is a measurement gate, not a claim that CPU frame time has improved.

When the pilot setting is active, periodic `VULKANIZER_GPU_TRANSFORM_HYBRID` logs report successful compute batches and matching readbacks, replaced position-math object counts, automatic reference checks, conservative fallback batches, bytes transferred, CPU packing, driver upload, dispatch submission, GPU kernel time when timer queries are available, fence wait, readback, CPU application/callback time, and end-to-end pipeline time. A missing timer-query duration is reported as zero; wall-clock sync and total time remain available. The repeated per-call synchronization and callback costs must be compared with vanilla before expanding the pilot.

## Hybrid versus GPU-resident

| Architecture | CPU work removed | Correctness and synchronization | Implementation status |
|---|---|---|---|
| Hybrid GPU calculation with batched readback | Per-object x/y transform arithmetic for each eligible bulk helper. CPU group/action scheduling, authoritative state writes, dirty flags, rotation side effects, section updates, and callbacks remain. | Same-frame fence and one readback per helper keep later gameplay reads current. The fence may stall. Unsupported arrays run through vanilla. | First opt-in prototype is built. Actual GD use and frame-time benefit still require the focused in-game check. |
| GPU-resident transforms consumed by Cocos | Can also remove CPU transform writes and some repeated Cocos sprite-transform work. The scheduler and gameplay/collision state still need CPU authority for the safe visual-only scope. | Avoids readback, but needs correct Cocos atlas/buffer mapping, upload/reorder/lifetime handling, and a draw barrier in the same Zink context. Dynamic/callback dependencies remain CPU. | Existing atlas integration research remains in this document. It is not discarded or claimed as implemented. |

Hybrid is the simpler way to preserve vanilla state and callbacks while testing whether transform arithmetic is a meaningful part of the 296.7 ms measurement. GPU-resident integration can eliminate more CPU work, but only if atlas integration is correct and the remaining callback/scheduler cost is not dominant. The hybrid timing breakdown will provide that evidence; neither route should be assumed faster before measurement.

The shared affine shader and standalone Zink harness were expanded to cover the resolved Cocos matrix path. The latest rebuilt harness binary was blocked from execution by Windows App Control after recompilation, so the expanded harness run is pending. The in-game pilot also performs a bit-exact live reference check before applying a result, with automatic vanilla fallback on mismatch.
