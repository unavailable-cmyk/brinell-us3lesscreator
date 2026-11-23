# Gameplay-speed regression and validation

The automatic 2026-11-15 21:39:25 test used a fixed 1/60-second delta in the base
game update. This was a test-only attempt to compare native state across render
frame partitions. It was incorrect for real-time gameplay: at approximately 30
rendered frames/second it permitted only about 120 simulation steps/second. One
logged heavy interval processed 212 steps in about two seconds. Its apparent FPS
and state-parity results are invalid. The test was not launched through the normal
public launcher, but was visible on the user's computer.

All delta overrides have been removed, including the uninstalled follow-up
scheduler-level override. The obsolete test flag rejects the automatic test.
Vulkanizer passes native scheduler and physics delta arguments through unchanged.
There is no player-speed multiplier, tick-rate compensation, or skipped physics.
The shared GPU stream, section events, culling cohorts, dependency barriers and
native fallback remain available. Player/camera-locked target groups are CPU
protected as well as collision objects and pivot/target dependencies.

The rotation replacement also now explicitly mirrors native collision-cache
invalidation (GD RVA 0x334b18..0x334b43). After rotating a non-decoration object,
both its rectangle and oriented-box caches are dirtied even if an oriented box
already exists. The SDK `fastRotateObject` inline does not explicitly perform
that unconditional invalidation. The native fixture now clears these caches
between ticks in its mixed solid/decorative scenario; default-dirty fixtures
could conceal an omitted invalidation. The same rule covers primitive replay
and the optional CPU consolidation path.

`VULKANIZER_GAMEPLAY_SPEED_AUDIT` observes native inputs. It reports every two
seconds: wall time, level progression, scheduler-input sum, physics-input sum,
native tick count and minimum/maximum delta, warp and music channel-0 clock. The
recorded deltas are never used to drive the simulation. Startup fast-forward and
pause/reset intervals are excluded from nominal-speed classification.

`VULKANIZER_TRANSFORM_CHECKPOINTS` additionally records player position, native X
velocity, Y velocity, the SDK's ambiguously named acceleration field, speed portal
multiplier, warp, mode, gravity and death state. `speed_ac` is diagnostic raw data,
not assumed to be a stable cross-process physical speed. Native half-ticks can
shift checkpoint sample times; missing equivalent times are not silently treated
as successful comparisons.

`VULKANIZER_TRANSFORM_VANILLA_BASELINE` uses the native rotation scheduler for the
automatic reference run. It does not receive the replacement CPU scheduler or GPU
stream. Baseline completion uses observed native physics steps rather than
requiring a GPU dispatch. Fixture dispatches never count as live activation.

`scripts/compare-gameplay-speed.ps1` requires completed, matching builds, equivalent
sampled player state, live GPU activation, no timing override, and full-speed
level/wall progression in the heavy interval. It rejects mismatches and insufficient
samples. `tests/gameplay-speed-comparison-test.ps1` verifies acceptance of equivalent
state and rejection of half-speed 30 FPS, changed velocity and timing overrides.
The C++ speed-window test also builds, but Windows App Control blocked its separate
executable; it has not been reported as executed.

Music timestamps must actually advance for an audio-sync conclusion. A stopped or
missing channel is reported as unavailable, not as proof of audio parity.
The report must distinguish clock-rate parity from complete gameplay-state parity.
No further optimization result is valid solely because it reaches 30 FPS.

## Final same-build validation

Native control: `Geode 2026-11-15 22.04.09.log`. GPU batching plus culling:
`Geode 2026-11-15 22.05.40.log`. Both completed 35-second automatic runs on the
same installed package. The 12-scenario scheduler proof, including clean cached
solid bounds, passed. All **37 shared player checkpoints matched**, including
**11 in the heavy section**, with no mismatches in the compared physical fields.
The optimized heavy windows advanced **1.00383 level seconds per real second**
and observed **241.75 native callbacks/second**, including GD's native half-ticks.
The corresponding native control advanced 0.97533 level seconds per real second;
its heavier stalls reduce its real-time catch-up rate slightly. No tick delta,
player velocity, or physics multiplier was changed to obtain the optimized rate.

`gameplay-speed-final-comparison.json` records the sampled player/clock parity
pass. This is a bounded test, not universal behavioral equivalence. The audio
clock stopped advancing in both runs, so audio synchronization remains unverified.
Visual/audio confirmation and a separate culling-off comparison remain outstanding;
the average FPS in this pair is not accepted as a culling performance milestone.
