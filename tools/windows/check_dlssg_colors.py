"""Compare the same-frame host-processed FG inputs outside the game's HUD.

Consumes BB_DUMP_FG_COLOR_TRIGGER captures, not NVIDIA's generated frames.
The scene region deliberately excludes the HUD across the top of the screen.
"""
import argparse
import json
from pathlib import Path
import re


def check(directory):
    final = next(directory.glob('f000_fg_final_*.raw'))
    match = re.fullmatch(r'f000_fg_final_(\d+)x(\d+)_(rgba|bgra)\.raw', final.name)
    if not match:
        raise ValueError('Unexpected capture name')
    width, height = map(int, match.groups()[:2])
    layout = match.group(3)
    hudless = directory / f'f000_fg_hudless_{width}x{height}_{layout}.raw'
    a, b = final.read_bytes(), hudless.read_bytes()
    if len(a) != width * height * 4 or len(b) != len(a):
        raise ValueError('Incomplete or differently sized captures')
    normal = swapped = changed = count = maximum = 0
    for y in range(int(height * .30), int(height * .93), 2):
        for x in range(int(width * .03), int(width * .97), 2):
            offset = (y * width + x) * 4
            errors = [abs(a[offset+c] - b[offset+c]) for c in range(3)]
            normal += sum(errors)
            swapped += sum(abs(a[offset+c] - b[offset+2-c]) for c in range(3))
            maximum = max(maximum, *errors)
            changed += max(errors) > 2
            count += 1
    result = {'final': str(final), 'hudless': str(hudless), 'resolution': f'{width}x{height}',
              'sampled_pixels': count, 'rgb_mean_error_8bit': normal / (3 * count),
              'rgb_max_error_8bit': maximum, 'wrong_rb_mean_error_8bit': swapped / (3 * count),
              'pixels_differing_over_2_fraction': changed / count,
              'colors_match': normal / (3 * count) <= .5,
              'note': 'Original same-frame FG inputs; generated frames and input latency are not captured.'}
    (directory.parent / 'color-check.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, help='Integration case capture/ directory')
    args = parser.parse_args()
    result = check(args.directory)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['colors_match'] else 1)
