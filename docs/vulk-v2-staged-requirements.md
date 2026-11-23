# Staged V2 requirements

## Stage 1, native transform metadata

The scene representation now stores native position, two rotation axes, scale, skew, anchor and flip state with each compiled object. The live compiler captures these fields from GD objects. The scene API can return the stored transform with the current shared translation applied to its position.

The existing live eligibility gate still admits only unit-scale, unrotated, unskewed and unflipped objects. General transformed baked geometry and restoring all transform fields during native handoff need a native reference fixture before that gate can expand. The controlled metadata assertions have been built, but Windows Application Control blocked their executable. The Geode mod build passed.

## Stage 2, composite history prototype

`SharedHistoryPrototype.hpp` is a standalone controlled model. It checks a supplied ordered route description, initial command stamp and history-suppression flag, and records the pre-command translation once per changed stamp. Tests cover repeated stamps, ordering and route mismatches. Supplying matching route descriptions is not proof of equivalent native action histories. Native fixtures must establish the actual movement routes, resolved action order, stamps, initial history, rounding and handoff fields before any live use. The new test executable was blocked by Windows Application Control.

## Stage 3, ownership and render readiness

No off-screen ownership expansion was enabled. The current live compiler continues to retain inactive and unbatched objects natively. A controlled test must establish movement while geometry is unavailable, later native activation, correct atlas insertion and a handoff without duplicate or missing transformations. Visibility and GPU geometry readiness must be represented independently of transform ownership before enabling that case.

## Coverage

| Coverage | Current evidence |
|---|---|
| A, verified live | 64 controlled-level objects were V2-owned at the matching visual checkpoint. Peaceful 2 had zero admitted objects. |
| B, prospective after validated 3B changes | The census found 1,768 objects in 96 membership sets under the V1 nonpositive-parent screen. Their observed six-second movement workload was 744 visits. These are unverified candidates and the estimate does not include a demonstrated history or off-screen implementation. |
| C, projected after 3C material and color work | Unquantified. Direct target-channel counts overlap other rejection reasons and do not establish a safe set. |
| D, remaining workload | The independent, overlapping reason counts and native movement visit weights are in `vulk-v2-dependency-census.md` and `review/v2-census/baseline.json`. |

Movement-visit counts measure the specified helper's work. CPU frame-time improvements require separate comparable captures. The stages above do not add per-object synchronization, geometry uploads, readbacks or compute dispatches to live V2 translation.
