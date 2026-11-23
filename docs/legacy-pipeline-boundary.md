# Legacy pipeline boundary and resident-renderer preparation

The independent Architecture V2 foundation now lives in `src/v2`. Its controlled executable owns scene compilation, shared translation scheduling and persistent drawing. Production game routing remains at the development recovery implementation. The remainder records the earlier OrderedVisualPlan extraction. Current interfaces and verification scope are documented in [VulkV2](vulk-v2.md).

The working hybrid path remains the fallback. This preparation does not implement a renderer or change eligibility, trigger timing, arithmetic order, section events or state-commit semantics.

## Reusable producer and semantics

`RotationScheduler.cpp`: auditVisualDependencies / visualObjectRejection classify native CPU consumers; visualGroup caches membership with snapshot invalidation; recordVisualAction captures native-resolved actions and command stamps; buildVisualCombinations deduplicates ordered membership tapes. Native action preparation/claims/activation stay authoritative. evaluateVisualCPU and RotationKernel define the existing reference arithmetic. TransformCulling, AdaptiveCulling and StableTransitionSort retain their existing policies and event order.

`OrderedVisualPlan.hpp` is a borrowed, GD-independent description of ordered actions, per-object spans, action-index indirection and scene-local object handles. Its traversal is shared by the CPU evaluator, parity reference and error replay. It requires neither CPU transform snapshots nor a transport. It is intentionally synchronous: backing-vector mutation invalidates it; handles require the scheduler scene/object lifetime. A resident consumer needs ownership/generation tracking before asynchronous use. Existing native fixtures exercise the extracted traversal, not a separately rewritten interpretation.

## Preserved legacy adapter and consumer

`HybridTransformExecutor` owns the existing GL compute buffers, upload, dispatch, synchronization and full readback. `visualInput` collects CPU authoritative snapshots for that adapter. The later part of flushVisualStream applies native section events/replay, history/rotation/dirty flags and Cocos state. These are legacy host-state responsibilities, not requirements of OrderedVisualPlan. The CPU-only environment toggle is still opt-in and GPU remains default. Neither working path was removed.

The scheduler and legacy collection still share one flush function. We deliberately did not move the large stateful apply block or redesign eligibility during this preparation. The new renderer should consume the scheduler plan after chain selection and before visualInput/host packing, using persistent GPU-owned object state. Merely invoking flushVisualStream with a new executor would retain the old CPU-GPU-CPU cycle and is not the intended integration.

## Next consumer contract (not implemented)

A resident implementation needs a scene-generation object registry, stable atlas/vertex indirection, explicit lifetime/invalidation on membership changes and atlas reallocation, native-order action submission, and drawing that reads GPU transforms without native uploads overwriting them. CPU-required pivots/collision/gameplay objects remain outside GPU ownership; dynamic promotion requires a bounded authoritative handoff before the CPU read. Section/dependency events need independent handling. This contract does not assume every decoration is CPU-independent or permit stale gameplay state.

The current ordered-plan action ABI is the existing native-tested POD format; it is not promised as a persistent cache ABI. Cocos-bound eligibility and native semantics remain in the scheduler rather than being advertised as a portable trigger compiler. No new hooks, backend replacement, persistent state registry or shader draw path were added.

## Snapshot

Run `./scripts/export-review.ps1` to produce `Vulkanizer-review.zip`. Paths and SHA-256 hashes refer to archived source bytes. Only allowlisted source/configuration extensions are collected; generated binaries, build trees, dependencies, old logs, .bak and Python caches are omitted. Latest compact benchmark summaries are copied under review/benchmarks, not the build tree. The manifest records source defaults, not undisclosed machine-local runtime settings. The archive is self-contained for source review; building still requires the pinned external SDK/toolchain. Source scripts retain configurable/historic path defaults and may need adaptation on another machine.

Current coupling limit: flushVisualStream still interleaves legacy host collection with span selection and constructs the borrowed view afterward. A resident adapter must emit the same plan metadata before that collection (or split that producer loop); this preparation does not advertise an already-working resident dispatch hook. The contract itself has no host snapshot requirement, so the new consumer need not call the legacy transport or commit block.
