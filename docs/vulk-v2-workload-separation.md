# Milestone 3B: workload dependency separation audit

This is a diagnostic stage, not a compatibility expansion. Peaceful still has zero eligible and zero V2-owned objects, with zero eliminated native movement visits. None of the requested persistent ownership, shared history, color, shake or generalized static-transform systems is enabled by this change. The opt-in player-X-lock path and all existing vetoes retain their previous behavior.

## Why the history restriction remains

Vulkanizer's native 2.2091 parity code records `m_lastPosition` as float and changes `m_unk4C4` once per new command index for objects where `m_isDecoration2` is false. The live shared path currently holds a shared current translation and reconstructs the current position at handoff; it does not track and materialize that object's command-indexed previous position or rectangle invalidation. The standalone shared-history prototype does not prove equivalence across real native command routes or mixed ownership. Removing this guard would therefore lose state needed by native consumers after handoff.

The section and visibility checks similarly prevent a V2-owned object's native section membership or atlas entry from becoming stale. Removing them without changing native activation and atlas topology handling would risk missing or duplicate transformations and drawing. The color and shake level vetoes also remain until inherited/remapped material effects and the native view/culling lifecycle are verified; their presence does not imply that every object is intrinsically incompatible.

## Observed six-second Peaceful workload

The game census covered 273,268 loaded objects, including initially invisible ones, and 16,550,767 native movement-member visits. Candidate object counts describe membership in a group with the stated protection cause; they do **not** measure calls to that trigger or prove that every member reads an authoritative position. Causes overlap and may not be summed. X-lock opt-in was enabled; its row still records membership provenance even where X lock by itself no longer rejects the group.

| Group classification | Objects | Observed native movement visits of members |
| --- | ---: | ---: |
| Player-X-lock recipient group | 68,014 | 16,234,867 |
| Trigger 1049 target group | 167,438 | 15,717,049 |
| Trigger 1346 target group | 47,742 | 13,990,744 |
| Trigger 1007 target group | 70,864 | 7,429,565 |
| Center or rotation reference group | 4,370 | 409,050 |
| Trigger 1006 target group | 4,291 | 16,333 |

The current protected-group criterion covered 16,496,692 visits. Non-`m_isDecoration2` / history-enter-fade restrictions covered 13,996,340 visits. Because these categories overlap, neither marginal gain nor the sum of their counts can be inferred from the independent rows.

The following cumulative *hypothetical screens* deliberately ignore specified blockers to reveal overlap; they are not eligibility, actual ownership, a proof of compatibility or a speedup prediction. The first stage excludes the unresolved level veto and initial visibility, atlas and spatial readiness. The next stages cumulatively disregard static-transform, history/enter/fade, and protected-group checks. The custom-shader observation remains checked; unknown inactive-object shaders, indirect material dependencies and live native behavior remain unresolved. Each visit is counted in the compiled native array routes observed during this one run.

| Hypothetical cumulative screen | Objects | Observed visits | Additional visits in this sequence |
| --- | ---: | ---: | ---: |
| Ignore level veto and initial readiness | 2,206 | 648 | 648 |
| Also ignore static transforms | 19,720 | 52,816 | 52,168 |
| Also ignore history/enter/fade | 19,720 | 52,816 | 0 |
| Also ignore protected groups | 183,091 | 15,352,919 | 15,300,103 |

The zero history marginal applies only after the preceding assumptions and while group protections remain. It does not mean native history is unnecessary. The large final row assumes away all protected-group causes together and should not be interpreted as a safe or feasible optimization by itself. It identifies the need to separate rotation, visibility/toggle and material operations and to handle their actual effects before ownership can expand.

## Validation and next work

The Geode package rebuilt successfully. A paired live native player-lock regression matched its same-tick capture, sampled positions and handed-off atlas quads. The census verifier checked bounded overlapping counts and monotonic cumulative screens. These checks establish the diagnostic change and preserve the existing fixture result; they do not validate Peaceful gameplay parity with V2 ownership. Raw reports, execution hashes and captures are in the review archive.

The next meaningful stage is to resolve the high-overlap group operations using native action boundaries, preserving command history and material/visibility authority, and then measure retained live ownership and eliminated work in Peaceful. The current data alone does not justify deleting any of those protections.
