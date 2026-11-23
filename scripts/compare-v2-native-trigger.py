"""Compare paired GD captures; a nonzero exit means the images differ."""
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
base = json.loads((root / 'baseline.json').read_text(encoding='utf-8-sig'))
shared = json.loads((root / 'shared.json').read_text(encoding='utf-8-sig'))
a, b = base['capture'], shared['capture']
fields = ('ticks', 'level_time', 'player_x', 'player_y', 'width', 'height',
          'roi_left', 'roi_top', 'roi_right', 'roi_bottom')
if any(a[field] != b[field] for field in fields):
    raise SystemExit('capture checkpoints differ')
left = (root / 'baseline.ppm').read_bytes().split(b'\n', 3)
right = (root / 'shared.ppm').read_bytes().split(b'\n', 3)
if left[:3] != right[:3] or left[0] != b'P6' or left[2] != b'255':
    raise SystemExit('unsupported or mismatched PPMs')
width, height = map(int, left[1].split())
if len(left[3]) != width * height * 3 or len(right[3]) != width * height * 3:
    raise SystemExit('truncated PPM')
x0, y0, x1, y1 = (b[k] for k in ('roi_left', 'roi_top', 'roi_right', 'roi_bottom'))
differences = [abs(left[3][(y * width + x) * 3 + c] - right[3][(y * width + x) * 3 + c])
               for y in range(y0, y1) for x in range(x0, x1) for c in range(3)]
native_positions = {v['id']: v for v in a['fixture_positions']}
v2_positions = {v['id']: v for v in b['fixture_positions']}
if native_positions.keys() != v2_positions.keys():
    raise SystemExit('fixture objects differ')
position_error = max((abs(native_positions[i][axis] - v2_positions[i][axis])
                      for i in native_positions for axis in ('x', 'y')), default=0)
native_objects = [i for i in native_positions if not v2_positions[i]['owned']]
quad_error = max((abs(native_positions[i][axis] - v2_positions[i][axis])
                  for i in native_objects for axis in ('quad_bl_x', 'quad_bl_y', 'quad_tr_x', 'quad_tr_y')), default=0)
atlas_changes = sum(native_positions[i]['atlas_index'] != v2_positions[i]['atlas_index'] for i in native_objects)
visibility_changes = sum(native_positions[i]['visible'] != v2_positions[i]['visible'] for i in native_objects)
atlas_quad_error = max((abs(native_positions[i][axis] - v2_positions[i][axis])
                        for i in native_objects for axis in ('atlas_bl_x', 'atlas_bl_y', 'atlas_tr_x', 'atlas_tr_y')
                        if axis in native_positions[i] and axis in v2_positions[i]), default=0)
report = {'baseline_capture': a, 'shared_capture': b,
          'changed_channels': sum(d != 0 for d in differences),
          'max_channel_delta': max(differences, default=0),
          'max_native_position_difference': position_error,
          'handed_off_quad_max_difference': quad_error,
          'handed_off_atlas_index_changes': atlas_changes,
          'handed_off_visibility_changes': visibility_changes,
          'handed_off_atlas_quad_max_difference': atlas_quad_error,
          'reference_error': shared['max_reference_position_error']}
(root / 'parity.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
if report['changed_channels'] or position_error:
    raise SystemExit('visual parity failed')
