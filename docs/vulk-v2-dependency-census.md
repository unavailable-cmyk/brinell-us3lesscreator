# V2 dependency census

## Trigger audit

The pinned 2.2091 binding for `GameObject::getColorIndex` maps ID 899 to `m_targetColor`. It is a color-channel trigger. `EffectGameObject` exposes duration, opacity, player-color, blending, copied color and activation fields. `GJEffectManager` exposes color actions and inheritance. The resulting channel can affect object vertex colors, alpha and blending, including through copied or inherited channels. A target channel is different from a movement group. The direct-channel census compares the target against each object's base and detail color IDs. This is a lower bound because inherited channels, player colors, remapping and dynamic color changes require transitive resolution.

ID 1520 is a camera-shake trigger. In the installed 2.2091 executable, `EffectGameObject::triggerObject` compares the object ID with 0x5f0 at RVA 0x6482f1. Its branch at RVA 0x6483f7 checks positive duration and strength and calls RVA 0x345bc0, the pinned binding for `GJBaseGameLayer::shakeCamera`. The input fields are duration, strength and interval. Its dependency is the camera transform and hence visibility, screen-space error and drawing for objects in view. The branch does not establish an object-position read or a movement target group. All 124 recorded ID 1520 objects had a zero target-group field, which alone does not establish their full remapping behavior.

Spawn and touch activation must retain native ordering. Spawn-trigger remapping can change targets and needs closure across the spawned trigger graph. The inspected 899/1520 records include two spawn-triggered and zero touch-triggered records. The observed 899 `m_copyColorID` field was zero in this snapshot. Color action inheritance and runtime remaps remain separate questions. The [official editor guide](https://www.robtopgames.com/files/GDEditor.pdf) describes trigger activation and remapping.

The census separately records direct color-channel targets and the level-wide camera-shake dependency. It does not treat either as proof that other objects are safe. Current live ownership retains the conservative veto because color-channel inheritance, remapping, material updates and the ordering of camera shake relative to visibility checks have not been validated end to end. This milestone does not expand live ownership.

## Peaceful 2 census

The census traversed all 273,268 loaded objects, including 268,042 initially inactive and 268,143 initially unbatched objects. Every recorded criterion is evaluated independently. The raw report contains 39 individual and aggregate flags. Counts overlap and cannot be summed. Shader and atlas state for inactive objects remain unknown until native activation. The mutually exclusive priority categories in the JSON are diagnostic screening categories, not proof that every member is fundamentally incompatible.

The current structural screen yields zero candidate objects and zero candidate chains when initial visibility, initial atlas readiness, spatial readiness, shader observations and the unresolved level-wide veto are excluded. A second upper-bound screen applies the V1 rule that permits nonpositive parent modes while retaining the other current structural screens. It finds 1,768 prospective objects across 96 distinct group-membership sets. This is not an ownership eligibility result. Material, ordering, activation, dynamic dependency and numerical validation remain outstanding.

V2 currently tests `m_parentMode` as a boolean. The earlier V1 rejection routine tests `m_parentMode > 0`. The census observed 267,755 negative parent modes and 16 positive parent modes. This difference accounts for a major conservative gate and needs native behavior tests before the V2 rule changes. It does not by itself establish that the 1,768 prospective objects are safe.

## Work-weighted results

The native-owned diagnostic run covered approximately six seconds of native level progression starting near level time 61.4. It recorded 16,942,719 native movement member visits. Compiled array routes accounted for all 16,942,719 visits in this capture. Each object's independent flags are weighted by the number of observed calls to arrays containing it. Values overlap across reasons. They describe movement helper visits, not unique objects, CPU time, rotations or a whole-level replay.

| Criterion | Objects | Observed movement visits |
|---|---:|---:|
| Protected group | 191,314 | 16,889,027 |
| Local scale, rotation, skew or flip | 242,095 | 16,639,046 |
| Initially inactive | 268,042 | 15,281,857 |
| Scale differs from one | 216,447 | 15,230,349 |
| Initially unbatched | 268,143 | 15,227,452 |
| Negative parent mode | 267,755 | 14,690,376 |
| Rotation differs from zero | 220,786 | 14,077,072 |
| Non-decoration2 history behavior | 64,036 | 14,054,906 |
| Directly targeted color channel | 166,502 | 7,093,911 |

The 1,768 prospective nonpositive-parent candidates received 744 recorded movement visits in this interval, about 0.0044% of the captured movement visits. The number is a workload observation for this short window, not a whole-level ranking. Native visibility and batching flags are observations taken during compilation, and must not be interpreted as permanent properties of off-screen objects.

Earlier V1 attribution measured 136,428,481 fallback nominations over a different and longer interval. Player-X-lock protected groups accounted for 131,244,621 of those nominations, including 88,727,910 for source group 575. That finding and the present independent flags overlap in scope and must not be added together. The earlier source report is `docs/chain-eligibility-census.md`.

## Milestone 3B

Establish native fixtures for negative parent mode, player-X-lock resolved deltas and dynamically activated off-screen objects. Record complete color-channel inheritance and remapping edges, then verify material handoff before admitting objects affected by ID 899. Verify camera-shake timing and viewport transforms against native drawing before narrowing ID 1520 protection. Prioritize the protected movement groups and transformed geometry that dominate observed work. Preserve the existing native owner for all unresolved cases.

The full report and the executable/source hashes are included under `review/v2-census`. The standard scene, live-contract, frame-profiler and evidence-parser tests passed. The census run restored the previously installed package and settings.
