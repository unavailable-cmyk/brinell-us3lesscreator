"""Compare fixed-clock eager and deferred visibility audits (not vanilla)."""
import argparse
import json
from pathlib import Path


def load(path, enabled):
    report = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    f = report["visibility_filter"]
    bad = ("ownership_revocations", "rebase_rejections", "render_failures",
           "emergency_releases", "unmodeled_attempts", "pivotless_abort")
    if not (report["test_completed"] and report["fixed_clock_frames"] and
            f["audit"] and f["enabled"] == enabled and f["trace"] and
            not f["tracking_lost"] and not f["failed"] and not f["audit_failures"] and
            not f["late_visible"] and not f["negative_requested"] and
            not any(report.get(k) for k in bad)):
        raise ValueError(f"Incomplete or failed audit: {path}; first miss {f.get('first_miss')}")
    if enabled and not (f["deferred_visits"] and f["resumed_at_visibility"] and f["audit_checkpoint_objects"]):
        raise ValueError(f"No deferred/resumed coverage: {path}")
    return report, f


def compare(eager, filtered):
    a, af = load(eager, False)
    b, bf = load(filtered, True)
    if af.get("eligibility_revision") != 2 or bf.get("eligibility_revision") != 2:
        raise ValueError("Capture both audits with the headless/reclassification patch (eligibility revision 2)")
    for field in ("level", "simulation_steps", "fixed_clock_frames", "fixed_input_dt_s",
                  "retained_at_test_completion", "peak_owned", "pivotless_commands",
                  "eliminated_move_visits", "gpu_rotation_recipients"):
        if a[field] != b[field]:
            raise ValueError(f"Run alignment mismatch {field}: eager={a[field]} filtered={b[field]}")
    if af["trace_columns"] != bf["trace_columns"]:
        raise ValueError("Trace schemas differ")
    fields = af["trace_columns"].split(",")
    rows_a, rows_b = af["trace"], bf["trace"]
    if len(rows_a) != len(rows_b):
        raise ValueError(f"Visibility pass count differs: {len(rows_a)} vs {len(rows_b)}")
    for i, (x, y) in enumerate(zip(rows_a, rows_b)):
        if len(x) != len(fields) or len(y) != len(fields):
            raise ValueError(f"Invalid trace row {i}")
        if x != y:
            differences = {name: {"eager": u, "filtered": v}
                           for name, u, v in zip(fields, x, y) if u != v}
            raise ValueError(f"First mismatch: pass {i}, step {x[0]}, command {x[1]}: {differences}")
    old = a["mirror_worklist"]["materializations"]
    new = b["mirror_worklist"]["materializations"]
    print(f"PASS: {len(rows_a)} aligned visibility passes; identical visible-set, relevant logical/history and visible sprite hashes")
    print(f"Materializations: {old:,} -> {new:,} ({old-new:,} fewer; {100*(old-new)/max(1,old):.2f}% reduction)")
    print(f"Deferred visits: {bf['deferred_visits']:,}; resumed at visibility: {bf['resumed_at_visibility']:,}; camera candidates: {bf['camera_candidates']:,}; replay checks: {bf['audit_checkpoint_objects']:,}; retained: {b['retained_at_test_completion']:,}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("eager", type=Path)
    parser.add_argument("filtered", type=Path)
    args = parser.parse_args()
    try:
        compare(args.eager, args.filtered)
    except (ValueError, KeyError, OSError) as error:
        raise SystemExit(f"FAIL: {error}")
