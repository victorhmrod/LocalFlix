"""Generates small synthetic videos for LocalFlix end-to-end tests.

Uses the mpv.exe from tools/mpv-cli (see docs/DEPENDENCIES.md) as an encoder, so
no other tool is required. Output goes to testdata/videos (git-ignored).

    python scripts/make_test_videos.py [--out DIR] [--seconds N]

Each clip is numbered ("Episode 1" burned into the picture, a different tone
per episode) so it is obvious on screen which file is playing. The clips use
different containers on purpose (mp4, mkv, webm, avi, mov).
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MPV = ROOT / "tools" / "mpv-cli" / "mpv.exe"
FONT = "C\\:/Windows/Fonts/arial.ttf"

# One longer clip for resume tests: positions under 5 s are deliberately not
# resumed, so the short 6 s clips cannot demonstrate resuming.
LONG_CLIP = ("long_resume_test.mkv", ["--ovc=libx264", "--oac=aac", "--ovcopts=preset=ultrafast,crf=34"], 40)

# (file name, container-specific encoder arguments)
CLIPS = [
    ("video_27.mkv", ["--ovc=libx264", "--oac=aac", "--ovcopts=preset=ultrafast,crf=32"]),
    ("downloaded_episode_04.mp4", ["--ovc=libx264", "--oac=aac", "--ovcopts=preset=ultrafast,crf=32"]),
    ("anime_final.webm", ["--ovc=libvpx", "--oac=libvorbis", "--ovcopts=deadline=realtime,cpu-used=8,b=300k"]),
    ("episode_01.avi", ["--ovc=mpeg4", "--oac=libmp3lame", "--ovcopts=qscale=8"]),
    ("S01E05 - The Reunion.mov", ["--ovc=libx264", "--oac=aac", "--ovcopts=preset=ultrafast,crf=32"]),
]


def encode(name: str, extra: list[str], label: int, seconds: int, out_dir: Path) -> None:
    target = out_dir / name
    if target.exists():
        print(f"  exists  {name}")
        return
    graph = (
        f"testsrc2=size=640x360:rate=25:duration={seconds},"
        f"drawtext=fontfile='{FONT}':text='Clip {label}  ({name})':fontsize=26:"
        "fontcolor=white:box=1:boxcolor=black@0.65:x=(w-text_w)/2:y=h-60"
    )
    freq = 220 + label * 110
    cmd = [
        str(MPV), "--no-config", "--really-quiet",
        f"av://lavfi:{graph}",
        f"--audio-file=av://lavfi:sine=frequency={freq}:duration={seconds}",
        f"--o={target}",
        *extra,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0 or not target.exists():
        print(f"  FAILED  {name}\n{result.stdout}\n{result.stderr}", file=sys.stderr)
        raise SystemExit(1)
    print(f"  created {name} ({target.stat().st_size // 1024} KB)")


def write_subtitle(out_dir: Path, seconds: int) -> None:
    srt = out_dir / "downloaded_episode_04.srt"
    if srt.exists():
        return
    srt.write_text(
        "1\n00:00:00,500 --> 00:00:02,500\nExternal subtitle line one\n\n"
        f"2\n00:00:02,600 --> 00:00:0{min(seconds, 9)},000\nExternal subtitle line two\n",
        encoding="utf-8",
    )
    print(f"  created {srt.name}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "testdata" / "videos")
    parser.add_argument("--seconds", type=int, default=6, help="length of each clip")
    args = parser.parse_args()

    if not MPV.exists():
        print(f"mpv.exe not found at {MPV}. See docs/DEPENDENCIES.md.", file=sys.stderr)
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    print(f"Writing {len(CLIPS) + 1} clips ({len(CLIPS)} of {args.seconds}s + one long clip) to {args.out}")
    for i, (name, extra) in enumerate(CLIPS, start=1):
        encode(name, extra, i, args.seconds, args.out)
    encode(LONG_CLIP[0], LONG_CLIP[1], len(CLIPS) + 1, LONG_CLIP[2], args.out)
    write_subtitle(args.out, args.seconds)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
