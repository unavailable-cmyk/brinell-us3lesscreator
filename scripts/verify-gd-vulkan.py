"""Correlate real GD context attestation with instrumented Mesa Vulkan calls.

No screenshot, renderer string alone, or standalone preview can pass this check.
"""
import argparse
import json
import re
from pathlib import Path


def verify_vulkan_events(events: list[dict], pid: int, hwnd: int) -> dict:
    matching = [event for event in events if event.get("schema") in (1, 2)
                and event.get("source") == "mesa-26.2.6-zink"
                and event.get("pid") == pid
                and int(event.get("hwnd", "0"), 16) == hwnd]
    if not any(event.get("event") == "vkCreateWin32SurfaceKHR" and event.get("result") == 0 for event in matching):
        raise ValueError("No successful Vulkan surface creation matches the process and original HWND")
    presents = [event for event in matching if event.get("event") == "vkQueuePresentKHR"]
    if any(event.get("present_errors", 0) > 0 or event.get("result") not in (0, 1000001003) for event in presents):
        raise ValueError("Vulkan presentation errors were recorded")
    usable = [event for event in presents if event.get("draw_calls", 0) > 0 and event.get("present_calls", 0) >= 120]
    if not usable:
        raise ValueError("Need Vulkan draw dispatches and at least 120 presents to the original HWND")
    return max(usable, key=lambda value: value["present_calls"])


def verify_wgl_events(events: list[dict], pid: int, hwnd: int) -> dict:
    matching = [event for event in events if event.get("schema") == 1
                and event.get("source") == "mesa-26.2.6-wgl"
                and event.get("event") == "wglSwapBuffers"
                and event.get("pid") == pid
                and int(event.get("hwnd", "0"), 16) == hwnd]
    if not matching:
        raise ValueError("No WGL swap timings match the Geometry Dash process and HWND")
    if any(event.get("failures", 0) for event in matching):
        raise ValueError("Mesa WGL reported failed swap calls")
    last = matching[-1]
    return {key: last.get(key) for key in (
        "swap_calls", "total_ms", "total_max_ms", "flush_ms", "flush_max_ms",
        "interval_wait_ms", "interval_wait_max_ms", "present_stage_ms", "present_stage_max_ms")}


def verify(geode_log: str, events: list[dict], expected_pid: int | None = None,
           wgl_events: list[dict] | None = None) -> dict:
    routes = []
    for line in geode_log.splitlines():
        if "[Vulkanizer]: GD_RENDER_ROUTE " in line:
            route = json.loads(line.split("GD_RENDER_ROUTE ", 1)[1])
            if expected_pid is None or route.get("pid") == expected_pid:
                routes.append(route)
    if len(routes) != 1:
        raise ValueError("Expected exactly one GD render-context attestation for this process")
    route = routes[0]
    if not route.get("zink") or "zink" not in route.get("renderer", "").lower() or route.get("preview") is not False:
        raise ValueError("GD's own context is not attested as Zink without a preview")
    hwnd = int(route["hwnd"], 16)
    if not hwnd:
        raise ValueError("GD window handle is missing")
    last = verify_vulkan_events(events, route["pid"], hwnd)
    result = {"status": "GD_GL_TO_VULKAN_CONFIRMED", "pid": route["pid"], "hwnd": route["hwnd"],
            "renderer": route["renderer"], "opengl32": route["opengl32"],
            "vulkan_draw_dispatches": last["draw_calls"], "vulkan_presents": last["present_calls"],
            "visual_correctness": "requires human confirmation", "gpu_timing": "not measured"}
    if last.get("schema") == 2:
        result["mesa_cpu_timing_window"] = {
            "presents": last.get("window_present_calls"),
            "vkQueuePresentKHR_total_ms": last.get("window_present_total_ms"),
            "vkQueuePresentKHR_max_ms": last.get("window_present_max_ms"),
            "zink_batch_vkQueueSubmit_calls": last.get("window_submit_calls"),
            "zink_batch_vkQueueSubmit_total_ms": last.get("window_submit_total_ms"),
            "zink_batch_vkQueueSubmit_max_ms": last.get("window_submit_max_ms"),
            "zink_flush_calls": last.get("window_flush_calls"),
            "zink_flush_total_ms": last.get("window_flush_total_ms"),
            "zink_flush_max_ms": last.get("window_flush_max_ms"),
            "vkAcquireNextImageKHR_calls": last.get("window_acquire_calls"),
            "vkAcquireNextImageKHR_total_ms": last.get("window_acquire_total_ms"),
            "vkAcquireNextImageKHR_max_ms": last.get("window_acquire_max_ms"),
            "note": "CPU-side API-call durations; not GPU execution time or the full WGL swapBuffers duration",
        }
    if wgl_events is not None:
        result["mesa_wgl_timing_window"] = verify_wgl_events(wgl_events, route["pid"], hwnd)
    return result


def verify_wgl_probe(probe_log: str, events: list[dict]) -> dict:
    pattern = (r"^WGL_API_PROBE pid=(\d+) hwnd=(0x[0-9a-fA-F]+) frames=(\d+) "
               r"renderer=(.+?) readback=(\d+),(\d+),(\d+) GL_error=0$")
    matches = re.findall(pattern, probe_log, flags=re.MULTILINE)
    if len(matches) != 1:
        raise ValueError("Expected one successful hidden WGL API check")
    pid, window, frames, renderer, red, green, blue = matches[0]
    if "zink" not in renderer.lower() or int(frames) < 120:
        raise ValueError("WGL API check did not complete 120 Zink frames")
    if any(abs(value - expected) > 2 for value, expected in zip(map(int, (red, green, blue)), (230, 40, 80))):
        raise ValueError("WGL texture readback mismatch")
    last = verify_vulkan_events(events, int(pid), int(window, 16))
    return {"status": "WGL_ZINK_API_CHECK_PASSED", "pid": int(pid), "hwnd": window,
            "renderer": renderer, "vulkan_draw_dispatches": last["draw_calls"],
            "vulkan_presents": last["present_calls"], "texture_readback": [int(red), int(green), int(blue)],
            "gd_rendering_status": "requires GD session evidence"}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    logs = parser.add_mutually_exclusive_group(required=True)
    logs.add_argument("--geode-log", type=Path)
    logs.add_argument("--wgl-probe-log", type=Path)
    parser.add_argument("--mesa-events", required=True, type=Path)
    parser.add_argument("--pid", type=int)
    parser.add_argument("--wgl-events", type=Path)
    args = parser.parse_args()
    try:
        events = [json.loads(line) for line in args.mesa_events.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
        if args.geode_log:
            wgl_events = None
            if args.wgl_events:
                wgl_events = [json.loads(line) for line in args.wgl_events.read_text(encoding="utf-8-sig").splitlines() if line.strip()]
            result = verify(args.geode_log.read_text(encoding="utf-8-sig"), events, args.pid, wgl_events)
        else:
            result = verify_wgl_probe(args.wgl_probe_log.read_text(encoding="utf-8-sig"), events)
        print(json.dumps(result, indent=2))
    except (ValueError, KeyError, OSError, TypeError) as error:
        print(json.dumps({"status": "NOT_CONFIRMED", "reason": str(error)}))
        raise SystemExit(1)
