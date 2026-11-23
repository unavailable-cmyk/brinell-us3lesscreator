"""Validate and summarize overlapping live workload-census counters."""
import json
import pathlib
import sys

report = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
census = report['eligibility_census']
total_objects = report['total']
total_visits = census['covered_native_move_visits']
assert total_visits <= census['native_move_visits_total']
assert report['peak_owned'] <= report['eligible'] <= total_objects
for name, row in census['protection_causes_overlapping'].items():
    if not 0 <= row['objects'] <= total_objects or not 0 <= row['observed_move_visits'] <= total_visits:
        raise SystemExit(f'Out-of-range protection cause: {name}')
previous = {'objects': 0, 'observed_move_visits': 0}
for name in ('ignore_unresolved_veto_and_initial_readiness', 'plus_static_transforms',
             'plus_history_enter_fade', 'plus_protected_groups'):
    row = census['cumulative_hypothetical_screens'][name]
    if not (previous['objects'] <= row['objects'] <= total_objects and
            previous['observed_move_visits'] <= row['observed_move_visits'] <= total_visits):
        raise SystemExit(f'Nonmonotonic or out-of-range cumulative screen: {name}')
    marginal = row['observed_move_visits'] - previous['observed_move_visits']
    print(f"{name}: {row['objects']} objects, {row['observed_move_visits']} visits, "
          f"{marginal} additional visits in this hypothetical sequence")
    previous = row
print(f'Observed native visits: {total_visits}; actual peak ownership: {report["peak_owned"]}; '
      f'actual eliminated visits: {report["eliminated_native_move_member_visits"]}')
print('PASS: overlapping causes bounded and cumulative hypothetical screens monotonic')
