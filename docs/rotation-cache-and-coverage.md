# Rotation coverage and read-only GPU input caching

The user's 01:33:12 run selected Zink and requested automatic transforms with
the heavy callback profiler off. Its log had no fast-rotation, parity or live
GPU application messages. The runner reported `presentmon_state=no-csv`.
Consequently that capture does not establish any frame-time improvement or
GPU break-even; the strict whole-array decoration gate was preventing the
candidate from being exercised.

The revised CPU gate accepts tested solid/decoration mixtures. It still rejects
trigger/player and group/area-parent-dependent cases. CPU calculations and
state application remain interleaved one object at a time, in native order.
GPU readback keeps the stricter independent-decoration gate. The automatic
mixed fixture explicitly calls the actual automatic entry point and compares
it with native GD; its work is excluded from gameplay activation counters.

`VULKANIZER_TRANSFORM_STATUS` reports accepted batches and object visits,
rejected batches and visits, GPU sample activation and the failure fuse.
`VULKANIZER_FRAME_LIGHT` records start-to-start mean/max frame intervals,
frames above 100 ms, simulation-step count and accepted/rejected rotation
visits in approximately two-second windows. It uses the existing frame hook
and one clock sample per frame; it does not enable per-object profiling.
Windows reset on gameplay/menu transitions. Their timing includes frame caps
and waits; the values are frame intervals, not exclusive CPU time.

The GPU executor now caches exact submitted bytes of the three shader-read-only
buffers. A matching buffer skips `glBufferSubData`; a changed size/content or
storage reallocation forces an upload. A context change clears all cached
inputs. The object-state buffer is always uploaded because compute modifies
it. No computed position, group membership, or GD object lifetime is cached.
The process-wide fence failure fuse prevents reuse after a timeout or failed
fence creation. Metrics expose uploaded bytes and reused input-buffer counts.

Controlled benchmarks compare cache off/on in the same GD process, warming
both variants before timing and resetting starting object transforms. GPU
execution overlaps fence wait and must not be added to that wait as separate
wall time. Benchmark callbacks are recorded by the scratch layer; real
section-container updates and drawing are outside that comparison.

Peaceful's supplied level-wide counts and overlapping memberships motivate
coverage measurement. They are not simultaneous visible-object counts and
do not justify combining operations across intervening CPU dependencies.
The old 78-helper-calls-per-frame capture remains separate from this run.
This change does not defer simulation, collapse trigger actions, or cache
transform results between sequential actions. Real level/frame-time benefit
still requires the next gameplay run's activation and frame-window logs.

## Controlled result

Native state parity and the actual automatic mixed-array gate passed in
`Geode 2026-11-15 01.49.16.log`, session
`dist/sessions/20261115-014915-2e51ef1b`. Both GPU variants were warmed first.
Total wall milliseconds for 32 calls, including candidate eligibility checks:

| Objects/call | Native | CPU fast | GPU cache off | GPU cache on |
|---:|---:|---:|---:|---:|
| 128 | 0.1090 | 0.0594 | 16.1496 | 13.5489 |
| 1,024 | 0.8664 | 0.5207 | 18.6030 | 17.3300 |
| 8,192 | 7.5058 | 5.5209 | 35.0633 | 38.7403 |

Caching reused 96 input buffers in each 32-call group. Upload bytes fell from
507,904 to 98,304; 4,063,232 to 786,432; and 32,505,856 to 6,291,456,
respectively (about 80.65% less). The largest case's upload-call duration fell
from 2.1945 to 1.1098 ms, but its total latency increased. Waiting, readback and
other preparation varied; this single sequential trial is not a statistically
established caching speedup. No hybrid break-even occurred at these sizes.

At 8,192 objects, cached GPU execution was 0.3806 ms, fence wait 11.9313 ms,
and readback 8.6238 ms across the 32 calls. GPU time overlaps the fence wait.
It is not valid to count it a second time in total latency.

Concise selected evidence and machine-readable results are saved as
`build/diagnostic-reports/rotation-cache-evidence.log` and
`rotation-cache-benchmarks.json`. The failed fixture captures with zero
completed candidate batches are invalid performance measurements and are
not included in this table.
