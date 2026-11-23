# Native trigger integration: partial player-lock result

This document supersedes the production static-transform opt-in claim in `vulk-v2-3b-live-expansion.md`. The fixture-value/object-type whitelist and production switch were removed. Static transforms remain native outside the isolated structural fixture. This work used native GD 2.2091, pinned Geode bindings and Vulkanizer's own previous native analysis; no Bismuth material was consulted. The supplied official editor PDF could not be fetched by the web tool because of its size.

## Native-resolved player-X-lock and atlas correction

With the independent opt-in `VULKANIZER_V2_RESOLVED_XLOCK=1`, a move trigger's X-lock flag alone no longer protects its entire recipient group. Native continues to prepare and order movement; the existing `moveObjects` interception receives the resolved delta. Center and rotation targets, target maps, dynamic references, other trigger flags, gameplay objects, object-level eligibility and unresolved level-wide dependencies remain protected. Without the switch the old group protection remains. This is an experimental expansion; player-Y lock, camera lock, target movement and dynamic modes remain protected.

The controlled game fixture sets X lock on a real native trigger with overlapping groups and a native gameplay solid. Before the fix, the paired same-tick capture differed in 30,627 color channels even though positions and checked handed-off sprite/atlas quads matched. The same difference occurred when all X-lock recipients remained native but another group used V2; two independent native-only captures matched. This exposed a mixed atlas draw problem, not proof that X-lock recipients alone were responsible.

Native `libcocos2d.dll` 2.2091 `CCTextureAtlas::drawNumberOfQuads(n, start)`, RVA `0xa9e70`, uploads only the requested dirty range and clears the atlas-wide dirty flag. Splitting a native atlas call around V2 ranges let the first native range clear that flag before subsequent native ranges were uploaded. The correction carries the original dirty state to each native subrange. It preserves native contiguous range draws and uploads only native fallback ranges; it does not reconstruct or routinely upload V2 geometry. Upload counters now account for actual native range payload rather than attributing a whole atlas to each partial upload.

After the correction, the opt-in production eligibility gate's controlled run reached 64 peak owned, 6,988 eliminated movement-member visits, 52,931 remaining native visits, at most two active chains and three dirty entries. Forty-three objects handed off on section boundaries and the other 21 at visibility boundaries; none were retained at the end. At tick 181, 21 remained owned, player state and all sampled positions matched native, and the captured region had zero changed channels. Handed-off sampled sprite/atlas vertices, visibility and atlas indices matched. The default-off control also matched over the full region containing native and V2 objects. The runs are short, controlled and not proof of long sequences, all group dependencies, persistent off-screen ownership or whole-level parity. Fixture screenshots, reports, comparison script and execution hashes are in the archive.

## Peaceful census and short comparison

The opt-in census covered all 273,268 loaded objects and 16,619,225 observed native move-member visits. Actual eligibility, peak ownership and eliminated visits remained **zero**, because 4,278 ID 899 color triggers, 124 ID 1520 camera-shake triggers and other unresolved dependency risks retain the level-wide veto. Protected groups affected 16,561,727 observed visits. The `local_transform` reason affected 16,320,846 visits; reasons overlap.

Excluding visibility/atlas readiness and the unresolved level veto yields 2,206 **prospective**, not verified or owned, objects across 100 membership sets. A hypothetical cumulative screen that additionally ignores static transforms and admits V1-style nonpositive parent modes yields 19,720 objects, 209 sets and 56,239 observed visits. This is a workload observation for the recorded short interval, not a measure of CPU time or an FPS projection. Color/material compatibility projections remain unquantified.

The two short six-second Peaceful runs below used the same final binary, OpenGL backend and opt-in, without the heavy census or per-object reference validation. The native baseline retains V2's ordinary counters and is **not** vanilla or V1. Starts and frame partitioning were not proven identical; no speedup claim is justified.

| Measure | Native baseline | V2 requested |
| --- | ---: | ---: |
| Approximate ending level time | 67.641 | 67.729 |
| Frames | 168 | 170 |
| Eligible / peak owned | 0 / 0 | 0 / 0 |
| Remaining native movement visits | 16,713,063 | 16,835,674 |
| Native section callbacks | 20,885,033 | 21,168,189 |
| Eliminated native visits | 0 | 0 |
| Native / V2 atlas draws | 3,128 / 0 | 3,184 / 0 |
| CPU frame p50 / p95 / p99 ms | 33.781 / 133.901 / 174.263 | 13.496 / 140.575 / 151.111 |
| GPU sample p50 / p95 / p99 ms | 0.141 / 0.406 / 0.710 | 0.121 / 0.406 / 0.715 |

The V2-requested short run recorded 147,744,000 native dirty-range upload bytes across 1,852 upload events; routine V2 geometry uploads and readbacks were zero. The longer 80–90 second attempt documented in `vulk-v2-3b-live-expansion.md` did not reach its checkpoint; no new result for that interval is available. The historical 80–90 recovery-path measurement cannot establish V2 performance.

Milestone 3B remains incomplete. Off-screen ownership, activation and atlas transitions, trigger 1520 shake, trigger 899 color/material changes, alpha, live history, rotation and Follow Player Y are not validated. Keep their current protections until independently tested. Prioritize a replayable player-lock fixture with broader mixed dependencies, then establish persistent ownership and matched vanilla/V1/V2 benchmarks before claiming an improvement.
