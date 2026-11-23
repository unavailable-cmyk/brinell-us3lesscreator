# Experimental transformation culling

GD Windows x64 2.2091, Geode SDK/loader 6.0.0-alpha.1. This builds on
`RotationScheduler.cpp`'s shared GPU action stream; it does not replace Zink,
the scheduler, the atlas experiment, or the existing fallback.

## Configuration

`experimental-transform-culling` is an independent boolean, **false by default**,
requiring restart. It needs experimental Zink and automatic transform optimization.
The public launcher does not enable it automatically. Turn it off and restart to
retain the existing batching route; turn automatic transforms off for native GD.
The environment override `VULKANIZER_TRANSFORM_CULLING` is for automated tests.

The option now chooses between culling and the existing batching path adaptively.
It measures elapsed processing time inside the transform stream divided by logical
object visits, including GPU waits and CPU application, rather than FPS. After a
120-visibility-frame culling sample it measures a batching sample; it requires a
10% measured advantage to select culling, and rechecks after 960 batching frames.
Samples below 10,000 visits do not decide. These are observations over different
time windows, so changing workload can still bias a decision; the periodic recheck
and margin limit switching but do not make the estimate a controlled experiment.
Pending cohorts are force-resolved before the path changes, retaining action order.

## What is actually deferred

The CPU still activates triggers, resolves easing, action order, claims, group
membership, and pivot parameters. A group records one ordered action descriptor.
Objects with identical participating group lists share the merged action tape.
Only materialized objects are packed into the existing OpenGL compute executor
running through GD's Zink context. GPU output replaces the position arithmetic;
one readback per submission supplies authoritative object state and section events.

With culling enabled, each group combination is partitioned into cached spatial
cohorts (256 game units), with a shared action cursor and swept interval bounds.
Advancing bounds evaluates actions per cohort, not per object. Off-screen cohorts
retain their tape cursor and skip individual GPU inputs, readback entries, position
writes, rotation setters, section updates, and Cocos propagation until resolution.
Actions are **not algebraically collapsed**: the evaluator preserves the original
ordered float/double rounding sequence when it resolves them.

Membership is validated against the cached pointer array; changes first resolve
pending work and rebuild affected combinations. Object references live until scene
reset. Empty combinations cannot retain action prefixes. Resolution prunes consumed
prefixes and renumbers shared indices; no per-object operation queues are allocated.

## Visibility and bounds

The viewport includes inverse-transformed window corners, native camera dimensions,
camera motion, shake, a generous visibility margin, and the previous viewport.
Each cohort tracks all intermediate swept bounds through ordered moves/rotations,
object offset ranges, sprite extents, fade margin, and float-rounding expansion.
Invalid/nonfinite bounds force resolution. Resolved GPU coordinates are checked
against the prediction; a violation drains pending work and disables culling.
This is conservative and may resolve many invisible objects. It does not use a
whole-level visibility scan on each simulation step.

Queues resolve by age (staggered after four visibility frames, mandatory at eight),
256 pending cohort operations, or the global 16,384-action record limit. These
limits bound accumulation but cannot guarantee a hard frame-time limit for an
arbitrarily large group or camera teleport. A required-state/visibility barrier
always takes precedence over a time budget.

## Eligibility and fallback

Only positively selected ordinary `GameObject` decoration is eligible. Collision
objects, players, triggers, start positions, particle/animated/custom children,
parent/linked/area dependencies, UI objects, active Cocos actions, unsupported
dynamic geometry, and protected targets retain the CPU path. Mixed groups are
partitioned; one solid member does not reject every decorative member.

A level audit accepts only explicitly handled trigger families. Dynamic target,
player/camera lock, and related target groups are protected. Actual pivot objects
and move-calculation dependencies are CPU anchors. A new runtime move anchor drains
pending work before the native read and is removed from GPU membership. An unknown
runtime pivot disables the stream. Unsupported trigger families or remapping
retain the previous processing route. This is not universal compatibility with
arbitrary mods that read or mutate decorative object state outside audited paths.

The GPU returns ordered section-transition events; native `updateObjectSection`
performs real spatial-container changes at their intermediate positions. More than
four crossings, extreme coordinates, or unsupported event conditions replay that
object's ordered operations on CPU. Dependency barriers, reset, scene exit and
test checkpoints force authoritative resolution. No delayed gameplay state is used
for the protected CPU objects.

The live V2 section-skip adapter can opt into `VULKANIZER_V2_CACHED_POLICIES=1`.
Compilation stores the stable object-type, class, trigger and start-position
classification in a byte array indexed by the dense `m_uniqueID` table. Parent,
linked-group, UI and extended-collision fields, coordinates, numerical validity
and section-crossing checks remain dynamic because their full mutation coverage
is not proven. Scene generation and object-pointer checks guard policy reads;
group/parent changes, type/start-position changes, reset, and retirement invalidate
the affected cache. Ownership is read live at each callback, so handoffs do not
leave cached ownership state. Consecutive movement arrays reuse a route only when
the exact array pointer and scene generation match. Invalid or unknown identities
stay on the native path. Aggregate cache counters are included in the V2 report.

## Validation and interpretation

The native scheduler fixtures compare CPU/GPU/shared/culling paths for 12 scenarios,
including overlapping groups, membership changes, independent pivots, history,
orientation locks, move/rotation ordering, section overflow, dynamic required-state
reads, off-screen-to-visible transitions, camera movement and zoom. A separate
12-step test verifies staggered budget resolution, final native state and an empty
action queue. These tests do not prove compatibility with every level/mod.

The former fixed-delta test override has been **removed**. At 30 FPS, replacing a
native delta with 1/60 second prevented catch-up and produced roughly half-speed
gameplay. Captures using it are invalid. The obsolete
`VULKANIZER_SCHEDULER_FIXED_INPUT` flag now rejects the automatic test instead of
changing game timing. Neither the scheduler nor physics hook modifies `dt`.
Checkpoint hashes sample up to 4,096 objects; they compare native position bits
and rotation offsets at matching simulation steps. Different render-frame
partitions and native half-ticks can confound cross-run comparisons; a matching
step count alone is insufficient evidence of identical input.

`scripts/compare-transform-culling.ps1` rejects missing completion, mismatched builds,
state differences and GPU/fallback failures. Its activation check starts only after
the real `PlayLayer` begins, excluding fixture GPU dispatches. Fixed-input captures
are explicitly marked ineligible as performance captures. A separate, matching
`compare-gameplay-speed.ps1` report must also pass before a performance capture is
considered valid. See `gameplay-speed-regression.md`.

Logs distinguish logical action/object visits, GPU materializations, CPU replay,
section events, deferred object windows, deferred operations, resolution causes,
packing, fence waiting, readback, application and total wall time. Deferred windows
are repeated opportunities, not unique objects. Logical visits minus final writes
must not be presented as every native callback eliminated; section replay and
protected CPU objects remain. Fence wait includes queued GPU execution and must
not be added to GPU execution again.

Measured results and outstanding validation belong in
`build/diagnostic-reports/transform-culling-results.json`.
