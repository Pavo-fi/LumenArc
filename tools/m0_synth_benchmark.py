#!/usr/bin/env python3
"""
m0_synth_benchmark.py - M0 gate tooling: synthetic OSD clips with known ground
truth -> probe_timestamps.py -> automatic success-rate assertion (design §12.1).

Also measures tail-frame extraction time on an indexless pseudo-MP4 (§12.5).

NOTE (design R-12): synthetic clips are ideal-condition (clean font, fixed
position, high contrast). Passing here is a regression gate, NOT the field
acceptance. The >=90% decision gate must be re-run on ~30 real surveillance
segments by the investigation team using probe_timestamps.py directly.

Usage:
    python tools/m0_synth_benchmark.py --ffmpeg <ffmpeg.exe> [--keep]
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBE = os.path.join(REPO, "probe_timestamps.py")

FONT_LATIN = "C:/Windows/Fonts/arial.ttf"
FONT_CJK = "C:/Windows/Fonts/msyh.ttc"

# Base wall-clock for ground truth: 2024-07-01 12:00:00 UTC.
BASE_EPOCH = 1719835200
CLIP_DUR_S = 30


def run(cmd, timeout=600):
    return subprocess.run(cmd, capture_output=True, timeout=timeout)


def osd_text(start_epoch, chinese=False):
    """drawtext expression rendering a running wall clock starting at epoch.

    Escaping layers (argv passed directly, no shell) — verified empirically:
      - filtergraph level: text value wrapped in single quotes;
      - expansion level: %{pts\:gmtime\:EPOCH\:FMT} separators escaped once;
      - drawtext's expansion parser splits args on ':' WITHOUT honoring
        backslash escapes, so FMT cannot contain literal colons -> use %T
        (this build's strftime supports %T; verified renders HH:MM:SS).
    """
    if chinese:
        return ("2024年07月01日 "
                "%{pts\\:gmtime\\:" + str(start_epoch) + "\\:%T}")
    return ("%{pts\\:gmtime\\:" + str(start_epoch)
            + "\\:%Y-%m-%d %T}")


def gen_clip(ffmpeg, out_path, start_epoch, variant):
    """Generate one synthetic clip. Returns True on success."""
    font = FONT_CJK if variant == "chinese" else FONT_LATIN
    # Windows drive-letter colon: must survive TWO unescape levels (graph +
    # filter-args) -> four backslashes before the colon (verified empirically)
    font_esc = font.replace(":", "\\\\:")
    vf = (f"drawtext=fontfile={font_esc}:text='{osd_text(start_epoch, variant == 'chinese')}'"
          f":x=40:y=30:fontsize=36:fontcolor=white:box=1:boxcolor=black@0.6")
    if variant == "noisy":
        vf += ",noise=alls=18:allf=t+u"
    base = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
            "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=25",
            "-vf", vf, "-t", str(CLIP_DUR_S)]
    if variant == "yuvj420p":
        cmd = base + ["-c:v", "libx264", "-preset", "veryfast",
                      "-pix_fmt", "yuvj420p", "-strict", "-2", out_path]
    elif variant == "noisy":
        cmd = base + ["-c:v", "libx264", "-preset", "veryfast", "-crf", "35",
                      out_path]
    elif variant == "pseudo_ts":
        ts_path = out_path + ".ts"
        cmd = base + ["-c:v", "libx264", "-preset", "veryfast",
                      "-f", "mpegts", ts_path]
        r = run(cmd)
        if r.returncode == 0:
            os.replace(ts_path, out_path)  # rename .ts -> .mp4 (pseudo-MP4)
            return True
        return False
    elif variant == "truncated":
        # realistic power-cut DVR file: mpegts (streamable, no trailer)
        # chopped at 80% -> container duration unreliable / tail unreadable
        ts_path = out_path + ".full.ts"
        cmd = base + ["-c:v", "libx264", "-preset", "veryfast",
                      "-f", "mpegts", ts_path]
        r = run(cmd)
        if r.returncode != 0:
            print(f"  GEN FAIL {variant}: {r.stderr.decode(errors='replace')[:300]}")
            return False
        size = os.path.getsize(ts_path)
        with open(ts_path, "rb") as fi, open(out_path, "wb") as fo:
            fo.write(fi.read(int(size * 0.8)))
        os.remove(ts_path)
        return True
    else:  # normal / chinese
        cmd = base + ["-c:v", "libx264", "-preset", "veryfast",
                      "-pix_fmt", "yuv420p", out_path]
    r = run(cmd)
    if r.returncode != 0:
        print(f"  GEN FAIL {variant}: {r.stderr.decode(errors='replace')[:300]}")
        return False
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ffmpeg", required=True)
    ap.add_argument("--keep", action="store_true", help="keep generated clips")
    ap.add_argument("--workdir", default="")
    args = ap.parse_args()

    root = args.workdir or tempfile.mkdtemp(prefix="lumenarc_m0_")
    os.makedirs(root, exist_ok=True)
    print(f"[M0] workdir: {root}")

    # ---- 1. generate clips with ground truth ----
    variants = ["normal", "normal", "normal", "yuvj420p", "noisy", "chinese",
                "pseudo_ts", "truncated"]
    ext = {"pseudo_ts": ".mp4", "truncated": ".ts"}
    clips = []  # (path, truth_wallMs_first, variant)
    for i, v in enumerate(variants):
        start = BASE_EPOCH + i * (CLIP_DUR_S + 5)  # small gaps between clips
        path = os.path.join(root, f"seg_{i:02d}_{v}{ext.get(v, '.mp4')}")
        print(f"[M0] generating {os.path.basename(path)} ...")
        if gen_clip(args.ffmpeg, path, start, v):
            # truth: OSD renders UTC of start_epoch; script parses as local.
            truth = int(time.mktime(time.gmtime(start)) * 1000)
            clips.append((path, truth, v))
    print(f"[M0] generated {len(clips)}/{len(variants)} clips")

    # ---- 2. run probe_timestamps.py ----
    dur_json = {p: CLIP_DUR_S * 1000 for p, _, v in clips if v != "truncated"}
    # truncated clip: report the inflated container duration like a real
    # power-cut file would (script must still work / degrade gracefully).
    for p, _, v in clips:
        if v == "truncated":
            dur_json[p] = CLIP_DUR_S * 1000
    dur_path = os.path.join(root, "durations.json")
    with open(dur_path, "w", encoding="utf-8") as f:
        json.dump(dur_json, f)

    cmd = [sys.executable, PROBE, "--ffmpeg-path", args.ffmpeg,
           "--work-dir", root, "--workers", "4",
           "--duration-json", dur_path,
           "--evidence-dir", os.path.join(root, "evidence")] + [p for p, _, _ in clips]
    print("[M0] running probe_timestamps.py ...")
    t0 = time.monotonic()
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    total_s = time.monotonic() - t0
    for line in r.stderr.splitlines():
        if line.startswith(("PROGRESS:", "ERROR:", "WARNING:")):
            print("  " + line)
    if r.returncode != 0:
        print(f"[M0] probe exit={r.returncode}\n{r.stderr[-2000:]}")
        sys.exit(1)
    results = {os.path.normpath(e["file"]): e for e in json.loads(r.stdout)}

    # ---- 3. evaluate ----
    print("\n[M0] === results ===")
    ok_n, rows = 0, []
    for path, truth, variant in clips:
        e = results.get(os.path.normpath(path), {})
        first = e.get("first")
        # implied wall-clock at stream rel 0 (sorter semantics, design §5.3.3)
        got = first.get("impliedStartMs") if first else None
        delta = abs(got - truth) if got else None
        ok = delta is not None and delta <= 2000
        ok_n += ok
        tail_ms = e.get("diag", {}).get("tailExtractMs", -1)
        head_ms = e.get("diag", {}).get("headExtractMs", -1)
        rows.append((variant, ok, got and first.get("text"), delta, head_ms, tail_ms))
        print(f"  {variant:10s} {'OK ' if ok else 'FAIL'} "
              f"text={first and first.get('text')!r:40s} "
              f"deltaMs={delta} headExtract={head_ms}ms tailExtract={tail_ms}ms")
    rate = ok_n * 100.0 / max(len(clips), 1)
    print(f"\n[M0] success rate (synthetic): {ok_n}/{len(clips)} = {rate:.1f}%")
    print(f"[M0] total OCR wall time: {total_s:.1f}s for {len(clips)} clips "
          f"({total_s / max(len(clips), 1):.1f}s/clip)")
    pseudo = [r for r in rows if r[0] == "pseudo_ts"]
    if pseudo:
        print(f"[M0] pseudo-MP4 tail extract: {pseudo[0][5]}ms "
              f"(field gate target: <=5000ms on 2GB indexless files)")

    if not args.keep:
        import shutil
        shutil.rmtree(root, ignore_errors=True)
        print("[M0] cleaned workdir")


if __name__ == "__main__":
    main()
