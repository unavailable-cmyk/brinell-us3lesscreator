# Hybrid correctness and fallback cost fix — 2026-11-15

The 00:44:40 Geode capture contains 80 hybrid windows, 9,473 successful
GPU batches, and 52,424,696 movement calculations reported as replaced.
Mean recorded time per accepted batch was 1.162 ms: preparation 0.356 ms,
GPU execution 0.0075 ms, fence waiting 0.2587 ms, transfer 0.1589 ms,
and CPU state application 0.3361 ms. GPU execution overlaps fence waiting;
these fields must not be summed to reconstruct wall time. These numbers
are not a comparison against equivalent vanilla helper calls.

Two defects were found by inspecting the implementation:

- Live move spans all had offset zero. Every object used the first object's
  action-index entry and therefore its X-lock flag. Spans now point to each
  object's own entry. The live and regression builders share that code.
- The shader used the SSBO allocation capacity as its live object count.
  Geometrically grown buffers and dispatch rounding could expose stale spans
  and indices. An explicit live-count uniform now bounds every invocation.
  The executor also validates span and action-index ranges before GL calls.

The fallback path traversed and allocated a copy of each group before checking
the one-batch budget. Budget, small-batch and no-op rejections now happen before
that traversal. This removes work that could never reach a GPU dispatch.

The prior capture stopped frame profiling at 100,048 updateObjectSection calls
in a 28.498 ms frame. A 270k-object level can legitimately exceed the 100k limit
for per-object callbacks. Those phases now use the existing 100m visual-call
limit. Scheduler/helper invocations retain their tighter limit; timing and
counter consistency checks remain enabled.

Before applying GPU results to any live object, the new implementation runs
three same-context GPU regressions (129, 65 and 17 objects) with alternating
X-lock flags and bit-exact CPU expectations, reusing its GPU allocations.
Failures disable offload for the process. These initialization checks are
separate from the gameplay throughput totals. Hybrid log schema is now 6.

The process fuse and one-batch-per-step cap remain. The fixes do not establish
universal trigger equivalence or a speedup. A rebuilt package alone does not
prove the shader regression passed; the next in-game log must report its result.
Rotation was not offloaded in the previous capture, so that run provides no
rotation correctness or performance evidence.
