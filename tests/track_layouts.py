"""Reject duplicated circuit shapes, including rotated/scaled/sheared copies.

Run with: python3 tests/track_layouts.py
"""
from pathlib import Path
import math
import struct

ROOT = Path(__file__).resolve().parents[1]
NAMES = ("country", "beach", "winter")


def solve(matrix, rhs):
    rows = [list(row) + [value] for row, value in zip(matrix, rhs)]
    for column in range(3):
        pivot = max(range(column, 3), key=lambda i: abs(rows[i][column]))
        rows[column], rows[pivot] = rows[pivot], rows[column]
        scale = rows[column][column]
        assert abs(scale) > 1e-8, "degenerate circuit"
        rows[column] = [v / scale for v in rows[column]]
        for i in range(3):
            if i != column:
                factor = rows[i][column]
                rows[i] = [a - factor * b for a, b in zip(rows[i], rows[column])]
    return [row[3] for row in rows]


def affine_error(source, target):
    basis = [(x, z, 1) for x, z in source]
    matrix = [[sum(p[a] * p[b] for p in basis) for b in range(3)] for a in range(3)]
    coefficients = [solve(matrix, [sum(p[a] * q[axis] for p, q in zip(basis, target))
                                   for a in range(3)]) for axis in range(2)]
    return math.sqrt(sum((sum(v * c for v, c in zip(p, coefficients[axis])) - q[axis])**2
                         for p, q in zip(basis, target) for axis in range(2)) / len(basis))


def main():
    circuits = []
    for name in NAMES:
        raw = (ROOT / "assets" / "tracks" / (name + ".tcp")).read_bytes()
        count, _, _, length = struct.unpack_from("<IIIf", raw, 4)
        assert raw[:4] == b"TCP1" and length > 900, (name, "extended circuit")
        points = [struct.unpack_from("<7f", raw, 20 + i * 28) for i in range(0, count, 8)]
        circuits.append([(p[0], p[2]) for p in points])
    # Prove the regression check recognizes the affine copies it must reject.
    copied = [(1.04*x + .06*z + 7, .94*z - 3) for x, z in circuits[0]]
    assert affine_error(circuits[0], copied) < 1e-6
    for i, source in enumerate(circuits):
        for j in range(i + 1, len(circuits)):
            target = circuits[j]
            best = min(affine_error(source, route[shift:] + route[:shift])
                       for route in (target, list(reversed(target)))
                       for shift in range(len(target)))
            assert best > 8, (NAMES[i], NAMES[j], "affine copies of the same circuit", best)
            print(f"{NAMES[i]} / {NAMES[j]}: best affine match still differs by {best:.1f} m")
    print("Distinct track layouts PASSED")


if __name__ == "__main__":
    main()
