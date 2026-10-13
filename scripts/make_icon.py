"""Generates the LocalFlix application icon (no third-party packages needed).

    python scripts/make_icon.py

Writes resources/icons/localflix.png (256 px, used inside the app) and
resources/localflix.ico (PNG-compressed, multiple sizes, used for the .exe).
The design: a rounded square with a blue gradient, a play triangle and a thin
progress bar: "a player that remembers where you stopped".
"""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIZES = (16, 24, 32, 48, 64, 128, 256)


def rounded_rect_alpha(x: float, y: float, size: float, radius: float) -> float:
    """1.0 inside the rounded square, 0.0 outside (hard edge, antialiased by supersampling)."""
    cx = min(max(x, radius), size - radius)
    cy = min(max(y, radius), size - radius)
    return 1.0 if (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2 else 0.0


def in_triangle(px: float, py: float, a, b, c) -> bool:
    def sign(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])

    d1 = sign((px, py), a, b)
    d2 = sign((px, py), b, c)
    d3 = sign((px, py), c, a)
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)


def render(size: int) -> bytes:
    ss = 3 if size <= 128 else 2  # supersampling grid per axis
    radius = size * 0.22
    # play triangle, optically centred slightly right of middle
    tri = (
        (size * 0.38, size * 0.27),
        (size * 0.38, size * 0.66),
        (size * 0.74, size * 0.465),
    )
    bar_y0, bar_y1 = size * 0.78, size * 0.84
    bar_x0, bar_x1 = size * 0.20, size * 0.80
    bar_fill = bar_x0 + (bar_x1 - bar_x0) * 0.62

    rows = bytearray()
    for y in range(size):
        rows.append(0)  # PNG filter: none
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    px = x + (sx + 0.5) / ss
                    py = y + (sy + 0.5) / ss
                    a = rounded_rect_alpha(px, py, size, radius)
                    if a == 0.0:
                        continue
                    t = (px + py) / (2 * size)  # diagonal gradient
                    r = 0x1B + (0x4C - 0x1B) * t
                    g = 0x24 + (0x7B - 0x24) * t
                    b = 0x3F + (0xE6 - 0x3F) * t
                    if in_triangle(px, py, *tri):
                        r, g, b = 255, 255, 255
                    elif bar_y0 <= py <= bar_y1 and bar_x0 <= px <= bar_x1:
                        if px <= bar_fill:
                            r, g, b = 255, 255, 255
                        else:
                            r, g, b = r * 0.55 + 255 * 0.0, g * 0.55, b * 0.55 + 70
                    acc[0] += r
                    acc[1] += g
                    acc[2] += b
                    acc[3] += 1.0
            n = ss * ss
            if acc[3] == 0:
                rows += bytes((0, 0, 0, 0))
            else:
                rows += bytes((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]), int(255 * acc[3] / n)))
    return bytes(rows)


def png_bytes(size: int) -> bytes:
    raw = render(size)

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )


def main() -> None:
    images = {s: png_bytes(s) for s in SIZES}
    png_path = ROOT / "resources" / "icons" / "localflix.png"
    png_path.parent.mkdir(parents=True, exist_ok=True)
    png_path.write_bytes(images[256])

    header = struct.pack("<HHH", 0, 1, len(SIZES))
    entries = b""
    offset = 6 + 16 * len(SIZES)
    blobs = b""
    for s in SIZES:
        data = images[s]
        entries += struct.pack("<BBBBHHII", 0 if s == 256 else s, 0 if s == 256 else s, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        blobs += data
    (ROOT / "resources" / "localflix.ico").write_bytes(header + entries + blobs)
    print(f"wrote {png_path.relative_to(ROOT)} and resources/localflix.ico ({len(SIZES)} sizes)")


if __name__ == "__main__":
    main()
