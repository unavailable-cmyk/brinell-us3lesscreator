"""Check a seeded mixed native/V2 color-and-shake game capture."""
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
native = json.loads((root / 'baseline.json').read_text(encoding='utf-8-sig'))
shared = json.loads((root / 'shared.json').read_text(encoding='utf-8-sig'))
a, b = native['capture'], shared['capture']
fields = ('level_time', 'ticks', 'player_x', 'player_y', 'fixture_camera', 'width', 'height')
if any(a[key] != b[key] for key in fields):
    raise SystemExit('Native simulation or seeded camera differs')
if not b['owned'] or not shared['rejections'].get('mutable_or_unresolved_color_channel', 0):
    raise SystemExit('Fixture did not exercise both V2 ownership and native color fallback')
left = (root / 'baseline.ppm').read_bytes().split(b'\n', 3)
right = (root / 'shared.ppm').read_bytes().split(b'\n', 3)
if left[:3] != right[:3] or left[0] != b'P6' or left[2] != b'255':
    raise SystemExit('Unsupported or mismatched image headers')
width, height = map(int, left[1].split())
if len(left[3]) != width * height * 3 or len(right[3]) != width * height * 3:
    raise SystemExit('Truncated PPM')
# Pad the bounds of accepted objects to include adjacent native-fallback objects.
x0 = max(0, min(a['roi_left'], b['roi_left']) - 40)
y0 = max(0, min(a['roi_top'], b['roi_top']) - 40)
x1 = min(width, max(a['roi_right'], b['roi_right']) + 40)
y1 = min(height, max(a['roi_bottom'], b['roi_bottom']) + 40)
differences = [abs(left[3][(y * width + x) * 3 + c] - right[3][(y * width + x) * 3 + c])
               for y in range(y0, y1) for x in range(x0, x1) for c in range(3)]
result = {'checkpoint_and_camera_equal': True, 'changed_channels': sum(d != 0 for d in differences),
          'max_channel_delta': max(differences, default=0), 'roi': [x0, y0, x1, y1],
          'owned_at_capture': b['owned'], 'native_color_rejections': shared['rejections']['mutable_or_unresolved_color_channel'],
          'max_reference_position_error': shared['max_reference_position_error']}
(root / 'parity.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
if result['changed_channels'] or result['max_reference_position_error']:
    raise SystemExit('Mixed color/shake parity failed')
