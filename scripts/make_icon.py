"""Generates the LocalFlix application icon (no third-party packages needed).

    python scripts/make_icon.py

Writes resources/icons/localflix.png (256 px, used inside the app) and
resources/localflix.ico (PNG-compressed, multiple sizes, used for the .exe).
The design is flat on purpose (solid colours, no gradients or shadows): a rounded square in the
app's accent blue, a white play triangle and a progress bar ("a player that remembers where you
stopped"). Very small sizes drop the bar so the triangle stays readable.
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


# Flat palette (matches the accent colour used by the application theme).
BLUE = (0x5B, 0x8D, 0xEF)
BLUE_TRACK = (0x3F, 0x6B, 0xD1)  # darker solid blue for the unfilled part of the bar
WHITE = (255, 255, 255)


def render(size: int) -> bytes:
    ss = 3 if size <= 128 else 2  # supersampling grid per axis
    radius = size * 0.22
    show_bar = size >= 40
    if show_bar:
        # triangle sits in the upper part, the bar underneath
        tri = (
            (size * 0.37, size * 0.24),
            (size * 0.37, size * 0.62),
            (size * 0.73, size * 0.43),
        )
        bar_y0, bar_y1 = size * 0.75, size * 0.81
        bar_x0, bar_x1 = size * 0.20, size * 0.80
        bar_fill = bar_x0 + (bar_x1 - bar_x0) * 0.62
    else:
        # tiny sizes: just a larger, vertically centred triangle
        tri = (
            (size * 0.34, size * 0.22),
            (size * 0.34, size * 0.78),
            (size * 0.78, size * 0.50),
        )
        bar_y0 = bar_y1 = bar_x0 = bar_x1 = bar_fill = -1.0

    rows = bytearray()
    for y in range(size):
        rows.append(0)  # PNG filter: none
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    px = x + (sx + 0.5) / ss
                    py = y + (sy + 0.5) / ss
                    if rounded_rect_alpha(px, py, size, radius) == 0.0:
                        continue
                    colour = BLUE
                    if in_triangle(px, py, *tri):
                        colour = WHITE
                    elif show_bar and bar_y0 <= py <= bar_y1 and bar_x0 <= px <= bar_x1:
                        colour = WHITE if px <= bar_fill else BLUE_TRACK
                    acc[0] += colour[0]
                    acc[1] += colour[1]
                    acc[2] += colour[2]
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
