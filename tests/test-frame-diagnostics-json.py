"""Validate the exact diagnostic JSON emitted by FrameProfiler."""
import json
import subprocess
import sys


def main():
    result = subprocess.run(
        [sys.argv[1], "--json-fixture"],
        check=True,
        capture_output=True,
        text=True,
    )
    line = next(
        item for item in result.stdout.splitlines()
        if item.startswith("FRAME_JSON_FIXTURE ")
    )
    data = json.loads(line.removeprefix("FRAME_JSON_FIXTURE "))

    assert data["schema"] == 6
    assert data["profiler_abi"] == 7
    assert data["frame_interval"] == "drawScene_end_to_drawScene_end"
    assert data["valid"] is True
    assert data["frames"] == data["target_frames"] == 16
    assert data["partial"] is False
    assert data["scene"].startswith("fixture")
    assert "\n" in data["scene"]
    assert data["measurement"]["clock_pairs"] == 160
    assert data["measurement"]["estimated_clock_overhead_ms"] == 0.016
    assert data["cpu_work_ms"]["draw_scene_inclusive_mean"] == 50.0
    assert data["cpu_work_ms"]["draw_scene_outside_cocos_swap_mean"] == 49.9
    assert data["swap_ms"]["cocos_mean"] == 0.1
    assert data["sample_counts"]["cocos_draw_samples"] == 0

    callbacks = {callback["id"]: callback for callback in data["callbacks"]}
    scheduler = callbacks["CCScheduler::update"]
    assert scheduler["calls"] == scheduler["timed_calls"] == 16
    assert scheduler["inclusive_total_ms"] == 48.0
    assert scheduler["exclusive_total_ms"] == 8.0
    assert scheduler["max_inclusive_ms"] == 3.0
    assert scheduler["max_exclusive_ms"] == 0.5
    assert scheduler["work_items"] == 0
    assert scheduler["work_unit"] == "none"

    move = callbacks["GJBaseGameLayer::processMoveActions"]
    assert move["calls"] == 1024
    assert move["timed_calls"] == 16
    assert move["estimate_weight"] == 64
    assert move["inclusive_total_ms"] == 32.0
    assert move["exclusive_total_ms"] == 24.0
    assert move["work_items"] == 16384
    assert move["work_unit"] == "move_action_entries"

    timer = callbacks["CCTimer::update"]
    assert timer["calls"] == 128
    assert timer["timed_calls"] == 16
    assert timer["estimate_weight"] == 8
    assert timer["max_is_sampled"] is True
    assert data["render_phases"]["node_visit"]["calls_per_frame"] == 1.0
    print("frame diagnostics JSON schema and escaped strings passed")


if __name__ == "__main__":
    main()
