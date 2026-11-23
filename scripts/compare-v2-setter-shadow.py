"""Compare two *aligned* setter-shadow runs. Raw integers are IEEE bit patterns.

Exit 0: coverage and correctness gates passed; 1: mismatch/invariant failure;
2: insufficient alignment/coverage. No floats or epsilon in the comparison.
"""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path


IDENTITY = ('initial_x_f64', 'initial_y_f64', 'initial_group_hash', 'object_kind', 'control')
STATE = ('level_time_f64', 'player_x_f64', 'player_y_f64', 'step_signature', 'state_signature')
FIELDS = ('logical_x_f64', 'logical_y_f64', 'visibility', 'outer', 'middle',
          'rotation_x_f32', 'rotation_y_f32', 'group_hash')
SPRITE = ('sprite_x_f32', 'sprite_y_f32')
REQUIRED = set(IDENTITY + STATE + FIELDS + SPRITE + (
    'source_index', 'step', 'pass_ordinal', 'owned', 'retention_count', 'first_retention_step'))


def visible(row):
    return bool(int(row['visibility']) & 1)


def differing_fields(native, retained):
    fields = FIELDS + (SPRITE if visible(native) and visible(retained) else ())
    return [field for field in fields if native[field] != retained[field]]


def load(path):
    with path.open(encoding='utf-8-sig') as f:
        summary = json.load(f)
    rows = {}
    sample_path = path.with_suffix('.shadow-samples.tsv')
    with sample_path.open(encoding='utf-8-sig', newline='') as f:
        reader = csv.DictReader(f, delimiter='\t')
        if not REQUIRED.issubset(reader.fieldnames or ()):
            raise ValueError(f'{sample_path}: missing aligned-schema fields')
        for raw in reader:
            row = {field: int(raw[field]) for field in REQUIRED}
            key = (row['step'], row['pass_ordinal'], row['source_index'])
            if key in rows:
                raise ValueError(f'{sample_path}: duplicate checkpoint/object {key}')
            rows[key] = row
    if len(rows) != summary['setter_shadow']['sample_rows']:
        raise ValueError(f'{sample_path}: row count differs from summary')
    return summary, rows


def compare(ns, native, rs, retained):
    n, r = ns['setter_shadow'], rs['setter_shadow']
    errors, gaps = [], []
    for label, summary, shadow in [('native', ns, n), ('retained', rs, r)]:
        if not summary['test_completed'] or not shadow['aligned_mode'] or not shadow['aligned_clock_frames']:
            gaps.append(f'{label}: missing completed aligned run')
        for field in ('setter_reference_mismatches', 'visibility_reference_mismatches', 'postcondition_failures'):
            if shadow[field]:
                errors.append(f'{label}: {field}={shadow[field]}')
        if summary['resident_gpu']['exactness_violations'] or summary['v2_native_commit_violations'] or summary['render_failures']:
            errors.append(f'{label}: GPU/render invariant failure')
        if not summary['resident_gpu']['validation_checks']:
            gaps.append(f'{label}: GPU validation not exercised')
        if shadow['disabled_after_failure']:
            errors.append(f'{label}: shadow disabled after failure')
        hyper = summary.get('hyper_aggressive')
        if hyper:
            for field in ('unmodeled_attempts', 'emergency_released', 'setter_failures', 'pivotless_failures', 'angle_failures'):
                if hyper[field]:
                    errors.append(f'{label}: hyper {field}={hyper[field]}')
            if not hyper['angle_checks'] or (label == 'retained' and not hyper['pivotless_checks']):
                gaps.append(f'{label}: hyper angle/pivotless validation not exercised')
            if 'pivotless_commands' in hyper:
                if hyper['pivotless_abort'] or hyper['pivotless_section_events']:
                    errors.append(f'{label}: angle-only replay aborted or produced section events')
                if label == 'retained' and (not hyper['pivotless_commands'] or not hyper['pivotless_probe_passed']):
                    gaps.append(f'{label}: batched angle-only rotation not exercised')
            if hyper['negative_requested']:
                gaps.append(f'{label}: planted-error run is not a correctness reference')
    if not n['native_reference'] or r['native_reference'] or n['group'] != r['group'] or ns['level'] != rs['level']:
        gaps.append('Reference/candidate group or level differs')
    if bool(ns.get('hyper_aggressive')) != bool(rs.get('hyper_aggressive')):
        gaps.append('Hyper mode differs between reference and candidate')
    if n['target_population'] != r['target_population']:
        gaps.append('Target populations differ')
    if n['target_owned_at_completion'] or n['retained_calls']:
        errors.append('Native summary reports target ownership or retention')
    if r['target_owned_at_completion'] != r['target_population'] or r['retained_objects_still_owned'] != r['unique_retained_objects']:
        errors.append('Retained group lost ownership')
    if not r['retained_calls'] or not r['later_step_calls']:
        gaps.append('Retained mode did not exercise later-step retention')
    for field in ('aligned_clock_frames', 'aligned_final_step', 'aligned_final_step_signature'):
        if n[field] != r[field]:
            gaps.append(f'Run schedule differs: {field}')

    common = native.keys() & retained.keys()
    missing_native = retained.keys() - native.keys()
    missing_retained = native.keys() - retained.keys()
    counts = Counter()
    mismatch_fields = Counter()
    first_failure = first_unmatched = None
    post_objects, repeat_objects, control_objects, boundary_objects = set(), set(), set(), set()
    retention_history, last_visibility = {}, {}
    hidden_difference_objects, hidden_later_visible = set(), set()
    negative_control = None
    for key in sorted(common):
        a, b = native[key], retained[key]
        if a['owned'] or a['retention_count']:
            counts['native_owned_or_retained_rows'] += 1
        if b['control'] and (b['owned'] or b['retention_count']):
            counts['retained_control_owned_rows'] += 1
        mismatched_state = [f for f in IDENTITY + STATE if a[f] != b[f]]
        if mismatched_state:
            counts['unmatched_state_rows'] += 1
            if first_unmatched is None:
                first_unmatched = {'checkpoint': key, 'fields': mismatched_state,
                                   'native': {f: a[f] for f in mismatched_state},
                                   'retained': {f: b[f] for f in mismatched_state}}
            continue
        counts['matched_rows'] += 1
        count, first = b['retention_count'], b['first_retention_step']
        obj = key[2]
        previous_count, previous_first = retention_history.get(obj, (0, first))
        if count < previous_count or (previous_count and first != previous_first) or (count and first > key[0]):
            counts['invalid_retention_history_rows'] += 1
        retention_history[obj] = (count, first)
        post = not b['control'] and count > 0 and key[0] >= first
        if b['control']:
            control_objects.add(obj)
            counts['control_rows'] += 1
        if post:
            post_objects.add(obj)
            counts['post_retention_rows'] += 1
            if key[0] > first:
                counts['later_step_rows'] += 1
            if not b['owned']:
                counts['post_retention_unowned_rows'] += 1
            if count > 1:
                repeat_objects.add(obj)
                counts['repeat_retention_rows'] += 1
            if visible(a) and visible(b):
                counts['post_retention_visible_rows'] += 1
                if negative_control is None:
                    negative_control = (a, b)
            # Actual native culling/visibility transitions, not an inferred screen edge.
            if obj in last_visibility and last_visibility[obj] != visible(a):
                counts['post_retention_visibility_flips'] += 1
                boundary_objects.add(obj)
        last_visibility[obj] = visible(a)
        if not visible(a) and not visible(b) and any(a[f] != b[f] for f in SPRITE):
            counts['hidden_sprite_difference_rows'] += 1
            if post:
                hidden_difference_objects.add(obj)
        if post and obj in hidden_difference_objects and visible(a) and visible(b):
            hidden_later_visible.add(obj)
        different = differing_fields(a, b)
        if different:
            counts['failed_rows'] += 1
            if post:
                counts['post_retention_failed_rows'] += 1
            mismatch_fields.update(different)
            if first_failure is None:
                first_failure = {'checkpoint': key, 'post_retention': post, 'fields': different,
                                 'native': {f: a[f] for f in different}, 'retained': {f: b[f] for f in different}}
        else:
            counts['passed_rows'] += 1

    counts['missing_native_rows'] = len(missing_native)
    counts['missing_retained_rows'] = len(missing_retained)
    counts['post_retention_objects'] = len(post_objects)
    counts['repeat_retention_objects'] = len(repeat_objects)
    counts['control_objects'] = len(control_objects)
    counts['boundary_objects'] = len(boundary_objects)
    counts['hidden_sprite_difference_post_objects'] = len(hidden_difference_objects)
    counts['hidden_difference_later_visible_objects'] = len(hidden_later_visible)
    counts['hidden_difference_never_later_visible_objects'] = len(hidden_difference_objects - hidden_later_visible)
    if any(counts[k] for k in ('unmatched_state_rows', 'missing_native_rows', 'missing_retained_rows')):
        gaps.append('Checkpoint/identity/gameplay-state alignment is incomplete')
    for k in ('failed_rows', 'native_owned_or_retained_rows', 'retained_control_owned_rows',
              'invalid_retention_history_rows', 'post_retention_unowned_rows'):
        if counts[k]:
            errors.append(f'{k}={counts[k]}')
    # Require meaningful retained-period, repeated-call, visible and culling-boundary coverage.
    minimums = {'post_retention_rows': 1000, 'later_step_rows': 500, 'post_retention_objects': 32,
                'repeat_retention_objects': 16, 'post_retention_visible_rows': 200,
                'control_objects': 16, 'boundary_objects': 8}
    for k, minimum in minimums.items():
        if counts[k] < minimum:
            gaps.append(f'Insufficient {k}: {counts[k]} < {minimum}')

    # Negative control injects a single raw-bit error into a copy of a real,
    # visible retained sample. Test the SAME predicate; never mutate gameplay.
    negative = {'sprite': False, 'visibility': False}
    if negative_control:
        _, b = negative_control
        clean = dict(b)
        for field, name in [('sprite_x_f32', 'sprite'), ('visibility', 'visibility')]:
            perturbed = dict(clean)
            perturbed[field] ^= 1
            negative[name] = not differing_fields(b, clean) and field in differing_fields(b, perturbed)
        if not all(negative.values()):
            errors.append('Planted-error negative control failed')
    else:
        gaps.append('No visible post-retention sample for negative control')
    total = counts['matched_rows'] or 1
    return {'verdict': 'FAIL' if errors else 'UNMATCHED_OR_INSUFFICIENT' if gaps else 'PASS',
            'counts': dict(counts), 'post_retention_share': counts['post_retention_rows'] / total,
            'repeat_retention_share_of_post': counts['repeat_retention_rows'] / (counts['post_retention_rows'] or 1),
            'mismatch_fields': dict(mismatch_fields), 'first_failure': first_failure,
            'first_unmatched': first_unmatched,
            'first_missing_native': min(missing_native, default=None),
            'first_missing_retained': min(missing_retained, default=None),
            'negative_control': negative, 'minimums': minimums, 'errors': errors, 'gaps': gaps,
            'scope': 'Post-native-visibility raw bits; sprite compared when visible in both; hidden sprite differences counted separately; target-group native reference (not full-engine vanilla).'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('native', type=Path, help='native aligned summary .json (with adjacent shadow-samples.tsv)')
    parser.add_argument('retained', type=Path, help='retained aligned summary .json')
    parser.add_argument('--output', type=Path, help='write JSON verdict to this path')
    args = parser.parse_args()
    try:
        ns, native = load(args.native)
        rs, retained = load(args.retained)
        result = compare(ns, native, rs, retained)
    except (OSError, KeyError, ValueError, TypeError) as error:
        result = {'verdict': 'UNMATCHED_OR_INSUFFICIENT', 'gaps': [str(error)]}
    rendered = json.dumps(result, indent=2)
    print(rendered)
    if args.output:
        args.output.write_text(rendered + '\n', encoding='utf-8')
    return {'PASS': 0, 'FAIL': 1, 'UNMATCHED_OR_INSUFFICIENT': 2}[result['verdict']]


if __name__ == '__main__':
    raise SystemExit(main())
