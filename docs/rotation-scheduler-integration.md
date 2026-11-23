# Actual rotation scheduler integration

> Historical first-integration results below. The current shared GPU batching,
> section-event and culling implementation is described in
> [transform-culling.md](transform-culling.md). Timing validation and the removed
> fixed-delta test regression are described in
> [gameplay-speed-regression.md](gameplay-speed-regression.md). The old single-GPU-
> sample-only description below does not describe the current stream.

Implemented 2026-11-15, GD Windows x64 2.2091 / Geode 6.0.0-alpha.1.

## Replacement and fallback

`CpuRenderPhaseHooks.cpp` now invokes `tryProcessRotationActions` from the actual
`processRotationActions` hook. This functional hook remains installed with frame
diagnostics off. `RotationScheduler.cpp` replaces the native inline position loop;
it no longer depends on the standalone `rotateObjects` helper being called.

**Experimental automatic transform optimization** remains opt-in, default false,
and requires a ready Zink context. Disabled/unavailable, failed parity, or unsupported
scheduler preconditions delegate to native GD. Movement retains its existing path.

Native group selection, action order/claims, history, dirty flags, orientation,
section callbacks, finalizers and root restoration remain. SIMD replaces the
per-object point-construction/affine helper calls with ordered double/float math.
The scheduler rounds differently from the standalone helper, so its CPU kernel and
GPU action flag are separate. Compile-time assertions check accessed field layout.

One independent solid/decoration batch per process gets GPU evaluation, bitwise CPU
comparison, then actual authoritative-state application before native callbacks.
Players, triggers, start positions, linked/parent-dependent objects, unsupported
classes and duplicates are excluded from this GPU sample. Subsequent scheduler
work uses CPU SIMD to avoid readback stalls. The verification batch repeats CPU
arithmetic and is **not credited as CPU savings**. GPU failure/mismatch finishes
the current claimed action on CPU, then future calls use native GD. Existing
GPU-resident atlas work is retained.

## Automated evidence

Extract: `build/diagnostic-reports/scheduler-live-evidence.log`.
Initial installed-build run: `Geode 2026-11-15 12.24.27.log`; session
`dist/sessions/20261115-122426-160159d6`.

* Native scheduler versus replacement: seven scenarios, three consecutive steps,
  CPU and GPU modes. Passed position bits, orientation, history, dirty flags,
  oriented-box state, callback order/state, claims/stamps, counts, root restoration.
  Covers static/optimized, overlap, aggregated actions, custom rectangles, zero
  finished rotation, orientation lock and absent center. Scratch callbacks record
  state/order; they do not exercise real spatial-section mutations. Coverage is
  bounded, not a universal proof for every mod/parent configuration.
* Automated actual `PlayLayer::scene` loaded **Peaceful 2**: 5,530 scheduler entries,
  8,266,305 object visits and **8,214,713 position visits**. One **450-object GPU
  batch** reached real authoritative state and completed native callbacks.
  Fence wait 1.015 ms; readback 0.667 ms. Later batches used CPU.
* GD's context identified Zink on RX 7600; compute ran in that context, not a preview.
  No separate Vulkan API trace was collected in this run. Evidence is compute
  completion, bitwise results, actual object application and GD's Zink route.
* No new Geode crash report in the successful run. Its exit code 1 is not interpreted
  as proof of success or of a crash; explicit test markers are used.

## Controlled timings

Milliseconds total, **32 actual scheduler calls**, scratch objects/callbacks.
These are not full gameplay frame timings. All visit counts validated.

| Objects/call | Position visits | Native CPU | SIMD | GPU + CPU shadow check |
| ---: | ---: | ---: | ---: | ---: |
| 128 | 4,096 | 0.0737 | 0.0699 | 14.4894 |
| 1,024 | 32,768 | 0.7033 | 0.6643 | 16.4754 |
| 8,192 | 262,144 | 6.6109 | 6.3096 | 35.3916 |

CPU reduction: 4.6-5.5% in this final run. GPU timing includes parity reference
math. No Peaceful FPS improvement or reduction of the historical 297 ms bottleneck
has yet been established. This justifies avoiding per-action readbacks, not
abandoning GPU development.

## Gameplay-start verifier correction

The subsequent manual run at 18:50 used Zink and enabled the automatic setting,
but the verifier disabled replacement at its first check. Geode suppressed nested
hook entry when the check ran inside the real scheduler. The candidate fixture
therefore executed the native routine and failed the replacement-execution counter.
The comparison now explicitly invokes the candidate implementation. Native GD
remains the control, and fixture inputs and mismatches receive named checks.

The corrected package was tested with verification deferred until the actual first
gameplay scheduler call. `Geode 2026-11-15 18.56.11.log` reports
`native_hook_entries=0 replacement_executions=1` and passes all seven CPU/GPU
scenarios. Actual Peaceful 2 then recorded 5,531 scheduler entries, 8,238,007 position
visits and 450 GPU-applied objects with completed callbacks. Fence wait was 0.774 ms
and readback 1.067 ms. This run measures activation and correctness. The controlled
performance table above comes from the earlier run.

## Focused visual check

Launch Experimental Vulkan. Enable **Experimental automatic transform optimization**
and **Experimental Zink session**; keep the dedicated pilot and Frame diagnostics
off. In the launcher enable **Performance run: use uninstrumented Zink**, keeping
CPU profiling and frame-time CSV off. Restart through the Vulkan launcher.
Play the same Peaceful section and report wrong pivots, drifting/missing objects,
stuck triggers or freezing. After visual correctness, compare matched baseline and
optimized gameplay frame times.
