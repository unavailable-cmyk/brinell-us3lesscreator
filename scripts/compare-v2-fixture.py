"""Compare same-tick GD fixture captures and native handoff state."""
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
baseline = json.loads((root / 'baseline.json').read_text(encoding='utf-8-sig'))
shared = json.loads((root / 'shared.json').read_text(encoding='utf-8-sig'))
native_capture, v2_capture = baseline['capture'], shared['capture']
for field in ('ticks', 'level_time', 'player_x', 'player_y', 'width', 'height',
              'roi_left', 'roi_top', 'roi_right', 'roi_bottom'):
    if native_capture[field] != v2_capture[field]:
        raise SystemExit(f'capture field differs: {field}')
if not shared['structural_handoff_fixture']['all_were_v2_owned']:
    raise SystemExit('handoff did not start with V2 ownership')
same_state = (baseline['structural_handoff_fixture']['objects'] ==
              shared['structural_handoff_fixture']['objects'])
if not same_state or len(shared['structural_handoff_fixture']['objects']) != 4:
    raise SystemExit('four native handoff states did not match')
a = (root / 'baseline.ppm').read_bytes().split(b'\n', 3)
b = (root / 'shared.ppm').read_bytes().split(b'\n', 3)
if a[:3] != b[:3] or a[0] != b'P6' or a[2] != b'255':
    raise SystemExit('unsupported or mismatched image headers')
width, height = map(int, a[1].split())
if len(a[3]) != width * height * 3 or len(b[3]) != width * height * 3:
    raise SystemExit('truncated image')
x0, y0, x1, y1 = (v2_capture[key] for key in ('roi_left', 'roi_top', 'roi_right', 'roi_bottom'))
differences = [abs(a[3][(y * width + x) * 3 + k] - b[3][(y * width + x) * 3 + k])
               for y in range(y0, y1) for x in range(x0, x1) for k in range(3)]
result = {'capture_baseline': native_capture, 'capture_shared': v2_capture,
          'changed_channels': sum(value != 0 for value in differences),
          'max_channel_difference': max(differences, default=0),
          'handoff_state_equal': same_state, 'all_handovers_v2_owned': True}
(root / 'parity.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
if result['changed_channels']:
    raise SystemExit('fixture region differs; see parity.json')
