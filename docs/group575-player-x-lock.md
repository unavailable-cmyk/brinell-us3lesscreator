# Group 575: narrower Player-X-lock protection

This is an opt-in GD 2.2091 / Geode 6.0.0-alpha.1 experiment. The setting
`experimental-group575-movement` defaults to false and requires a restart.
It requires Zink and automatic transform optimization, with transformation
culling disabled. It does not remove Player-X-lock protection from other groups.

## State consumption and implementation

Local native disassembly establishes the following sequence:

* `GJEffectManager::prepareMoveActions`, RVA 0x369ae0: the Player-X-lock branch
  at 0x369d42 reads the command flag at +0x73, the effect manager's float player
  X displacement at +0x808, the command's double X multiplier at +0x80, and its
  double accumulated displacement at +0x160. Intermediate preparation adds to
  that accumulator; resolved preparation adds the accumulated/lock displacement
  to the movement delta. This branch does not read target decorations' positions.
* Native `processMoveActions` resolves dynamic movement calculations first.
  Its calls at 0x33645a and 0x3364ef pass resolved static/optimized group deltas
  to `moveObjects` (0x3368d0), including native float narrowing. This preparation,
  activation order, claims and accumulation remain native.
* The GPU tape receives those resolved helper operations in native order. It
  retains each object's ordered double additions and rotation operations; it
  does not combine deltas into a mathematically equivalent but differently
  rounded sum. Command stamps, history, X masks and intermediate section events
  remain part of the existing evaluator.

The exception is granted only when the complete level dependency audit permits
the stream and Group 575's accumulated protection causes equal **64**, exclusively
Player-X-lock. Group 575 is then removed from the blanket protected-group set.
Its members still pass every per-object filter and every other protected group.
Explicit centers/pivots, target-group mappings, dynamic movement references,
gameplay objects, unsupported classes, parents, particles and other excluded
objects remain CPU-authoritative. Newly observed dynamic movement references
force resolution and promotion to CPU processing before their coordinate or
rotation-offset read. Unknown trigger semantics reject the stream.

Eligible rotations share the same ordered tape. Forcing them back to CPU drained
the tape and made the initial narrower exception slower. A pivot-less rotation
now skips that global drain **only when its cached target group contains no GPU
members**. It still executes all original CPU rotation work. Mixed/GPU targets,
unknown groups and real dependencies retain the barrier. Visibility, reset,
exit, topology changes and existing budget/error barriers still resolve pending
work. No command-preparation or physics hooks change timing or player speed.

This uses the existing hybrid evaluator/readback and CPU-authoritative results;
it is not a GPU-resident renderer. Existing section-transition ordering, replay
on event overflow and executor-error fallback remain in place. Group membership
snapshots are checked on reuse. No persistent level data is modified.

## Repeated performance comparison

All runs use the existing saved Peaceful level, automatic Zink transform batching,
culling and precompilation off, transform-cost diagnostics on, and native timing.
The automated run supplies the same no-input gameplay scenario; render-frame
partitioning is **not** replayed identically. Statistics below cover native level
time **[80, 90) seconds**, not the entire level or a claim about every worst case.
Percentiles use the higher order statistic `ceil((n-1)*p)`.

| Interleaved run | Frames | FPS | Median ms | P95 ms | P99 ms |
|---|---:|---:|---:|---:|---:|
| Same binary, option off: control 1 | 277 | 27.77 | 39.443 | 48.311 | 53.484 |
| Option on: optimized 1 | 330 | 32.99 | 30.917 | 36.517 | 43.196 |
| Same binary, option off: control 2 | 288 | 28.88 | 37.031 | 45.237 | 50.040 |
| Option on: optimized 2 | 330 | 33.07 | 30.875 | 37.374 | 47.561 |

The original validated package's earlier matched window reached 28.86 FPS,
median 37.175 ms, P95 45.688 ms, P99 49.395 ms. A fresh repeat is recorded in
`build/diagnostic-reports/group575-validated-repeat` separately.

Counters use physics checkpoints at **80.066256866 and 90.066256866 seconds**:

| Work in that ten-second interval | Control 1 | Optimized 1 | Control 2 | Optimized 2 |
|---|---:|---:|---:|---:|
| Movement fallback nominations | 34,171,623 | 9,585,413 | 34,420,861 | 9,595,877 |
| CPU movement helper member visits | 37,888,715 | 13,468,105 | 38,137,953 | 13,478,569 |
| CPU rotation helper member visits | 11,732,405 | 11,732,405 | 11,732,405 | 11,732,405 |
| Actual CPU applications of GPU final states | 13,024,363 | 14,707,556 | 13,742,459 | 14,714,238 |
| Additional intermediate section events | 990,199 | 1,640,739 | 991,217 | 1,641,417 |
| Replayed helper member visits | 183,246 | 170,169 | 164,158 | 170,298 |

Fallback nominations decline about **72%**, and CPU movement helper visits
about **64.5%**. Those are member visits, not exact counts of individual position
stores: native X masks and dirty/history bookkeeping must not be conflated with
coordinate writes. CPU state application has not disappeared. The explicit
GPU-result application counters above count actual writes to GD state, and
section/replay work is reported separately rather than hidden as zero overhead.

For eligible Group 575 members specifically, optimized runs evaluate 25,804,203
and 25,811,916 ordered move-member operations while applying 5,794,720 and
5,821,809 final CPU states. There are no additional Group 575 GPU rotations in
this particular interval; they ran earlier in the same captures. CPU-only
pivot-less barrier sites were bypassed 672/674 times. This last counter counts
barrier calls, not an independently measured number of GPU submissions saved.

The benefit comes from coalescing repeated state propagation and avoiding
unrelated stream drains. The rejected GPU variants reduced nomination counters
without improving frame times; nomination reduction alone was not accepted.

## Correctness checks and their limits

* The final native-reference suite passes **13 scenarios, three steps each**,
  comparing positions, rotation, history, dirty flags, callbacks, command stamps,
  claims, counts and root state. Its added disjoint CPU-rotation case asserts
  that the selective barrier actually activates in stream/culling proof modes.
  Existing mixed-target, pivot, section overflow, dynamic reference and visible
  resolution cases continue to pass.
* Eight live batches containing real Group 575 members pass a bit-exact ordered
  CPU/GPU comparison, including intermediate section-event output. This checks
  actual GPU results before application, not an independent preview.
* All **59 common player checkpoints** in each optimized run match control 1
  for logged position, velocity, acceleration/speed raw value, speed-portal state,
  multiplier, time warp and mode/death flags. Native fixed physics deltas remain
  1/240 s in the comparable heavy windows; full-speed audit passes. The SDK field
  logged as `speed_ac` is excluded from this claim: its semantics are unverified
  and its values differ between both control runs as well.
* Full-level sampled decoration snapshots are **not bit-identical across
  wall-paced runs**, including control versus control. At step 9600, the two
  controls differ for the same 455 sampled X / 268 sampled Y values, by up to
  14.5026 / 0.7502 world units. Optimized/control differences are up to
  6.4456 / 0.3334 and 9.6683 / 0.5001. No rotation-field differences were found
  in these samples. These frame-partition-dependent snapshots do not establish
  identical-input whole-level parity, and no universal epsilon is imposed.
  Native fixtures and same-input batch comparisons are the arithmetic checks.
* Automated no-input runs do not validate every input-dependent event, audio
  synchronization, practice checkpoint or third-party mod consumer. No visual
  correctness claim is based on screenshots. One focused human visual check is
  still required for this experimental option.

The earlier immediate-CPU section-callback and fused-finalizer experiments were
rejected (28.14 and 26.86 FPS). The GPU exception with forced CPU rotations was
also rejected. Their reports and the original validated package remain under
`build/diagnostic-reports`; the rejected loops are absent from the live path.

## Reproduction and manual check

Use `scripts/benchmark-transform-matrix.ps1 -Modes batching -TransformDiagnostics`
for the control, adding `-Group575` for the candidate, with distinct output
directories. `scripts/analyze-group575.ps1` accepts those directories and writes
`build/diagnostic-reports/group575-comparison.json` with counters and state checks.
`VULKANIZER_GROUP575_VERIFY=1` adds the first eight live reference comparisons;
use that for correctness, not the timing runs.

For the single manual check, select Zink, enable **Experimental automatic
transform optimization** and **Experimental Group 575 movement**, and restart.
Keep the separate pilot, culling, precompilation and launcher capture/benchmark
options off. Play the same Peaceful section through the drop and level time
80–90 seconds. Report misaligned/lagging decoration, player slowdown, a freeze
or a crash; otherwise report that it matches the previous appearance. Toggle
Group 575 movement off and restart to restore the previous protection policy.
