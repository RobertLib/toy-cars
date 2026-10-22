"""Draw every circuit at one common scale: screenshots/track-layouts.svg.

    python3 tools/layout_svg.py

Reads the exported tracks (assets/tracks/*.tcp), the island outlines in
tools/themes.py and the names and colours in src/tracks.c. Bridges are drawn
where the road passes over itself, ramps as bars across the road, the finish as
a chequered line. No Blender needed.
"""
from pathlib import Path
import math
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from themes import THEMES  # noqa: E402

COLUMNS, CARD_W, CARD_H, GAP = 5, 300, 380, 14
SCALE = 1.08 / 2.0   # Pixels per metre, the same for every island
CUP_NAMES = ('ROOKIE CUP', 'PRO CUP', 'MASTER CUP')


def track_info():
    """Name, subtitle, accent and cup of each circuit, from src/tracks.c."""
    source = (ROOT / 'src' / 'tracks.c').read_text()
    info = {}
    for entry in re.finditer(r'\{\.id = "(\w+)", \.name = "([^"]+)", \.subtitle = "([^"]+)",(.*?)\.crossings',
                             source, re.S):
        key, name, subtitle, body = entry.groups()
        accent = [float(v.rstrip('f')) for v in re.search(r'\.accent = \{([^}]*)\}', body).group(1).split(',')[:3]]
        cup = int(re.search(r'\.cup = (\d)', body).group(1))
        night = '.night = true' in body
        info[key] = dict(name=name, subtitle=subtitle, accent=accent, cup=cup, night=night)
    return info


def read_track(key):
    raw = (ROOT / 'assets' / 'tracks' / (key + '.tcp')).read_bytes()
    count, cans, ramps, length = struct.unpack_from('<IIIf', raw, 4)
    points = [struct.unpack_from('<7f', raw, 20 + i * 28) for i in range(count)]
    offset = 20 + count * 28 + cans * 8
    ramp_points = [points[int(struct.unpack_from('<4f', raw, offset + i * 16)[0])] for i in range(ramps)]
    return points, ramp_points, length


def crossings(points):
    """Station pairs where the road passes over itself, upper level first."""
    n = len(points)
    found = []
    for i in range(n):
        a, b = points[i], points[(i + 1) % n]
        for j in range(i + 3, n - (i == 0)):
            c, d = points[j], points[(j + 1) % n]
            det = (b[0] - a[0]) * (d[2] - c[2]) - (b[2] - a[2]) * (d[0] - c[0])
            if abs(det) < 1e-9:
                continue
            u = ((c[0] - a[0]) * (d[2] - c[2]) - (c[2] - a[2]) * (d[0] - c[0])) / det
            v = ((c[0] - a[0]) * (b[2] - a[2]) - (c[2] - a[2]) * (b[0] - a[0])) / det
            if 0 < u < 1 and 0 < v < 1:
                found.append((i, j) if a[1] > c[1] else (j, i))
    return found


def hex_color(rgb, k=1.):
    return '#' + ''.join(f'{max(0, min(255, round(v * k * 255))):02x}' for v in rgb)


def main():
    info = track_info()
    rows = math.ceil(len(THEMES) / COLUMNS)
    width = COLUMNS * CARD_W + (COLUMNS + 1) * GAP
    height = rows * CARD_H + (rows + 1) * GAP
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}">',
           f'<rect width="{width}" height="{height}" fill="#f5f3ec"/>']
    for k, T in enumerate(THEMES):
        meta = info[T.key]
        points, ramps, length = read_track(T.key)
        x0 = GAP + (k % COLUMNS) * (CARD_W + GAP)
        y0 = GAP + (k // COLUMNS) * (CARD_H + GAP)
        cx, cy = x0 + CARD_W / 2, y0 + 72 + (CARD_H - 120) / 2
        accent = hex_color(meta['accent'])
        out.append(f'<rect x="{x0}" y="{y0}" width="{CARD_W}" height="{CARD_H}" rx="16" fill="white"/>')
        out.append(f'<text x="{x0 + 18}" y="{y0 + 30}" font-family="sans-serif" font-size="12" '
                   f'font-weight="bold" fill="#8a8f8c">{k + 1} · {CUP_NAMES[meta["cup"]]}</text>')
        out.append(f'<text x="{x0 + 18}" y="{y0 + 54}" font-family="sans-serif" font-size="21" '
                   f'font-weight="bold" fill="{accent}">{meta["name"]}</text>')
        sea = '#d6e6ea' if T.sea else '#edf0e8'
        outline = []
        for j in range(144):
            a = j * 2 * math.pi / 144
            r = T.edge_radius(a) * SCALE
            outline.append(f'{cx + math.cos(a) * r:.1f},{cy + math.sin(a) * r:.1f}')
        out.append(f'<polygon points="{" ".join(outline)}" fill="{sea}"/>')
        def screen(p):
            return f'{cx + p[0] * SCALE:.1f},{cy + p[2] * SCALE:.1f}'
        road = ' '.join(screen(p) for p in points[::2]) + ' ' + screen(points[0])
        out.append(f'<polyline points="{road}" fill="none" stroke="#2f3433" stroke-width="5.5" '
                   f'stroke-linejoin="round"/>')
        out.append(f'<polyline points="{road}" fill="none" stroke="{accent}" stroke-width="2.2" '
                   f'stroke-linejoin="round"/>')
        # The upper level of each crossing is redrawn on top with a pale casing.
        for upper, _ in crossings(points):
            span = [points[(upper + d) % len(points)] for d in range(-14, 15, 2)]
            deck = ' '.join(screen(p) for p in span)
            out.append(f'<polyline points="{deck}" fill="none" stroke="white" stroke-width="9"/>')
            out.append(f'<polyline points="{deck}" fill="none" stroke="#2f3433" stroke-width="5.5"/>')
            out.append(f'<polyline points="{deck}" fill="none" stroke="{accent}" stroke-width="2.2"/>')
        for p in ramps:
            a = (cx + (p[0] + p[4] * 6) * SCALE, cy + (p[2] - p[3] * 6) * SCALE)
            b = (cx + (p[0] - p[4] * 6) * SCALE, cy + (p[2] + p[3] * 6) * SCALE)
            out.append(f'<line x1="{a[0]:.1f}" y1="{a[1]:.1f}" x2="{b[0]:.1f}" y2="{b[1]:.1f}" '
                       f'stroke="#e8b200" stroke-width="3.4" stroke-linecap="round"/>')
        p = points[0]
        for side in (-1, 1):
            q = (cx + (p[0] + p[4] * 7 * side) * SCALE, cy + (p[2] - p[3] * 7 * side) * SCALE)
            out.append(f'<line x1="{cx + p[0] * SCALE:.1f}" y1="{cy + p[2] * SCALE:.1f}" '
                       f'x2="{q[0]:.1f}" y2="{q[1]:.1f}" stroke="{"#111" if side < 0 else "#fff"}" '
                       f'stroke-width="3"/>')
        climb = max(q[1] for q in points) - min(q[1] for q in points)
        facts = (f'{length / 1000:.2f} km · {len(crossings(points))} '
                 f'{"bridge" if len(crossings(points)) == 1 else "bridges"} · '
                 f'{len(ramps)} jumps · {climb:.0f} m climb{" · night" if meta["night"] else ""}')
        out.append(f'<text x="{x0 + CARD_W / 2}" y="{y0 + CARD_H - 22}" text-anchor="middle" '
                   f'font-family="sans-serif" font-size="13" fill="#54605e">{facts}</text>')
    out.append('</svg>')
    path = ROOT / 'screenshots' / 'track-layouts.svg'
    path.write_text('\n'.join(out) + '\n')
    print('Wrote', path.relative_to(ROOT))


if __name__ == '__main__':
    main()
