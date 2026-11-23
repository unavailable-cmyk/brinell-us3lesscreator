"""Require exact CPU/GPU output equality plus same-process Zink Vulkan dispatch evidence."""
import argparse
import json
import math
import re
from pathlib import Path


PASS = re.compile(
    r"^TRIGGER_GPU_POC_PASS pid=(\d+) hwnd=(0x[0-9a-fA-F]+) "
    r"renderer=(.+?) version=(.+?) objects=(\d+) actions=(\d+) "
    r"object_events=(\d+) exact_matches=(\d+) order_probe=\(1,2\)$",
    re.MULTILINE,
)
SCALE_PASS = re.compile(r"^TRIGGER_GPU_SCALE_PASS (.+)$", re.MULTILINE)


def parse_scale_rows(probe_log: str, require_complete: bool = True) -> list[dict[str, str]]:
    rows = []
    for line in SCALE_PASS.findall(probe_log):
        row = dict(token.split("=", 1) for token in line.split() if "=" in token)
        rows.append(row)
    expected = {
        (case, size, sync, buffers)
        for case in ("move", "rotation_pivot", "ordered_move_rotation")
        for size in (1, 100, 1000, 10000)
        for sync, buffers in (("explicit_fence", "reused"),
                              ("implicit_readback", "reused"),
                              ("explicit_fence", "reallocated"))
    }
    actual = set()
    for row in rows:
        key = (row.get("case"), int(row.get("objects", "-1")),
               row.get("sync"), row.get("buffers"))
        if key in actual:
            raise ValueError(f"Duplicate scaling benchmark row: {key}")
        actual.add(key)
        size = key[1]
        multiplier = 4 if key[0] == "ordered_move_rotation" else 1
        if int(row.get("action_entries", "-1")) != size * multiplier:
            raise ValueError(f"Object/action entry count is wrong for {key}")
        if int(row.get("cpu_action_evaluations_per_sample", "0")) < 100000:
            raise ValueError(f"CPU timing window is too short for {key}")
        if row.get("exact_parity") != "1" or row.get("batches_per_iteration") != "1" or row.get("readbacks_per_iteration") != "1":
            raise ValueError(f"Scaling row did not pass exact parity or one-batch accounting: {key}")
        for field in ("cpu_math_ms", "cpu_state_apply_callback_proxy_ms", "cpu_total_ms",
                      "gpu_prepare_ms", "gpu_upload_ms", "gpu_dispatch_submit_ms", "gpu_compute_ms",
                      "gpu_fence_wait_ms", "gpu_readback_ms", "gpu_state_apply_callback_proxy_ms",
                      "hybrid_total_ms", "buffer_allocations_avg"):
            value = float(row[field])
            if not math.isfinite(value) or value < 0:
                raise ValueError(f"Non-finite or negative {field} in {key}")
        if row.get("callback_note") != "synthetic_one_per_object;not_Cocos_callbacks":
            raise ValueError(f"Callback comparison was not clearly labeled for {key}")
        if row.get("measured_repeats") != "3":
            raise ValueError(f"Unexpected repeat count for {key}")
        if row.get("gpu_timing_summary") != "median_of_3;compute_overlaps_fence_wait":
            raise ValueError(f"GPU timing was not summarized as a median for {key}")
        expected_readback_timing = "post_fence_transfer" if key[2] == "explicit_fence" else "transfer_plus_implicit_wait"
        if row.get("readback_timing") != expected_readback_timing:
            raise ValueError(f"Readback synchronization was not labeled for {key}")
    if require_complete and actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise ValueError(f"Expected all 36 scale cases; missing={missing[:3]} extra={extra[:3]}")
    if not actual:
        raise ValueError("No scaling benchmark rows were recorded")
    return rows


def verify(probe_log: str, events: list[dict], require_complete: bool = True) -> dict:
    scale_rows = parse_scale_rows(probe_log, require_complete=require_complete)
    matches = PASS.findall(probe_log)
    if len(matches) != 1:
        raise ValueError("Expected one successful GPU transform comparison")
    pid, hwnd_text, renderer, version, objects, actions, object_events, exact = matches[0]
    pid = int(pid)
    hwnd = int(hwnd_text, 16)
    objects, actions, object_events, exact = map(int, (objects, actions, object_events, exact))
    if "zink" not in renderer.lower():
        raise ValueError("Compute context was not reported by Zink")
    if objects < 1 or actions < 1 or object_events < 1 or exact != objects:
        raise ValueError("CPU/GPU result counts are incomplete")

    matching = [event for event in events
                if event.get("source") == "mesa-26.2.6-zink"
                and event.get("pid") == pid
                and int(event.get("hwnd", "0"), 16) == hwnd]
    if not any(event.get("event") == "vkCreateWin32SurfaceKHR" and event.get("result") == 0
               for event in matching):
        raise ValueError("No successful Zink Vulkan surface matches the probe HWND")
    presents = [event for event in matching if event.get("event") == "vkQueuePresentKHR"
                and event.get("result") in (0, 1000001003)]
    if not presents:
        raise ValueError("No successful Vulkan present matches the probe HWND")
    evidence = max(presents, key=lambda event: event.get("compute_dispatch_calls", 0))
    expected_dispatches = 1 + sum(1 + int(row["measured_repeats"]) for row in scale_rows)
    if evidence.get("compute_dispatch_calls", 0) < expected_dispatches:
        raise ValueError(
            "Vulkan evidence did not cover the scaling rows: "
            f"observed {evidence.get('compute_dispatch_calls', 0)} vkCmdDispatch calls, "
            f"expected at least {expected_dispatches} (one PoC plus each scaling warm-up/repeat)"
        )
    if evidence.get("present_errors", 0):
        raise ValueError("The probe recorded Vulkan presentation errors")
    return {
        "status": "ZINK_TRIGGER_GPU_POC_CONFIRMED",
        "pid": pid,
        "hwnd": hwnd_text,
        "renderer": renderer,
        "opengl": version,
        "objects": objects,
        "ordered_actions": actions,
        "object_action_entries": object_events,
        "exact_cpu_gpu_matches": exact,
        "scale_benchmark_rows": len(scale_rows),
        "hybrid_faster_than_cpu_rows": sum(
            float(row["hybrid_total_ms"]) < float(row["cpu_total_ms"]) for row in scale_rows
        ),
        "vkCmdDispatch_calls": evidence["compute_dispatch_calls"],
        "successful_vkQueuePresentKHR": evidence["present_calls"],
        "scope": "isolated ordered move/rotate kernel; not GD integration",
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe-log", required=True, type=Path)
    parser.add_argument("--mesa-events", required=True, type=Path)
    parser.add_argument("--allow-partial", action="store_true",
                        help="Accept one or more explicitly filtered scaling cases")
    args = parser.parse_args()
    try:
        log = args.probe_log.read_text(encoding="utf-8-sig")
        events = [json.loads(line) for line in args.mesa_events.read_text(encoding="utf-8-sig").splitlines()
                  if line.strip()]
        print(json.dumps(verify(log, events, require_complete=not args.allow_partial), indent=2))
        return 0
    except (ValueError, KeyError, OSError, TypeError) as error:
        print(json.dumps({"status": "NOT_CONFIRMED", "reason": str(error)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
