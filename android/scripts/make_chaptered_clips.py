"""Generates the small chaptered clips the Android instrumented tests need (skipping openings and endings).

    python android/scripts/make_chaptered_clips.py [--out DIR]

Output goes to android/testdata (git-ignored), which app/build.gradle.kts bundles into the test APK next to the
clips of scripts/make_test_videos.py. The chapters can only be written by ffmpeg (the mpv used for the other clips
does not copy them into its output), so ffmpeg must be on the PATH. The pictures are synthetic: a test pattern
with a different tone per clip. Nothing here reads or touches any video of yours.

  chaptered_episode.mkv / .mp4  20 s: Opening 0-5 s, Episode 5-10 s, Ending 10-20 s (runs to the end of the file)
  chaptered_postcredits.mkv     20 s: Episode 0-10 s, Ending 10-17 s, Post-Credits Scene 17-20 s
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # android/
SECONDS = 20

# (file name, tone in Hz, [(chapter title, start second)]) -- a chapter lasts until the next one starts
CLIPS = [
    ("chaptered_episode.mkv", 330, [("Opening", 0), ("Episode", 5), ("Ending", 10)]),
    ("chaptered_episode.mp4", 330, [("Opening", 0), ("Episode", 5), ("Ending", 10)]),
    ("chaptered_postcredits.mkv", 440, [("Episode", 0), ("Ending", 10), ("Post-Credits Scene", 17)]),
]


def ffmetadata(chapters: list[tuple[str, int]]) -> str:
    lines = [";FFMETADATA1"]
    for i, (title, start) in enumerate(chapters):
        end = chapters[i + 1][1] if i + 1 < len(chapters) else SECONDS
        lines += ["[CHAPTER]", "TIMEBASE=1/1000", f"START={start * 1000}", f"END={end * 1000}", f"title={title}"]
    return "\n".join(lines) + "\n"


def encode(name: str, tone: int, chapters: list[tuple[str, int]], out_dir: Path, ffmpeg: str) -> None:
    target = out_dir / name
    if target.exists():
        print(f"  exists  {name}")
        return
    with tempfile.TemporaryDirectory() as tmp:
        meta = Path(tmp) / "chapters.ffmeta"
        meta.write_text(ffmetadata(chapters), encoding="utf-8", newline="\n")
        cmd = [
            ffmpeg, "-v", "error", "-y",
            "-f", "lavfi", "-i", f"testsrc2=size=640x360:rate=25:duration={SECONDS}",
            "-f", "lavfi", "-i", f"sine=frequency={tone}:duration={SECONDS}",
            "-i", str(meta), "-map", "0:v", "-map", "1:a", "-map_chapters", "2",
            "-c:v", "libx264", "-preset", "ultrafast", "-crf", "34", "-pix_fmt", "yuv420p", "-c:a", "aac",
            str(target),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0 or not target.exists():
        print(f"  FAILED  {name}\n{result.stdout}\n{result.stderr}", file=sys.stderr)
        raise SystemExit(1)
    print(f"  created {name} ({target.stat().st_size // 1024} KB)")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "testdata", help="output directory (default: android/testdata)")
    args = parser.parse_args()

    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ffmpeg not found on the PATH (needed to write chapters into the test clips)")
    args.out.mkdir(parents=True, exist_ok=True)
    print(f"Generating chaptered clips in {args.out}")
    for name, tone, chapters in CLIPS:
        encode(name, tone, chapters, args.out, ffmpeg)


if __name__ == "__main__":
    main()
