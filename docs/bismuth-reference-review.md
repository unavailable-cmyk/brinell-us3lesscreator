# Bismuth architectural reference and Vulkanizer rotation work

## Scope and provenance

Inspected the user's `Bismuth-master.zip`, SHA-256
`C560D57B77604ABD8E178D8166899C5410176AE9AE744A3F1B801C15F2E1BADD`.
Read the license, README, technical explanation and selected renderer source.
No Bismuth implementation was copied, translated, linked or packaged. The
Vulkanizer changes were written from its existing code and the installed GD
2.2091 / Cocos DLL instruction sequences. Bismuth's decompilation files were
not used to implement the replacement helpers.

The archive's license contains GPLv3 terms with additional restrictions on
AI-modified binary distribution and generative level creation/modification.
This review does not authorize redistributing a modified Bismuth build.

## Verified architectural findings

| Available source/document | Technique | Vulkanizer relevance |
|---|---|---|
| `GroupManager.hpp`, technical explanation | Group-combination IDs and shared transformation states | Can reduce repeated work across objects with identical memberships, but shared transforms cannot replace authoritative CPU positions when gameplay reads those positions between actions. |
| `WorkGroupScheduler.hpp` | Sorted batches cached by color-channel blend-state keys | Repeated draw ordering can be cached when the complete ordering key repeats. Vulkanizer still uses Cocos batches; adopting this requires explicit invalidation for atlas/order/lifetime changes. |
| `ObjectSorter.cpp` | Separate layer/batch ordering and object collection | Avoid rebuilding traversal/order data when nothing relevant changed. It does not justify skipping gameplay updates or sorting trigger actions. |
| `Renderer.cpp`, `objectCulling.comp`, buffer headers, technical explanation | GPU object/sprite data, culling and draw preparation | This architecture can eliminate CPU sprite propagation and readback. It is a different integration boundary from Vulkanizer's authoritative-state hybrid path. |
| `renderer/hooks.cpp` | Hooks into GD's rendering/transform flow | Running two replacements simultaneously risks competing ownership. Vulkanizer now disables its transform replacement if Geode reports Bismuth loaded. |

These are source/document findings. Bismuth was not installed or benchmarked;
no claims are made about the behavior or speed of an unavailable compiled build.
The technical explanation's performance estimates are not measurements for
Vulkanizer or for this computer.

## Independent changes applied to Vulkanizer

1. Resolve the Cocos transform once per rotation helper rather than relocating
   a Cocos node, resolving its world transform, and concatenating matrices for
   every object. A CPU loop applies the same float MULSS/ADDSS translation
   sequence. Dirty/history changes, `rotateObject`, section updates and the
   finalizer stay in original object order. The translation node's final state
   is restored to the last native center-relative position.
2. Correct the GPU shader to match the actual rotation helper. Native GD first
   narrows the position minus object offset to float, subtracts its center,
   computes affine-concat translation in float, forms a float delta, then adds
   that delta to the original double position. Cocos' general point-transform
   helper uses different intermediate precision and is not a valid oracle.
3. Remove CPU-path state/action staging and per-object hash-node allocation.
   GPU duplicate checks sort reusable scratch pointers without reordering the
   authoritative object array. Existing reusable SSBO capacity remains.
4. Detach the per-object section/rotation diagnostic wrappers when detailed
   diagnostics are disabled, in addition to the pre-existing phase wrappers.
5. Keep automatic movement native. Apply at most one live GPU rotation batch
   for verification, then use the faster CPU rotation implementation. The GPU
   evaluator remains implemented and is tested against native GD, but repeated
   same-frame readback is not forced after measurements show it loses.

## Automatic validation and limits

The opt-in test environment `VULKANIZER_ROTATION_SELFTEST=1` schedules a test
after the main menu is initialized. A separate GD layer and real GameObject
instances exercise the actual native helpers. Fixtures explicitly configure
decoration and solid classification fields; creating a sprite alone is not
assumed equivalent to complete level loading.

The test compares native GD against GPU and CPU candidates over ordered moves
and external-center rotations, including zero/positive/negative angles,
accumulated rotation, free/locked orientation and mixed decoration/solid state.
It checks positions, sprite rotations and offsets, prior-position history,
command stamps, dirty flags, callback sequence, and the final translation-node
position. Section and finalizer callbacks are recorded by the scratch layer,
without mutating an actual level's sections. Production calls retain GD's real
callbacks. A failed state comparison leaves the native helper active.

Timings use the native helper and both candidates on equivalent configured
decoration arrays at 128, 1024 and 8192 objects, 32 calls each. Candidate timing
includes its live eligibility audit. This measures helper work with recorded
callbacks, not real section-container mutation, rendering or whole-frame speed.
No Peaceful FPS improvement is inferred from it.

Final controlled run: `Geode 2026-11-15 01.30.47.log`, session
`dist/sessions/20261115-013047-3b2afcf4`. Native state comparison and the test
suite reported PASS. The log confirms diagnostic section/rotation wrappers
were detached. Total wall times in milliseconds for 32 helper calls:

| Objects per call | Native | CPU candidate | Hybrid GPU |
|---:|---:|---:|---:|
| 128 | 0.1081 | 0.0607 | 12.1178 |
| 1,024 | 0.8569 | 0.5302 | 15.9271 |
| 8,192 | 9.2231 | 5.7387 | 35.3955 |

The CPU candidate took 38–44% less helper time in this single controlled run.
No hybrid GPU break-even occurred at these tested sizes. These are not
statistical frame-time results. Concise machine-readable results and selected
evidence are in `build/diagnostic-reports/rotation-final-benchmarks.json` and
`rotation-final-evidence.log`. Earlier timings excluded the eligibility scan
or included diagnostic wrappers and should not be substituted for these.

GPU verification supports whole arrays of positively classified plain
decorations without group/area parents or custom parent mode. The later mixed
workload revision extends the CPU path to tested solid/decoration arrays in
native object order. Trigger, player or parent-dependent arrays fall back to GD.
The setting remains off by
default. The runtime failure fuse prevents further submissions after a GPU
fence/device failure and preserves vanilla fallback. The standalone GPU-resident
atlas work has not been removed or replaced.

Shared transform caching across helpers, deferred section updates, scene-wide
GPU culling and GPU-resident Cocos geometry remain unimplemented here. Their
invalidation/dependency rules need direct integration evidence before use.
