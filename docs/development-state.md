# Vulkanizer development state
## Architecture V2 foundation, 2026-11-17

The independent shared-chain scene compiler, CPU chain scheduler, persistent GL 3.3 renderer and controlled scale tests are implemented in `src/v2`. Production GD extraction, hook routing and Cocos ownership integration remain pending. The controlled prototype uses its own shared execution path. Current architecture, benchmark scope and reproduction are recorded in [VulkV2](vulk-v2.md).

The sections below retain the earlier recovery-path workflow and historical benchmark results.

Canonical build: `build`, optimized `RelWithDebInfo` (/O2, release runtime plus symbols), pinned Geode 6.0.0-alpha.1. Existing dependencies/configuration are reused. Primary package: `build/us3lesscreator.vulkanizer.geode`; distribution: `dist/Vulkanizer-Windows-x64`.

Recovery: original validated `build/diagnostic-reports/before-group575.geode` is unchanged. `recovery/pre-reset-installed.geode` preserves the installed pre-reset experimental package separately. Neither is overwritten by builds.

Archived: three unreferenced build trees are under `archive/2026-11-16-obsolete-builds`: scheduler-breakdown, swap-boundary, trigger-diagnostics. Their reports/dependencies remain intact. No script referenced them. All Mesa/Zink trees, native API probes and trigger PoC builds are retained because scripts or dependencies still use them. No Git operations are part of this workflow.

## Short cycle

From the project root, in PowerShell:

    ./scripts/dev.ps1 Build
    ./scripts/dev.ps1 Install
    ./scripts/dev.ps1 Benchmark -Evaluator GPU -OutputDirectory build/diagnostic-reports/my-gpu
    ./scripts/dev.ps1 Benchmark -Evaluator CPU -OutputDirectory build/diagnostic-reports/my-cpu
    ./scripts/dev.ps1 Compare -RunDirectories @('build/diagnostic-reports/my-gpu','build/diagnostic-reports/my-cpu')
    ./scripts/dev.ps1 Restore

Build uses the configured CMake/MSBuild tree, real file tracking in a new writable tracking directory, no forced rebuild and no explicit reconfiguration. A no-change build/package completed in 2.67 seconds. Configuration changes require explicit maintenance, not automatic SDK substitution. Compilation failures stop packaging. Install/restore refuse while GD is running. Restore selects the original validated package; it does not silently promote an experiment.

Launch: `./scripts/dev.ps1 Launch -Evaluator CPU` (or GPU). Uses the existing controlled Zink launcher and existing persisted mod settings. Benchmark temporarily selects automatic batching, culling/precompilation off, and restores settings. Add `-Group575` only for explicit comparison; default comparisons preserve the blanket policy. Add `-Diagnostics` only for cost/visit captures, not performance comparisons.

## CPU evaluator

`VULKANIZER_VISUAL_CPU_ONLY=1` selects ordered CPU evaluation inside flushVisualStream. Unset/other values retain GPU evaluation. Selection does not set visual.failed; normal successful checking, continued batching, event replay and state application remain intact. It bypasses the stream executor/upload/fence/readback, not Zink rendering or the separate startup GPU correctness proofs/bootstrap sample. The legacy gpu_batches/state-write field names count stream batches/applications in CPU mode too; the explicit evaluator marker distinguishes the backend. They are not proof of GPU dispatch.

Benchmarks retain full native level progression into [80,90) seconds; there is no unverified seek or timing override. Initial benchmark reset-gpu-1 overlapped a build and is excluded. Raw reports remain preserved. Group 575 diagnostic gains are provisional against the original package: a fresh validated-package run reached 33.2 FPS. Do not present nomination reductions alone as performance gains.

## Earlier GPU-resident direction

The earlier direction retained native activation and CPU-required objects while moving decorative transforms into persistent GPU storage. Architecture V2 now establishes the independent shared-chain foundation described above. Production adapters still need atlas lifetime, dependency handoff, section, material and draw-order validation.

## Reset benchmark result (2026-11-16)

Same binary, Group 575 off, batching enabled, culling/precompilation off, [80,90) level seconds. Full native progression was retained; no reliable state-preserving seek was introduced.

| Performance capture | FPS | Median ms | P95 ms | P99 ms | Level seconds / wall second |
|---|---:|---:|---:|---:|---:|
| GPU reset-gpu-repeat | 28.16 | 37.408 | 46.866 | 58.083 | 1.0034 |
| CPU reset-cpu-clean | 27.33 | 39.948 | 50.914 | 62.708 | 1.0041 |

CPU evaluation did not demonstrate a performance win. This small valid comparison is not a statistically definitive architecture ranking. GPU stays default. reset-gpu-clean is invalid: 546 presentation polls shared one simulation time during a >3-second pause. The comparison script now rejects >250ms heavy-window simulation plateaus. reset-gpu-1 overlapped compilation and is also excluded; neither file was deleted.

Both valid runs pass all 13 native-reference scenarios. Of 49 common player checkpoint indices, 38 have exactly matching simulation times and matching logged core player state; 11 are time-unmatched and cannot establish state parity. The unverified speed_ac SDK field varies across native controls and is excluded explicitly. These wall-paced runs are not identical-delta replay; whole-level bitwise state/visual parity is not claimed.

Separate diagnostic runs, ten-second checkpoint interval 80.066256866–90.066256866:

| Cumulative interval cost/work | GPU | CPU |
|---|---:|---:|
| CPU tape evaluation ms | 0 | 1668.201 |
| GPU timestamp execution ms | 106.885 | 0 |
| Fence wall ms | 559.823 | 0 |
| Readback ms | 409.431 | 0 |
| Packing ms | 1105.238 | 1040.488 |
| State application ms | 1712.188 | 1482.169 |
| Native CPU movement member visits | 38,193,388 | 37,726,959 |
| Native CPU rotation member visits | 11,732,405 | 11,732,405 |
| Actual final-state applications | 13,708,015 | 12,574,857 |
| Intermediate section events | 990,504 | 989,767 |
| Replay member visits | 169,123 | 194,750 |

GPU execution overlaps fence waiting; do not sum those as independent latency. CPU application/packing are still substantial on both architectures. Member visits are physical helper nominations/iterations, not unique objects or exact position-store counts. State applications explicitly count commits to authoritative GD objects. Capture counts vary with native render-frame partitioning. No native vertex-rebuilding cost has been independently measured in this reset.

Reports: build/diagnostic-reports/reset-performance.json and reset-costs.json. Compare via scripts/compare-stream.ps1 (also dev.ps1 Compare). Diagnostic frame times are not substituted for performance captures. Startup GPU proofs/sample may still execute in CPU-stream mode; only the ordered visual stream bypasses GPU compute transfers. The cost counters confirm zero stream fence/readback in CPU mode.

Accepted: reversible evaluator toggle, native-reference coverage, real incremental tracking, compact comparison, pause rejection, preserved recovery/archive. Rejected: treating operation reductions or the anomalously high paused FPS as speedups; CPU-only acceleration claim. Next: a restricted GPU-resident draw integration design that retains native activation/CPU anchors and removes routine packing/readback/commit/vertex work for GPU-owned decorations. Establish atlas lifetime/indirection and draw-order correctness before implementation; no replacement renderer was added here.

## Review preparation

OrderedVisualPlan now isolates transport-independent action traversal used by CPU evaluation, parity reference and fallback replay. Native scheduling/eligibility and legacy GPU transport/CPU commits remain unchanged. See docs/legacy-pipeline-boundary.md. The source-review archive is generated by scripts/export-review.ps1; it excludes dependencies/binaries and includes current compact benchmark summaries. This is preparation, not a GPU-resident implementation or a new performance claim.

### Preparation verification

Incremental build passed after the plan extraction. Two seven-second Cache Test runs (GPU and CPU stream modes) each passed all 13 native-reference scenarios and live gameplay initialization. No new Peaceful benchmarks or screenshots were used. The previously installed package was restored afterward and matched recovery/pre-review-isolation.geode by SHA-256. Original validated recovery files are unchanged. Snapshot verification checks every source hash and rejects binaries/build/archive/log paths; run tests/review-export-test.ps1 after export.
