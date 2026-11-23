# Experimental level precompilation

The restart-only `experimental-level-precompile` setting defaults to false and
requires automatic transform optimization and a ready Zink context. At the end
of PlayLayer initialization it audits trigger dependencies and prepares group
snapshots, object classifications and shared membership combinations for move
and rotation target IDs. Existing per-array pointer comparisons, membership
invalidation, pivot/target barriers, GPU parity checks and CPU fallbacks remain.

This prepares data structures only. Trigger activations, touch input, spawn
chains, easing progress, action ordering, pivot values, callbacks and all
gameplay decisions remain runtime operations. It does not predict or schedule
touch triggers. Unsupported trigger families and remapping retain the existing
fallback. Start-position loading and unavailable contexts skip preparation.

Peaceful 2, 2026-11-16: 667 target IDs, 1,039 cached arrays, 55,881 eligible
objects, 385 combinations and 128 ms initialization in the optimized run.
Its native comparison matched 56 sampled player checkpoints, including 30 in
the heavy section. Native scheduler fixtures passed. This is not a proof for
every level, input sequence or visual state, especially touch-dependent cases.

In one 45-second pair on the same build, level-time 80–90 averaged 28.79 FPS
(p95 47.60 ms, p99 63.55 ms) with precompilation and 29.87 FPS (p95 43.92 ms,
p99 47.92 ms) without. These are single captures and don't establish a reliable
regression, but show no measured performance improvement. Precompilation built
1,086 cached arrays versus 191 in the control, suggesting substantial unused
work and memory. Keep it opt-in and investigate selecting only required arrays
before considering a manual rollout. Previous package:
`build/diagnostic-reports/before-level-precompile.geode`.
