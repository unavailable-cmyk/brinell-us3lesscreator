# Chain eligibility census — 2026-11-16

Status: **partial census; not sufficient to approve a compiled evaluator**.
The follow-up attribution and timing results at the end of this document supersede
the earlier suggestion that residual helper visits might be propagation counts.
No compiler, persistent cache or GPU-resident renderer was added. Diagnostic
candidate: `build/diagnostic-reports/eligibility-census-candidate.geode`.
The previously installed package was restored after testing.

## Captures and measurement boundaries

Two alternating diagnostics-off/on pairs were captured for Cache Test (7 seconds
after scene creation) and Peaceful 2 (45 seconds). All use the same binary,
normal native delta arguments, batching and no culling, no precompilation,
no heavy frame diagnostics, no PresentMon or CPU trace. Settings were restored.
Existing native scheduler fixtures ran before optimized tests. No screenshots
were taken. Inputs were not recorded/replayed: these are repeated automatic
runs, **not proven identical input traces**. This prevents a strict causal
overhead or cross-run visual-equivalence conclusion.

Raw logs and per-frame data are in `build/diagnostic-reports/census-*`.
`eligibility-census-timing.json` and `summarize-eligibility-census.ps1` contain
the section comparison. No new rendering/state optimization was introduced.

| Peaceful level-time 80–90 | Mean ms | FPS | p95 ms | p99 ms | Game / wall |
|---|---:|---:|---:|---:|---:|
| Control 1 | 35.308 | 28.3 | 46.16 | 55.51 | 1.000 |
| Census 1 | 34.309 | 29.1 | 46.00 | 57.70 | 1.000 |
| Control 2 | 34.640 | 28.9 | 47.44 | 58.34 | 1.000 |
| Census 2 | 35.423 | 28.2 | 46.40 | 58.47 | 1.000 |

The observed mean-time differences are approximately -2.8% and +2.3%.
Overhead is **not resolved by these repeats**; zero overhead is not established.
Cache Test control means were 1.321/1.340 ms, versus 1.270/1.326 ms with census.
The small-level frame window is level time 0.5–3, covering the overlapping actions.

## Eligibility and observed work

The runtime A/B counters are taken only after actual native action parameter
resolution and conservative GPU eligibility checks. They retain native action
ordinals, per-object arithmetic and CPU dependency barriers. They are operations,
not unique trigger instances. Activation via touch/spawn is independent of these
computation categories. Static C/D/E records are candidate classifications, not
verified shared-evaluation eligibility.

Last periodic Peaceful snapshots, not exact end-of-capture totals:

| Metric | Census 1 | Census 2 |
|---|---:|---:|
| A translation helper entries | 19,498 | 19,736 |
| A eligible object operations | 87,113,893 | 87,305,816 |
| B affine helper entries | 42,990 | 42,975 |
| B eligible object operations | 100,190,835 | 100,189,467 |
| Eligible unique objects retained | 43,160 | 43,160 |
| Active / dormant membership combinations | 20 / 31 | 15 / 36 |
| Largest combination | 5,030 objects | 5,030 objects |
| CPU move visits, category unresolved | 142,324,503 | 142,266,895 |
| CPU rotation visits, category unresolved | 45,590,995 | 45,571,959 |
| GPU final state writes | 33,155,333 | 33,067,850 |

There are 51 nonempty membership combinations in these snapshots, averaging
846.3 retained objects per combination. **This is not an exact-history
deduplication ratio**: different objects have different initial states/offsets,
and this counter does not certify an equivalent chain evaluator. The current
GPU kernel still performs individual ordered operations. Per-object arithmetic
evaluations eliminated by a new compiled evaluator: **zero** (none implemented).
Final-write consolidation already exists; the above is not a new improvement.

Cache Test had two active combinations of eight objects each; its snapshot
recorded 3,664 translation and 7,680 affine object operations, 7,680 final writes,
480 submissions, ~210.5 ms waiting and ~62.8 ms readback. This demonstrates why
a tiny decorative workload should not automatically be sent to synchronous GPU
compute just because it can be shared or batched.

## Processing costs — measured, but not an eligibility CPU-time distribution

Peaceful snapshot 1 includes approximately 2,612 ms packing (inclusive), 2,273 ms
state/Cocos application, 558 ms section transition handling (inclusive), 1,765 ms
GPU fence wait, 1,204 ms readback, 373 ms upload and 36 ms dispatch submission.
Do not sum these indiscriminately: packing includes upload-adjacent preparation,
and transition replay includes transformation work. Fence wait includes queued
GPU execution. The section-call sampler measured 15.2 ms over 420,753 samples;
it is biased by array ordinal and **must not be multiplied into a whole-level
exclusive estimate**. Sampled objects were nearly all decoration, with zero
ordinary solids sampled; this does not prove the absence of solid work.

Whole-frame CPU-time percentages for A/B/C/D/E are **unmeasured**. The current
collection does not separate easing from native scheduling, C color/alpha/pulse
costs, D dependencies, or E unknown semantics. Nor does it independently measure
dirty-flag writes, vertex rebuilding and render submission. Reporting numerical
percentages here would fabricate precision. Those missing exclusive scopes are
required before this census can be called comprehensive.

## Static workload planning

Both Peaceful scans counted A=4,832 ordinary move candidates, B=2,069 rotation
candidates, C=5,062 potential color/alpha/pulse candidates, D=391 recognized
runtime/dependency events and E=174 remaining trigger records. There were
14 touch-marked and 40 spawn-marked records and 2,127 dependency references.
Summed target membership was 5,450,691, **not unique objects or simultaneous work**.
Cache Test: A=2, B=1, 16 eligible objects and one pivot reference.

`VULKANIZER_STATIC_CENSUS` logs 3,000-unit X-position bins, maximum trigger
duration and dependency-reference counts. These are spatial planning estimates,
**not the native editor timeline**. Camera/speed changes, channels, spawn delays,
touch input, interruption and cycles prevent treating position as activation time.
Native timeline interval extraction, effective overlap analysis and SCC detection
have not been implemented. A reference count is not a cycle detector.

## Numerical and gameplay limits

The existing actual-native scheduler fixtures check bitwise state equivalence
for their supported scenarios (including approximately 10,000-unit positions),
not arbitrary affine composition or every high-coordinate case. No new universal
epsilon is imposed. The standalone CPU illustration in
`census-double-composition.json` compares 10,000 sequential double additions of
0.1 with one folded addition: drift ranges from 8.82e-10 at X=1,000 to
2.384e-4 at X=1e9. This is **not native GD affine verification**, and X=1e9 is
outside the conservative GPU section-event coordinate subset. It demonstrates
why algebraic folding is not automatically bitwise equivalent. Cumulative
screen-space drift and high-coordinate native affine divergence remain unmeasured.

Collision objects, actual pivot/target dependencies, player/camera lock groups,
parents, linked objects, animation/particle dependencies and unverified trigger
families retain authoritative processing. Lack of collision alone never grants
eligibility. These runs do not validate practice/checkpoint/restart behavior,
touch-input replay, layering or visual tolerances.

## Recommendation

Do not proceed to a general affine chain evaluator yet. First establish exact
runtime histories and exclusive cost attribution for the protected CPU decoration
path: it still receives roughly 188 million visits in the snapshots. Shared
parameters already exist; sharing a matrix alone does not remove these accesses.
The first evaluator candidate should target an independently verified translation
subset, retain native per-object rounding where needed, use CPU execution for
tiny batches, and prove reductions in object-state commits and dependency-safe
traversals. Potential savings are estimates until measured against native state.

This report does **not** satisfy the requested complete census: native timeline,
cycle graph, full exclusive category distribution, replayed identical input,
high-coordinate native composition tests and vertex/render scopes remain open.

## Follow-up: exact counter domains and measured execution/wait separation

2026-11-16, diagnostic artifact `causal-census-candidate.geode`. Previously
validated package restored afterward. No GPU-resident prototype was built.

### De-overlap established from source and new counters

`recordVisualAction` increments the 187M logical operations by the eligible
subset's size. `tryVisualMoveObjects` increments residual movement nominations
either when routing returns no usable GPU subset (then native/hybrid fallback
is called), or by the mixed group's retained CPU subset size. `execute` increments
residual rotation nominations in the no-center orientation loop, or for the
remaining inline scheduler body after removing GPU objects from that action.

For a successfully split helper call these eligible and residual subsets are
disjoint. Across different calls, steps and actions, the same physical object
can occur in either population repeatedly. These are **not unique objects**.
`replayedVisits`, `stateWrites` and section events describe later passes and
must not be added as new independent objects or classified CPU math without
accounting for their relationship to the original operations.

None of the residual nomination counters increments in `setPosition`, Cocos
dirtying, `updateOrientedBox`, section callbacks, camera or parallax propagation.
Those costs may execute downstream of nominated work, but are not the cause
of a second entry in the 188M counter. It was incorrect to interpret that
number itself as evidence of 188M downstream propagation visits. A general
counter-label caveat: the scheduler's first verified GPU sample can be included
in the body counter named CPU, despite using GPU arithmetic. The new subset
counter was zero in both final periodic snapshots below.

| Residual helper nominations | Peaceful run 1 | Peaceful run 2 |
|---|---:|---:|
| Move: no usable routed GPU subset / fallback | 137,233,664 | 136,816,069 |
| Move: retained CPU subset in mixed group | 5,026,799 | 5,026,859 |
| Rotation: no-center orientation loop | 2,156,187 | 2,152,964 |
| Rotation: inline scheduler body subset | 43,415,772 | 43,392,982 |
| GPU verification sample included in that body | 0 | 0 |
| Total residual nominations | 187,832,422 | 187,388,874 |

This is an exact breakdown by **instrumented source branch** of those counters,
not a completed attribution to originating native trigger IDs or machine-code
call sites. In particular, the fallback bucket does not yet separate not-ready,
unsupported, protected, empty-eligible, follow or remapped causal origins. Local
GD disassembly confirms calls from `processMoveActions` at RVAs 0x33645a and
0x3364ef into `moveObjects` (0x3368d0), but this does not prove they are the only
callers represented in the runtime bucket. No stack-sampled caller census was
collected. No nomination is declared redundant or eliminable by this table.

### Four separate measurements

The executor now optionally samples `QueryPerformanceCounter` elapsed wall time,
`QueryThreadCycleTime` raw main-thread cycle deltas (including a nested fence
cycle delta), the existing GL elapsed GPU timestamp query, and fence wall time.
The extra QPC/cycle instrumentation is disabled in the control condition.
These are the **last periodic cumulative executor snapshots**, not complete
CPU propagation-stage timings, and not medians of individual dispatches.

| Measurement | Cache 1 | Cache 2 | Peaceful 1 | Peaceful 2 |
|---|---:|---:|---:|---:|
| QPC executor wall, ms | 289.557 | 286.627 | 3,377.653 | 3,390.546 |
| Executor thread cycles, raw | 175,070,705 | 184,187,651 | 3,772,727,713 | 3,871,241,497 |
| Fence thread cycles, raw subset | 92,669,534 | 103,757,429 | 752,277,705 | 748,381,653 |
| GPU query execution, ms | 3.756 | 3.745 | 200.164 | 199.875 |
| Fence wall, ms | 215.768 | 216.381 | 1,759.617 | 1,757.247 |

Thread cycles are **not milliseconds**, cannot be converted using an assumed
fixed CPU frequency, and can include driver spinning. Fence cycles are already
inside executor cycles. GPU work overlaps fence waiting; do not add it again to
wall latency. Cache Test's roughly 216 ms fence wall versus 3.75 ms GPU query
execution is evidence against interpreting that wait as computational GPU
saturation. It does not alone distinguish scheduling, submission and driver costs.
The CPU state-commit stage still needs its own QPC and thread-cycle scope; the
executor numbers are not a replacement for that measurement.

### Interleaved frame distributions

Run order was Cache control/on, Peaceful control/on, repeated twice. The same
binary and settings were used; only the diagnostic switch varied. Windows:
Cache Test level time 0.5–3; Peaceful 80–90. Per-frame data was buffered and
written after completion. Median/P95/P99 below are within each run, not a pooled
statistical confidence interval. Machine state and input replay were uncontrolled.

| Level | Condition / repeat | Median ms | P95 ms | P99 ms | Game / wall |
|---|---|---:|---:|---:|---:|
| Cache | Control 1 | 1.174 | 1.98 | 2.32 | 1.000 |
| Cache | Diagnostic 1 | 1.127 | 1.88 | 2.34 | 1.000 |
| Cache | Control 2 | 1.145 | 1.80 | 1.92 | 1.001 |
| Cache | Diagnostic 2 | 1.136 | 1.85 | 2.34 | 1.000 |
| Peaceful | Control 1 | 37.621 | 46.29 | 50.28 | 1.000 |
| Peaceful | Diagnostic 1 | 36.988 | 48.58 | 58.01 | 1.000 |
| Peaceful | Control 2 | 36.560 | 44.78 | 49.59 | 1.000 |
| Peaceful | Diagnostic 2 | 38.019 | 47.12 | 58.86 | 1.000 |

Raw output: `causal-timing.json`, `causal-*-control-*`, `causal-*-on-*` in the
diagnostic report directory. Two repeats do not establish a statistically
reliable overhead baseline, especially at P99. No speedup is claimed.

### Transitive CPU consumers and outstanding verification

**Verified count of objects with zero transitive CPU consumers: not established.**
The 43,160 eligible objects are a bounded execution whitelist, not a proven
GPU-resident population. The existing audit protects resolved pivot objects,
dynamic target/player/camera groups, `m_targetGroups`, and runtime move-node
rotation references. It does not construct a complete transitive reader graph
for Follow/Scale/Collision consumers or arbitrary mod reads. Counter-trigger
names or NoTouch alone must not be used to certify coordinate independence.

Identical-input replay is **not implemented or claimed**. These tests were
automatic runs using ordinary timing, with existing checkpoints recorded; they
were not a verified identical-input experiment. Adding a fixed-delta override
would not satisfy this requirement and was not done. No new input behavior was
added during this attribution pass.

The requested completion gate remains closed: caller/trigger attribution within
the fallback bucket, separately scoped authoritative CPU commits, transitive
consumer closure, and verified replay plus adequate repeats remain outstanding.
The previous partial-census limitations still apply. No GPU-resident work is
authorized by these results alone.

## Completed focused attribution of the movement fallback bucket

This section closes **movement fallback reason/source-branch attribution for
the new captures**, not the other outstanding census requirements. No compiler
or GPU-resident prototype was implemented. The previously validated package
was restored after the diagnostic runs.

### Capture scope and reconciliation

Two focused 45-second Peaceful runs were executed, retaining native timing,
the existing executor, normal trigger activation, and gameplay fallbacks.
The first run reconciled 135,624,301 fallback nominations. It established that
130,440,443 were protected-target rejections, 5,140,800 linked-group rejections,
33,212 non-decoration types and 9,846 parent-mode rejections. Every nomination
was assigned a first rejection gate. Native Windows stack unwinding returned
no GD caller through the Geode hook chain, so caller RVA 0 in that run was
**unresolved**, not a real call site. This was investigated rather than accepted.

The second run added native-phase/array-domain attribution and protection
provenance. All **136,428,481** nominations reconciled exactly against the
original fallback counter, across 29 source-group/rejection rows. Unknown
reason rows: **0**. Unresolved native caller rows: **0**.

Raw evidence: `build/diagnostic-reports/move-attribution-1/batching.geode.log`
and `move-attribution-2/batching.geode.log`. Full second-run rows are in
`move-attribution-2/attribution.json`; totals and methodology are in `summary.json`.
The prior historical 137,233,664 capture did not record member rejection or
caller metadata, so that exact historical count cannot be retroactively split.
These are fresh runs of the same instrumented bucket; totals vary with frame
partitioning and completed simulation work. No historical attribution was invented.

### Exact source branches behind the fallback

| First rejecting condition | Nominations | Share |
|---|---:|---:|
| Membership in a protected player-X-lock target group | 131,244,621 | 96.20% |
| `m_linkedGroup` nonzero | 5,140,800 | 3.77% |
| `m_objectType != Decoration` | 33,214 | 0.024% |
| `m_parentMode > 0` | 9,846 | 0.007% |
| Total | **136,428,481** | **100%** |

These are mutually exclusive **first-failing-gate counts**, not a claim that
objects cannot have several dependencies. The gate ordering is explicit in
`visualObjectRejection`; member histograms are built with group snapshots and
rebuilt on membership or dynamic-anchor changes. No per-object clock was added.
The scope includes only the existing fallback nomination counter after the
visual owner is initialized; no broader camera/render traversal was folded in.

Every protected rejection in the second capture carried cause bit **64**:
`EffectGameObject::m_lockToPlayerX`. None was attributed to Follow, random,
Spawn remapping, unknown trigger families, GPU errors, missing stream readiness,
command-stamp limits or StartPos loading in this capture. That does not establish
that such operations are absent elsewhere in GD; it establishes why this bucket
fell back during these runs.

The dependency audit protects the target, center, rotation-target and modifier-
center group IDs referenced by qualifying movement/rotation triggers. Objects
in **any** such group are excluded, including when the current helper action
itself is ordinary movement. This is a conservative level-wide rule, not a
runtime proof that each member's coordinate is being consumed on every visit.
The count therefore identifies a potential over-conservative eligibility policy;
it does **not** prove that disabling protection would preserve behavior.

### Native call sites

| GD 2.2091 native call instruction | Source array domain | Nominations |
|---|---|---:|
| RVA **0x33645a** inside `processMoveActions` | `m_staticGroups` | 87,049,620 |
| RVA **0x3364ef** inside `processMoveActions` | `m_optimizedGroups` | 49,378,861 |
| Other / unresolved | — | **0** |

Both disassembled calls target `moveObjects` at RVA 0x3368d0. Attribution is
**source-branch reconstruction**, not a successful raw return-address capture:
the diagnostic brackets actual native `processMoveActions`, identifies the
passed array by pointer in the native static/optimized vectors, and maps the
distinct domains to those two verified instructions. Static assertions validate
the SDK's vector offsets against the disassembly (0xf50 and 0xf68). Ambiguous
cross-domain aliases do not get assigned either call site; the fallback stack
capture remains unresolved if it cannot identify a native frame. No such
unresolved nominations occurred in the final run. Source-group labels come
from the array lookup and are separate from protected consumer-group labels.

| Largest source group / cause | Static call | Optimized call | Combined |
|---|---:|---:|---:|
| Group 575 / player-X-lock protection | 76,849,890 | 11,878,020 | **88,727,910** |
| Group 806 / player-X-lock protection | 0 | 15,957,134 | 15,957,134 |
| Group 913 / player-X-lock protection | 0 | 10,225,976 | 10,225,976 |
| Group 815 / linked-group first gate | 5,140,800 | 0 | 5,140,800 |
| Group 815 / player-X-lock first gate | 1,072,800 | 4,194,155 | 5,266,955 |
| Group 805 / player-X-lock protection | 3,894,258 | 0 | 3,894,258 |

Group 575 alone represents **65.04%** of the fallback bucket. Group 815's
different rows demonstrate why mixed groups must be classified per member and
property rather than rejected as an undifferentiated group.

### Causal conclusion and limits

The dominant bucket is ordinary native movement-helper traversal retained by
Vulkanizer's conservative dependency policy, principally player-X-lock groups.
It is not 137M unobserved downstream `setPosition`, oriented-box, dirty-flag,
section, fade or parallax callbacks. Those may be necessary consequences of
the helper's work; they are not what increments this nomination counter.
No traversal or callback has been declared eliminable by these diagnostics.

These detailed caller/reason runs were for attribution, **not performance**:
the first attempted stack unwinding on every fallback and the final run uses
native-phase array identification plus per-call aggregate updates. Their frame
times must not be substituted for the earlier interleaved timing baseline.
The reason/caller attribution requested for this bucket is complete for the
final captured interval. Verified input replay, transitive consumer closure,
and all other open items in the broader census remain uncompleted and were
not expanded in this focused task.
