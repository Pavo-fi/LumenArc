#!/usr/bin/env python3
"""
probe_timestamps.py - OSD timestamp OCR for LumenArc preprocessing (design §6.4).

Extracts first/last candidate frames from each video via ffmpeg, locates OSD
regions (4 corners + top/bottom center), preprocesses crops (CLAHE + OTSU),
runs RapidOCR, parses timestamps with prioritized regexes, and votes across
candidate frames. Forensic rule: raw OCR text is preserved verbatim; parsed
values are derived and reported alongside.

Usage:
    python probe_timestamps.py --ffmpeg-path <ffmpeg> --work-dir <dir>
        [--workers N] [--duration-json <path>] [--with-sha256]
        [--evidence-dir <dir>] <file1> [file2 ...]

Protocol (P1, same family as analyze_video.py):
    stdout: single-line JSON array, one object per file
    stderr: PROGRESS:<done>|<total>|<pct> / ERROR:<file>:<reason> / WARNING:<msg>
"""

import argparse
import atexit
import concurrent.futures as futures
import datetime
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

import cv2
import numpy as np

# ---------------------------------------------------------------------------
# Constants (tuning table, design §5.2.3 - adjust with real footage only)
# ---------------------------------------------------------------------------
CROP_W_RATIO = 0.30          # crop block width  = 30% of frame width
CROP_W_RATIO_WIDE = 0.60     # second-pass width when the OSD line is cut off
CROP_H_RATIO = 0.12          # crop block height = 12% of frame height
CROP_MIN_WIDTH = 512         # upscale crop to at least this width
OCR_MAX_WIDTH = 1600         # frames downscaled to this width before OCR
MIN_OCR_SCORE = 0.6          # below -> discard block (digit-confusion guard)
SINGLE_HIT_MIN_SCORE = 0.85  # single-frame vote acceptance (RapidOCR calibration)
VOTE_TOLERANCE_MS = 1500     # implied-start cluster tolerance
HEAD_OFFSETS_S = (0, 1, 2)   # first-frame candidates (boot black / OSD delay)
TAIL_OFFSETS_S = (3, 1)      # last-frame candidates relative to trusted end
FFMPEG_HEAD_TIMEOUT_S = 30
FFMPEG_TAIL_TIMEOUT_S = 90   # indexless pseudo-MP4 tail seek can be slow

# Regex priority list (design §5.2.4). Hit = stop.
# 秒组前允许空格（OCR 常把冒号读成空格："15:03 :25" 仍解析为 15:03:25）
# 日期与时间之间允许星期等无数字间隔（"2026-07-22 星期三 03:18:01"：
# 此前 \s*[T\s]? 桥接不了「星期三」，整行失配 → 窄裁剪碎片被
# RE_NO_YEAR 错配（"11.1" 被当 11月1日）→ 墙钟错数月 + 速率异常被拒）
RE_FULL = re.compile(
    r"(\d{4})[-/年.](\d{1,2})[-/月.](\d{1,2})[日号]?\s*"
    r"(?:星期[一二三四五六日天]\s*)?"
    r"(?:\([A-Za-z]{3}\)\s*|[A-Za-z]{3}\.\s*)?"
    r"[T\s]?"
    r"(\d{1,2}):(\d{2})(?:\s*:(\d{2}))?(?:[:.](\d{1,3}))?")
RE_NO_YEAR = re.compile(
    r"(?<!\d)(\d{1,2})[-/月.](\d{1,2})[日号]?\s+(\d{1,2}):(\d{2})\s*:(\d{2})(?!\d)")
RE_TIME_ONLY = re.compile(r"(?<!\d)(\d{1,2}):(\d{2})\s*:(\d{2})(?!\d)")
RE_FILENAME_YEAR = re.compile(r"(20\d{2})")
# 文件名完整日期（监控导出常见命名 20260722_031301_me00060：日期_时间_通道）
RE_FILE_FULLDATE = re.compile(r"(?<!\d)(20\d{2})(\d{2})(\d{2})(?!\d)")

# OCR digit-confusion normalization (applied to a DERIVED copy only; the raw
# text is never altered, design §9.2 forensic rule).
_CONFUSION = str.maketrans({"O": "0", "o": "0", "l": "1", "I": "1", "|": "1",
                            "S": "5", "s": "5", "B": "8", "Z": "2", "G": "6"})

# ---------------------------------------------------------------------------
# Process-global state (one RapidOCR instance per worker process)
# ---------------------------------------------------------------------------
_engine = None
_work_ctx = {}


def _init_engine():
    """ProcessPoolExecutor initializer: load model once per worker process.
    Single-threaded inference per worker: N workers x 1 thread avoids the
    thread oversubscription that starves the main app's analysis (field
    report: luminance analysis 5s -> 5min during OCR)."""
    global _engine
    from rapidocr_onnxruntime import RapidOCR
    try:
        _engine = RapidOCR(intra_op_num_threads=1, inter_op_num_threads=1)
    except TypeError:
        _engine = RapidOCR()


def _cleanup_dir(path):
    if path and os.path.isdir(path):
        shutil.rmtree(path, ignore_errors=True)


# ---------------------------------------------------------------------------
# ffmpeg helpers
# ---------------------------------------------------------------------------
def _ffprobe_path(ffmpeg_path):
    d = os.path.dirname(ffmpeg_path)
    name = "ffprobe.exe" if os.name == "nt" else "ffprobe"
    p = os.path.join(d, name)
    return p if os.path.isfile(p) else None


def ffprobe_duration_ms(ffmpeg_path, video_path):
    """Duration fallback when C++ did not supply a trusted duration."""
    ffprobe = _ffprobe_path(ffmpeg_path)
    if ffprobe:
        try:
            r = subprocess.run(
                [ffprobe, "-v", "error", "-show_entries", "format=duration",
                 "-of", "default=noprint_wrappers=1:nokey=1", video_path],
                capture_output=True, encoding="utf-8", errors="replace", timeout=30)
            if r.returncode == 0:
                return int(float(r.stdout.strip()) * 1000)
        except (subprocess.TimeoutExpired, ValueError):
            pass
    # Last resort: parse "Duration: hh:mm:ss.cc" from ffmpeg -i stderr
    try:
        r = subprocess.run([ffmpeg_path, "-hide_banner", "-i", video_path],
                           capture_output=True, encoding="utf-8", errors="replace", timeout=30)
        m = re.search(r"Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)", r.stderr)
        if m:
            h, mnt, s = int(m.group(1)), int(m.group(2)), float(m.group(3))
            return int((h * 3600 + mnt * 60 + s) * 1000)
    except subprocess.TimeoutExpired:
        pass
    return 0


def extract_frame(ffmpeg_path, video_path, out_png, ss=None, sseof=False,
                  timeout=FFMPEG_HEAD_TIMEOUT_S):
    """Extract one frame as PNG (lossless evidence; avoids the mjpeg encoder's
    full-range-YUV strictness) and MEASURE its true stream position.

    Seek accuracy (design §12.5, verified empirically): input-side -ss on
    indexless containers (TS/PS pseudo-MP4) is byte-interpolated and can land
    SECONDS away from the target. ffmpeg shifts timestamps so the seek point
    is 0, so the frame's true relative position = ss + pts_time (showinfo).
    The caller uses this measured relMs for voting -> inaccurate seeks still
    yield correct implied wall-start.

    Fallback chain: input seek (fast) -> output seek (demux+decode-discard,
    accurate). Returns (elapsed_ms, actual_rel_ms) or (-1, None).
    """
    attempts = []
    if sseof:
        attempts = [("eof", None)]   # position unknowable: bonus frame only
    elif ss is not None:
        attempts = [("in", ss), ("out", ss)]
    for mode, pos in attempts:
        cmd = [ffmpeg_path, "-hide_banner", "-y"]
        if mode == "eof":
            cmd += ["-sseof", "-0.1"]
        elif mode == "in":
            cmd += ["-ss", f"{pos:.3f}"]
        cmd += ["-i", video_path]
        if mode == "out":
            cmd += ["-ss", f"{pos:.3f}"]
        cmd += ["-frames:v", "1", "-vf", "showinfo", out_png]
        t0 = time.monotonic()
        try:
            r = subprocess.run(cmd, capture_output=True, encoding="utf-8",
                               errors="replace", timeout=timeout)
        except subprocess.TimeoutExpired:
            continue
        elapsed = int((time.monotonic() - t0) * 1000)
        ok = (r.returncode == 0 and os.path.isfile(out_png)
              and os.path.getsize(out_png) > 0)
        if not ok:
            if os.path.isfile(out_png):
                os.remove(out_png)  # zero-frame attempt leaves nothing behind
            continue
        m = re.search(r"pts_time:([-\d.]+)", r.stderr or "")
        if mode != "eof" and m and pos is not None:
            actual_ms = int((pos + float(m.group(1))) * 1000)
        elif mode != "eof":
            actual_ms = int(pos * 1000)  # no measurement: trust target
        else:
            actual_ms = -1               # unknown (bonus frame)
        return elapsed, actual_ms
    return -1, None


# ---------------------------------------------------------------------------
# OCR / parsing
# ---------------------------------------------------------------------------
def crop_blocks(img, wide=False):
    """6 OSD candidate blocks: 4 corners + top/bottom center (design §5.2.2).
    wide=True doubles width for CJK date lines that exceed the narrow crop."""
    h, w = img.shape[:2]
    cw = int(w * (CROP_W_RATIO_WIDE if wide else CROP_W_RATIO))
    cw = min(cw, w)
    ch = int(h * CROP_H_RATIO)
    # order: top-left, top-right, bottom-left, bottom-right, top-center, bottom-center
    boxes = [(0, 0), (w - cw, 0), (0, h - ch), (w - cw, h - ch),
             ((w - cw) // 2, 0), ((w - cw) // 2, h - ch)]
    crops = []
    for x, y in boxes:
        c = img[y:y + ch, x:x + cw]
        if c.shape[1] < CROP_MIN_WIDTH:
            scale = CROP_MIN_WIDTH / max(c.shape[1], 1)
            c = cv2.resize(c, None, fx=scale, fy=scale,
                           interpolation=cv2.INTER_CUBIC)
        crops.append(c)
    return crops


def preprocess(crop):
    """CLAHE contrast enhance + denoise + OTSU binarize (design §5.2.3)."""
    gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY) if crop.ndim == 3 else crop
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    enhanced = clahe.apply(gray)
    blur = cv2.GaussianBlur(enhanced, (3, 3), 0)
    _, binary = cv2.threshold(blur, 0, 255,
                              cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    kernel = np.ones((2, 2), np.uint8)
    opened = cv2.morphologyEx(binary, cv2.MORPH_OPEN, kernel)
    return enhanced, opened


def ocr_image(img):
    """Run RapidOCR on one image. Returns list[{box, text, score}]."""
    if _engine is None:
        _init_engine()
    try:
        result, _ = _engine(img)
    except Exception as e:  # engine must never kill the worker
        print(f"WARNING:RapidOCR inference failed: {e}", file=sys.stderr)
        return []
    if not result:
        return []
    return [{"box": item[0], "text": str(item[1]), "score": float(item[2])}
            for item in result]


def _imread_unicode(path):
    """cv2.imread 在 Windows 用 ANSI fopen：路径含中文（如视频位于
    “测试文件”目录 → 证据帧全在中文路径下）时静默返回 None，整片
    OCR 全灭。np.fromfile + imdecode 走 Win32 Unicode API。"""
    try:
        data = np.fromfile(path, dtype=np.uint8)
        if data.size == 0:
            return None
        return cv2.imdecode(data, cv2.IMREAD_COLOR)
    except OSError:
        return None


def merge_ocr_lines(items):
    """Merge same-baseline detection boxes into full lines.

    A single OSD line is often split into multiple boxes ("2024-07-01" +
    "12:00:01") or the regex needs the full line -> group by center-y,
    sort by x, join with a space. Score = min member score (weakest link).
    """
    if not items:
        return []
    for it in items:
        ys = [p[1] for p in it["box"]]
        xs = [p[0] for p in it["box"]]
        it["cy"] = sum(ys) / len(ys)
        it["x0"] = min(xs)
        it["h"] = max(ys) - min(ys) or 1.0
    med_h = sorted(it["h"] for it in items)[len(items) // 2]
    items = sorted(items, key=lambda it: (it["cy"], it["x0"]))
    lines = []
    for it in items:
        if lines and abs(it["cy"] - lines[-1][-1]["cy"]) <= max(med_h * 0.6, 4):
            lines[-1].append(it)
        else:
            lines.append([it])
    out = []
    for line in lines:
        line.sort(key=lambda it: it["x0"])
        text = " ".join(it["text"] for it in line)
        score = min(it["score"] for it in line)
        out.append((text, score))
    return out


def normalize_for_parse(text):
    t = text.translate(_CONFUSION)
    t = t.replace("：", ":").replace("　", " ")
    return t


def _valid(y, mo, d, h, mi, s):
    return (2000 <= y <= datetime.datetime.now().year + 1 and 1 <= mo <= 12
            and 1 <= d <= 31 and 0 <= h <= 23 and 0 <= mi <= 59 and 0 <= s <= 59)


def _file_full_date(filename):
    """文件名完整日期 (y, mo, d) 或 None（值域校验）。"""
    m = RE_FILE_FULLDATE.search(filename)
    if not m:
        return None
    y, mo, d = int(m.group(1)), int(m.group(2)), int(m.group(3))
    if 2000 <= y <= datetime.datetime.now().year + 1 and 1 <= mo <= 12 and 1 <= d <= 31:
        return (y, mo, d)
    return None


def parse_timestamp(text, filename=""):
    """Parse wall-clock from (normalized) OCR text.
    Returns (datetime, ms_frac, kind) or None. Value-range validated.
    kind:
      'full'     OSD 含完整日期（最可信）
      'noyear'   OSD 月-日 + 文件名年
      'timeonly' OSD 仅时分秒 + 文件名完整日期（纯时间 OSD 摄像头兜底）
    优先级即可信度；调用方在多点/多裁剪间应优先 full（防碎片错配月日）。"""
    t = normalize_for_parse(text)
    m = RE_FULL.search(t)
    if m:
        y, mo, d = int(m.group(1)), int(m.group(2)), int(m.group(3))
        h, mi = int(m.group(4)), int(m.group(5))
        s = int(m.group(6)) if m.group(6) else 0
        frac = m.group(7) or ""
        ms = int(frac.ljust(3, "0")[:3]) if frac else 0
        if _valid(y, mo, d, h, mi, s):
            try:
                return datetime.datetime(y, mo, d, h, mi, s), ms, "full"
            except ValueError:
                pass
    m = RE_NO_YEAR.search(t)
    if m:
        ym = RE_FILENAME_YEAR.search(filename)
        if ym:
            y = int(ym.group(1))
            mo, d = int(m.group(1)), int(m.group(2))
            h, mi, s = int(m.group(3)), int(m.group(4)), int(m.group(5))
            if _valid(y, mo, d, h, mi, s):
                try:
                    return datetime.datetime(y, mo, d, h, mi, s), 0, "noyear"
                except ValueError:
                    pass
    # RE_TIME_ONLY：OSD 无日期时，用文件名完整日期补齐（原设计为
    # reference-only；监控文件名 20260722_031301_me00060 自带可信日期）
    m = RE_TIME_ONLY.search(t)
    if m:
        fd = _file_full_date(filename)
        if fd:
            y, mo, d = fd
            h, mi, s = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if _valid(y, mo, d, h, mi, s):
                try:
                    return datetime.datetime(y, mo, d, h, mi, s), 0, "timeonly"
                except ValueError:
                    pass
    return None


def _search_crops(crops, filename, use_binary):
    """Scan crops; 命中分级：full（完整日期）恒优先于 noyear/timeonly
    （窄裁剪碎片如 "11.1 03:18:01" 常被错配月日，full 行才是可信证据）。
    full 命中即提前结束（控制成本）。"""
    best_full = None
    best_part = None
    for crop in crops:
        enhanced, binary = preprocess(crop)
        variant = binary if use_binary else enhanced
        for text, score in merge_ocr_lines(ocr_image(variant)):
            if score < MIN_OCR_SCORE:
                continue
            parsed = parse_timestamp(text, filename)
            if parsed is None:
                continue
            dt, ms, kind = parsed
            cand = {"conf": score, "dt": dt, "ms": ms, "rawText": text,
                    "kind": kind}
            if kind == "full":
                if best_full is None or score > best_full["conf"]:
                    best_full = cand
            elif best_part is None or score > best_part["conf"]:
                best_part = cand
        if best_full is not None:
            break  # full 命中即停（跨帧投票守护正确性）
    return best_full if best_full is not None else best_part


def ocr_frame(frame_path, filename, roi=None):
    """OCR one candidate frame. Cost-bounded adaptive chain:
      narrow crops + enhanced -> narrow + binary -> wide corner crops.
    Frames are downscaled to OCR_MAX_WIDTH first (det cost scales with
    pixels; 20px+ OSD text stays legible). Returns dict(best) or None.

    roi: (x0, y0, x1, y1) 归一化坐标（0~1，按帧尺寸换算；用户框选的时间戳
    区域）。指定时只识别该区域（放大 3 倍 + 增强），排除画面干扰。"""
    img = _imread_unicode(frame_path)
    if img is None:
        return None
    h, w = img.shape[:2]
    if roi is not None:
        x0 = max(0, min(int(roi[0] * w), w - 1))
        y0 = max(0, min(int(roi[1] * h), h - 1))
        x1 = max(x0 + 1, min(int(roi[2] * w), w))
        y1 = max(y0 + 1, min(int(roi[3] * h), h))
        crop = img[y0:y1, x0:x1]
        if crop.size == 0:
            return None
        # 放大 3 倍（时间戳字号小，放大后 OCR 更稳）
        crop = cv2.resize(crop, None, fx=3.0, fy=3.0,
                          interpolation=cv2.INTER_CUBIC)
        ch, cw = crop.shape[:2]
        if cw > OCR_MAX_WIDTH:
            crop = cv2.resize(crop,
                              (OCR_MAX_WIDTH, int(ch * OCR_MAX_WIDTH / cw)),
                              interpolation=cv2.INTER_AREA)
        best = _search_crops([crop], filename, use_binary=False)
        if best is None:
            best = _search_crops([crop], filename, use_binary=True)
        return best
    if w > OCR_MAX_WIDTH:
        img = cv2.resize(img, (OCR_MAX_WIDTH, int(h * OCR_MAX_WIDTH / w)),
                         interpolation=cv2.INTER_AREA)
    # 全帧优先：OSD 长行（含星期/通道前缀，如 2560 宽机身）常被 30% 窄裁剪
    # 切断；全帧 merge_ocr_lines 重组完整行，full-date 命中最可信。
    # 链：全帧 enhanced → 窄裁剪 enhanced → 窄裁剪 binary → 宽裁剪上角。
    # 任何 pass 出 full 即返回；都没有则取最优 partial（noyear/timeonly）。
    partial = None
    for crops, use_bin in (([img], False),
                           (crop_blocks(img), False),
                           (crop_blocks(img), True),
                           (crop_blocks(img, wide=True)[:2], False)):
        best = _search_crops(crops, filename, use_bin)
        if best is None:
            continue
        if best.get("kind") == "full":
            return best
        if partial is None or best["conf"] > partial["conf"]:
            partial = best
    return partial


def _ocr_frame_capped(frame_path, filename):
    """Cost-capped OCR for bonus frames: one full-frame enhanced pass only."""
    img = _imread_unicode(frame_path)
    if img is None:
        return None
    h, w = img.shape[:2]
    if w > OCR_MAX_WIDTH:
        img = cv2.resize(img, (OCR_MAX_WIDTH, int(h * OCR_MAX_WIDTH / w)),
                         interpolation=cv2.INTER_AREA)
    return _search_crops([img], filename, use_binary=False)


def vote(candidates):
    """Multi-frame voting (design §5.2.5). candidates: list of
    (rel_ms, ocr_dict). Returns (implied_wall_start_ms, conf, chosen) or
    (0, 0.0, None); chosen carries relMs/raw for evidence and wall math."""
    points = []
    for rel_ms, o in candidates:
        if o is None:
            continue
        wall = int(time.mktime(o["dt"].timetuple()) * 1000) + o["ms"]
        points.append({"implied": wall - rel_ms, "conf": o["conf"],
                       "relMs": rel_ms, "raw": o})
    if not points:
        return 0, 0.0, None
    # cluster implied wall-start within tolerance
    points.sort(key=lambda p: p["implied"])
    clusters = []
    for p in points:
        if clusters and abs(p["implied"] - clusters[-1][-1]["implied"]) <= VOTE_TOLERANCE_MS:
            clusters[-1].append(p)
        else:
            clusters.append([p])
    clusters.sort(key=lambda c: (len(c), max(p["conf"] for p in c)), reverse=True)
    top = clusters[0]
    chosen = max(top, key=lambda p: p["conf"])
    if len(top) >= 2:
        conf = 0.95
    elif chosen["conf"] >= SINGLE_HIT_MIN_SCORE:
        conf = 0.7
    else:
        return 0, 0.0, None  # single weak hit -> not trustworthy
    return chosen["implied"], conf, chosen


def ocr_side(cands, filename, roi=None):
    """OCR one side (head/tail) with incremental voting: stop as soon as two
    frames agree (halves the common-case inference count)."""
    points = []
    used = []
    for rel_ms, path in cands:
        o = ocr_frame(path, filename, roi)
        used.append((rel_ms, path))
        if o is not None:
            points.append((rel_ms, o))
        if len(points) >= 2:
            w, conf, chosen = vote(points)
            if w > 0 and conf >= 0.95:
                return w, conf, chosen, used
    w, conf, chosen = vote(points)
    return w, conf, chosen, used


# ---------------------------------------------------------------------------
# Per-file pipeline (runs inside worker process)
# ---------------------------------------------------------------------------
def process_file(video_path, ffmpeg_path, frame_dir, duration_ms):
    """Returns the per-file JSON dict (contract §6.4)."""
    os.makedirs(frame_dir, exist_ok=True)
    base = os.path.basename(video_path)
    out = {"file": video_path, "ok": False, "first": None, "last": None,
           "durationMs": duration_ms, "diag": {}}

    def grab(tag, ss=None, sseof=False, timeout=FFMPEG_HEAD_TIMEOUT_S):
        p = os.path.join(frame_dir, f"{tag}.png")
        el, actual_ms = extract_frame(ffmpeg_path, video_path, p, ss=ss,
                                      sseof=sseof, timeout=timeout)
        return (p, el, actual_ms)

    # --- first-frame candidates (relMs = MEASURED true position) ---
    head_cands, head_ms = [], 0
    for off in HEAD_OFFSETS_S:
        if duration_ms and off * 1000 >= duration_ms:
            continue
        p, el, actual = grab(f"head_{off}s", ss=off)
        if el >= 0:
            head_ms = max(head_ms, el)
            head_cands.append((actual if actual is not None and actual >= 0
                               else off * 1000, p))
    out["diag"]["headExtractMs"] = head_ms

    # --- last-frame candidates ---
    tail_cands, tail_ms = [], 0
    if duration_ms > 0:
        for off in TAIL_OFFSETS_S:
            ss = max(0.0, (duration_ms - off * 1000) / 1000.0)
            p, el, actual = grab(f"tail_{off}s", ss=ss,
                                 timeout=FFMPEG_TAIL_TIMEOUT_S)
            if el >= 0:
                tail_ms = max(tail_ms, el)
                tail_cands.append((actual if actual is not None and actual >= 0
                                   else duration_ms - off * 1000, p))
    # final-frame attempt (position unknowable -> bonus evidence, no voting)
    bonus_frame = ""
    p, el, _ = grab("tail_eof", sseof=True, timeout=FFMPEG_TAIL_TIMEOUT_S)
    if el >= 0:
        tail_ms = max(tail_ms, el)
        bonus_frame = p
    out["diag"]["tailExtractMs"] = tail_ms

    # --- OCR + incremental vote ---
    w_first, conf_first, ch_first, used_first = ocr_side(head_cands, base)
    w_last, conf_last, ch_last, used_last = ocr_side(tail_cands, base)

    # bonus: tail vote failed but eof frame exists -> low-confidence evidence
    # (cost-capped: single narrow-enhanced pass, no wide fallback)
    if w_last <= 0 and bonus_frame:
        o = _ocr_frame_capped(bonus_frame, base)
        if o is not None:
            wall = int(time.mktime(o["dt"].timetuple()) * 1000) + o["ms"]
            w_last, conf_last = wall, 0.5
            ch_last = {"relMs": 0, "raw": o}  # relMs unknown: wallMs only
            used_last = [(0, bonus_frame)]

    def side(implied_ms, conf, chosen, used):
        """wallMs semantics: implied wall-clock at stream rel 0 (head) /
        wall-clock of the chosen tail frame (tail). relMs = evidence frame
        position. Sorter uses first.wallMs + durationMs (design §5.3.3)."""
        if implied_ms <= 0 or chosen is None:
            return None
        frame_wall_ms = implied_ms + chosen["relMs"]
        dt = datetime.datetime.fromtimestamp(frame_wall_ms / 1000.0)
        img_path = ""
        for rel, path in used:
            if rel == chosen["relMs"]:
                img_path = path
                break
        return {
            "relMs": int(chosen["relMs"]),
            "text": chosen["raw"]["rawText"],
            "conf": round(conf, 3),
            "ts": dt.strftime("%Y-%m-%dT%H:%M:%S.") + f"{frame_wall_ms % 1000:03d}",
            "wallMs": int(frame_wall_ms),
            "impliedStartMs": int(implied_ms),
            "frameImg": img_path,
            "cropImg": "",
        }

    out["first"] = side(w_first, conf_first, ch_first, used_first)
    out["last"] = side(w_last, conf_last, ch_last, used_last)
    out["ok"] = out["first"] is not None
    if not out["ok"]:
        out["error"] = "ocr_all_failed"
    return out


def _worker(video_path):
    ctx = _work_ctx
    # per-file frame dir: parallel workers must never share jpg paths
    tag = hashlib.sha1(video_path.encode("utf-8")).hexdigest()[:12]
    frame_dir = os.path.join(ctx["frame_root"], tag)
    try:
        if video_path in ctx.get("frames_only", ()):
            res = process_file_frames_only(video_path, ctx["ffmpeg"],
                                           frame_dir,
                                           ctx["durations"].get(video_path, 0))
        else:
            res = process_file(video_path, ctx["ffmpeg"], frame_dir,
                               ctx["durations"].get(video_path, 0))
        res["frameTag"] = tag
    except Exception as e:
        import traceback
        res = {"file": video_path, "ok": False, "first": None, "last": None,
               "error": f"worker_exception:{e}",
               "trace": traceback.format_exc()[-800:]}
    if ctx.get("sha256"):
        res["sha256"] = sha256_file(video_path)
    return res


def _worker_at(video_path, positions, roi=None):
    """At-mode worker (v1.2.1 parallel): sample a file's requested positions.
    Process-local ctx; evidence dir is per-file (tag + '_at'), safe to share."""
    ctx = _work_ctx
    tag = hashlib.sha1(video_path.encode("utf-8")).hexdigest()[:12]
    frame_dir = os.path.join(ctx["frame_root"], tag + "_at")
    try:
        return process_file_at(video_path, ctx["ffmpeg"], frame_dir,
                               ctx["durations"].get(video_path, 0), positions,
                               roi)
    except Exception as e:
        import traceback
        return {"file": video_path, "ok": False, "samples": [],
                "error": f"worker_exception:{e}",
                "trace": traceback.format_exc()[-800:]}


def process_file_frames_only(video_path, ffmpeg_path, frame_dir, duration_ms):
    """Evidence frames without OCR (in-stream absolute time already trusted):
    one head frame + one tail frame, no inference at all."""
    os.makedirs(frame_dir, exist_ok=True)
    out = {"file": video_path, "ok": True, "framesOnly": True,
           "first": None, "last": None,
           "durationMs": duration_ms, "diag": {}}
    p1 = os.path.join(frame_dir, "head_1s.png")
    el, actual = extract_frame(ffmpeg_path, video_path, p1, ss=1)
    if el >= 0:
        out["first"] = {"relMs": int(actual or 1000), "text": "", "conf": 0,
                        "frameImg": p1, "cropImg": ""}
    p2 = os.path.join(frame_dir, "tail_eof.png")
    el, actual = extract_frame(ffmpeg_path, video_path, p2, sseof=True,
                               timeout=FFMPEG_TAIL_TIMEOUT_S)
    if el >= 0:
        out["last"] = {"relMs": int(actual or 0), "text": "", "conf": 0,
                       "frameImg": p2, "cropImg": ""}
    return out


def process_file_at(video_path, ffmpeg_path, frame_dir, duration_ms, positions,
                    roi=None):
    """Calibration sampling mode (V1 plan §3.2, --at-json): for each requested
    stream position, extract ±0.25s candidate frames, OCR with incremental
    voting, and return ONE wall-clock sample per position.

    Sample semantics: relMs = MEASURED true position of the chosen evidence
    frame; wallMs = implied wall-start + relMs. Seek inaccuracy is corrected
    by the showinfo measurement inside extract_frame (same as head/tail mode).
    """
    os.makedirs(frame_dir, exist_ok=True)
    out = {"file": video_path, "ok": False, "samples": [],
           "durationMs": duration_ms}
    for pos in positions:
        pos = max(0, int(pos))
        cands = []
        for off_ms in (-250, 0, 250):
            ss = pos + off_ms
            if ss < 0:
                continue
            if duration_ms and ss >= duration_ms:
                continue
            p = os.path.join(frame_dir, f"at_{pos}_{off_ms:+d}.png")
            el, actual = extract_frame(ffmpeg_path, video_path, p,
                                       ss=ss / 1000.0)
            if el >= 0:
                cands.append((actual if actual is not None and actual >= 0
                              else ss, p))
        w, conf, chosen, used = ocr_side(cands, os.path.basename(video_path),
                                        roi)
        if w > 0 and chosen is not None:
            img = ""
            for rel, path in used:
                if rel == chosen["relMs"]:
                    img = path
                    break
            out["samples"].append({
                "relMs": int(chosen["relMs"]),
                "wallMs": int(w + chosen["relMs"]),
                "text": chosen["raw"]["rawText"],
                "conf": round(conf, 3),
                "frameImg": img,
            })
        else:
            out["samples"].append({"relMs": pos, "wallMs": 0,
                                   "error": "ocr_failed"})
    out["ok"] = any(s.get("wallMs", 0) > 0 for s in out["samples"])
    if not out["ok"]:
        out["error"] = "ocr_all_failed"
    return out


def _worker_init(ffmpeg_path, frame_root, durations, sha256,
                 frames_only=frozenset()):
    # 模型加载惰性化（首个 OCR 调用时）：纯 frames-only 批次不加载模型
    _work_ctx.update({"ffmpeg": ffmpeg_path, "frame_root": frame_root,
                      "durations": durations, "sha256": sha256,
                      "frames_only": set(frames_only)})


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="OSD timestamp OCR probe")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--ffmpeg-path", required=True)
    ap.add_argument("--work-dir", required=True)
    ap.add_argument("--workers", type=int,
                    default=max(2, min(8, os.cpu_count() or 4)))
    ap.add_argument("--duration-json", default="",
                    help="JSON object {file: trustedDurationMs} from C++ side")
    ap.add_argument("--with-sha256", action="store_true")
    ap.add_argument("--frames-only-json", default="",
                    help="JSON {framesOnly: [file]}: extract evidence frames "
                         "but skip OCR (in-stream absolute time already trusted)")
    ap.add_argument("--evidence-dir", default="",
                    help="persistent dir for evidence frames; default=temp")
    ap.add_argument("--at-json", default="",
                    help="calibration sampling mode: JSON {file: [posMs,...]}; "
                         "one wall-clock sample per position (V1 plan §3.2)")
    ap.add_argument("--roi-json", default="",
                    help="optional user-selected timestamp ROI: "
                         "JSON {file: [x0,y0,x1,y1]} in video pixels")
    args = ap.parse_args()

    if not os.path.isfile(args.ffmpeg_path):
        print(f"ERROR::ffmpeg not found: {args.ffmpeg_path}", file=sys.stderr)
        sys.exit(2)

    durations = {}
    if args.duration_json and os.path.isfile(args.duration_json):
        try:
            with open(args.duration_json, "r", encoding="utf-8") as f:
                durations = {os.path.normpath(k): int(v)
                             for k, v in json.load(f).items()}
        except (json.JSONDecodeError, ValueError) as e:
            print(f"WARNING:duration-json parse failed: {e}", file=sys.stderr)

    frames_only = set()
    if args.frames_only_json and os.path.isfile(args.frames_only_json):
        try:
            with open(args.frames_only_json, "r", encoding="utf-8") as f:
                frames_only = {os.path.normpath(p)
                               for p in json.load(f).get("framesOnly", [])}
        except (json.JSONDecodeError, ValueError, AttributeError) as e:
            print(f"WARNING:frames-only-json parse failed: {e}", file=sys.stderr)

    files = [os.path.normpath(f) for f in args.files]
    for f in files:
        if f not in durations:
            durations[f] = ffprobe_duration_ms(args.ffmpeg_path, f)

    tmp_root = tempfile.mkdtemp(prefix="lumenarc_ocr_")
    atexit.register(_cleanup_dir, tmp_root)
    frame_root = args.evidence_dir or tmp_root
    if args.evidence_dir:
        os.makedirs(args.evidence_dir, exist_ok=True)

    # ---- calibration sampling mode (--at-json) ----
    # Single process (positions are few; model lazy-loads once). Progress is
    # per position: PROGRESS:<done>|<total>|<pct>.
    at_positions = {}
    if args.at_json and os.path.isfile(args.at_json):
        try:
            with open(args.at_json, "r", encoding="utf-8") as f:
                at_positions = {os.path.normpath(k): [int(x) for x in v]
                                for k, v in json.load(f).items()}
        except (json.JSONDecodeError, ValueError, AttributeError) as e:
            print(f"WARNING:at-json parse failed: {e}", file=sys.stderr)

    rois = {}
    if args.roi_json and os.path.isfile(args.roi_json):
        try:
            with open(args.roi_json, "r", encoding="utf-8") as f:
                rois = {os.path.normpath(k): [float(x) for x in v]
                        for k, v in json.load(f).items()}
        except (json.JSONDecodeError, ValueError, AttributeError) as e:
            print(f"WARNING:roi-json parse failed: {e}", file=sys.stderr)

    if at_positions:
        # v1.2.1：按位置分片并行（单文件多位置也并行；证据帧文件名含
        # 位置，不同片不冲突）。200 点 4 worker ≈ 3min（串行 ~10min）。
        tasks = []
        for f in files:
            poss = at_positions.get(f, [])
            if not poss:
                continue
            n_chunks = max(1, min(args.workers, (len(poss) + 7) // 8))
            chunk = max(1, (len(poss) + n_chunks - 1) // n_chunks)
            for c in range(0, len(poss), chunk):
                tasks.append((f, poss[c:c + chunk]))
        total = sum(max(1, len(sub)) for _, sub in tasks)
        done = 0
        try:
            with futures.ProcessPoolExecutor(
                    max_workers=max(1, min(args.workers, len(tasks))),
                    initializer=_worker_init,
                    initargs=(args.ffmpeg_path, frame_root, durations,
                              False, frozenset())) as pool:
                futs = {pool.submit(_worker_at, f, sub, rois.get(f)): i
                        for i, (f, sub) in enumerate(tasks)}
                results = {}
                for fut in futures.as_completed(futs):
                    i = futs[fut]
                    f, sub = tasks[i]
                    try:
                        res = fut.result()
                    except Exception as e:
                        res = {"file": f, "ok": False, "samples": [],
                               "error": f"executor:{e}"}
                    if f not in results:
                        results[f] = {"file": f, "ok": False, "samples": [],
                                      "durationMs": res.get("durationMs", 0)}
                    results[f]["samples"] += res.get("samples", [])
                    results[f]["ok"] = results[f]["ok"] or res.get("ok", False)
                    if res.get("error") and not results[f].get("error"):
                        results[f]["error"] = res["error"]
                    done += max(1, len(sub))
                    if res.get("error") and not res.get("ok"):
                        print(f"WARNING:{f}:{res.get('error')}",
                              file=sys.stderr, flush=True)
                    print(f"PROGRESS:{done}|{total}|{done * 100.0 / total:.1f}",
                          file=sys.stderr, flush=True)
                ordered = [results.get(f, {"file": f, "ok": False,
                                           "samples": [],
                                           "error": "no_result"})
                           for f in files]
        finally:
            if not args.evidence_dir:
                _cleanup_dir(tmp_root)
        json.dump(ordered, sys.stdout, ensure_ascii=False)
        sys.stdout.write("\n")
        return

    results = {}
    total = len(files)
    try:
        workers = max(1, min(args.workers, total))
        with futures.ProcessPoolExecutor(
                max_workers=workers,
                initializer=_worker_init,
                initargs=(args.ffmpeg_path, frame_root, durations,
                          args.with_sha256, frames_only)) as pool:
            futs = {pool.submit(_worker, f): f for f in files}
            done = 0
            for fut in futures.as_completed(futs):
                f = futs[fut]
                try:
                    res = fut.result()
                except Exception as e:
                    res = {"file": f, "ok": False, "first": None,
                           "last": None, "error": f"executor:{e}"}
                results[f] = res
                done += 1
                if not res.get("ok"):
                    print(f"ERROR:{f}:{res.get('error', 'unknown')}",
                          file=sys.stderr, flush=True)
                print(f"PROGRESS:{done}|{total}|{done * 100.0 / total:.1f}",
                      file=sys.stderr, flush=True)
    finally:
        if not args.evidence_dir:
            _cleanup_dir(tmp_root)

    # stable output order = input order
    ordered = [results.get(f, {"file": f, "ok": False, "first": None,
                               "last": None, "error": "no_result"})
               for f in files]
    json.dump(ordered, sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")


def _force_utf8_io():
    """Child-process IO contract with the C++ engine is UTF-8. On Chinese
    Windows the embeddable Python defaults stdout/stderr to GBK, which both
    garbles PROGRESS/ERROR lines containing Chinese paths and can crash
    subprocess readers (text=True uses locale codec). Force UTF-8."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass


if __name__ == "__main__":
    _force_utf8_io()
    main()
