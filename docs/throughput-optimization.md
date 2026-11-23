# Throughput changes after gameplay-speed correction

The recovery package is `build/diagnostic-reports/before-throughput-optimization.geode`.
The pinned SDK, native timing, existing launcher, shared GPU tapes and CPU fallback
are retained.

## Removed work

* Forced resolutions (dependency reads, reset, queue limits and parity checkpoints)
  no longer advance predictive visibility bounds. They must resolve regardless of
  visibility, and rebuild exact cohort bounds from the resulting state. Non-forced
  culling retains conservative swept bounds and validates resulting GPU positions.
* Large section-transition arrays use stable integer sorting by action ordinal and
  original group-member ordinal. The scratch vector is reused. Small arrays retain
  comparison sorting. This preserves native action-major section application and
  does not suppress section events or move them past gameplay consumers.
* Disabled read-only GPU-input caching no longer creates CPU cache copies. Enabled
  caching still compares exact bytes and respects buffer reallocation.

Native scheduler proof adds sort comparisons at 31, 4,096 and 16,385 transitions,
including extreme integer keys and scratch reuse. Existing clean collision-cache,
overlap, rotation precision, dependency, zoom and queue-budget tests remain.

## Reproducible comparison

`scripts/benchmark-transform-matrix.ps1` runs native OpenGL, native Zink, automatic
batching, and batching with culling. Each uses the existing local Peaceful scene,
45 seconds of ordinary wall time, matching settings and the same installed binary.
The original mod settings are restored after the matrix. An interrupted run leaves
the original settings in the output folder for recovery, and stops further tests.
Native references bypass the replacement rotation scheduler. A native OpenGL run
does not need to initialize compute just to start the reference scene.

Per-frame samples are buffered during the automatic run and written on completion,
avoiding per-frame disk writes. They contain wall time, level time, frame interval
and cumulative physics steps. `scripts/summarize-transform-matrix.ps1` reports
measured p95/p99, mean FPS, progression and ticks per wall second in level-time
sections 73–80, 80–90 and 90–100. These are sections of the local test copy, not
claims about every part of the original level. Startup and fixture frames do not
enter these windows. No timing inputs are overridden.

FPS and timing data alone do not establish visual correctness. Player checkpoint
comparisons, active GPU evidence, bounds failures and fallbacks must be considered.
Peaceful's replacement-song mismatch is pre-existing and is not counted as a mod
regression. Audio-specific validation requires another suitable level.
