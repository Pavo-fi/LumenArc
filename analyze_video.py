#!/usr/bin/env python3
"""
Offline video luminance + audio analyzer - v0.3.
Reads a video frame-by-frame using OpenCV, computes average luminance
for each ROI, extracts audio via ffmpeg, computes spectrogram and volume.
Outputs compact JSON to stdout.

Usage:
    python analyze_video.py <video_path> <roi_json> [options]

roi_json: [{"x": int, "y": int, "w": int, "h": int}, ...]
Output: {"timestamps": [ms, ...], "luminances": [[...], ...], "fps": 30.0,
         "audio": {"volume": [...], "spectrogram": [[...], ...], ...}}
"""

import sys
import os
import json
import argparse
import subprocess
import tempfile
import cv2
import numpy as np
import queue
import re
import threading
import time

SCENE_CHANGE_THRESHOLD = 30.0
MAX_ANALYSIS_FRAMES = 5000


def check_fps(video_path):
    """Return video FPS + total frames and exit.

    元数据 fps（cv2.CAP_PROP_FPS）在部分 DVR 导出文件上翻倍/错误
    （如实际 15fps 报 30fps），故实测前 48 帧的 PTS 节奏校准。"""
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print(f"ERROR:Failed to open video: {video_path}", file=sys.stderr)
        sys.exit(1)
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    # 实测 PTS 节奏：grab 不解码，POS_MSEC 取每帧呈现时间戳
    pts = []
    for _ in range(48):
        if not cap.grab():
            break
        ms = cap.get(cv2.CAP_PROP_POS_MSEC)
        if ms and ms > 0:
            pts.append(ms)
    cap.release()
    if fps <= 0:
        fps = 30.0
    pts = sorted(set(pts))
    if len(pts) >= 8:
        deltas = sorted(b - a for a, b in zip(pts, pts[1:]) if b - a > 0.5)
        if deltas:
            measured = 1000.0 / deltas[len(deltas) // 2]
            if 3.0 <= measured <= 240.0 and abs(measured - fps) / fps > 0.04:
                fps = measured
    print(json.dumps({"fps": fps, "total_frames": total_frames}, separators=(',', ':')))


def _probe_video(video_path):
    """快速探测：fps（含 PTS 节奏校准，DVR 文件元数据常翻倍/错误）、总帧数、分辨率、时长。

    复用 check_fps 的实测逻辑：grab 不解码，取前 48 帧呈现时间戳的中位间隔。"""
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"cannot open video: {video_path}")
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    pts = []
    for _ in range(48):
        if not cap.grab():
            break
        ms = cap.get(cv2.CAP_PROP_POS_MSEC)
        if ms and ms > 0:
            pts.append(ms)
    cap.release()
    if fps <= 0:
        fps = 30.0
    pts = sorted(set(pts))
    if len(pts) >= 8:
        deltas = sorted(b - a for a, b in zip(pts, pts[1:]) if b - a > 0.5)
        if deltas:
            measured = 1000.0 / deltas[len(deltas) // 2]
            if 3.0 <= measured <= 240.0 and abs(measured - fps) / fps > 0.04:
                fps = measured
    dur_ms = (total_frames / fps) * 1000.0 if total_frames > 0 else 0.0
    return fps, total_frames, width, height, dur_ms


class _ProgressReporter:
    """统一进度协议：PROGRESS:<已分析采样数>|<估计总采样数>|<pct>。

    三个字段的语义在所有路径一致（ffmpeg 分块/cv2 回退/多视频）；
    pct 按全局样本数计算且单调递增。多视频场景通过 base 累加前序视频的实际样本数，
    并将 auto_total 置 False（总数由外层统一预估）。"""
    def __init__(self, total_est=0, base=0, auto_total=True, enabled=True):
        self.total = max(1, int(total_est))
        self.base = int(base)
        self.auto_total = auto_total
        self.enabled = enabled
        self._last_pct = -1.0

    def set_total(self, total_est):
        self.total = max(1, int(total_est))

    def update(self, done_local):
        if not self.enabled:
            return
        done = self.base + int(done_local)
        pct = min(99.0, done * 100.0 / self.total)
        if pct <= self._last_pct:
            return
        self._last_pct = pct
        print(f"PROGRESS:{done}|{self.total}|{pct:.1f}", file=sys.stderr, flush=True)


_PTS_RE = re.compile(rb"pts_time:([-\d.]+)")
_DIMS_RE = re.compile(rb"\bs:(\d+)x(\d+)")


class _FfmpegGrayPipe:
    """ffmpeg 子进程管道：时间段 → select 预抽帧 → 灰度 rawvideo + showinfo 真实 PTS。

    select 滤镜在像素导出前抽样（Python 只处理需要的帧）；showinfo 置于 select 之后，
    stderr 每行 pts_time 与 stdout 每帧字节严格一一对应（-t 置于输入侧保证末尾无悬挂行）。
    pts_time 以 -ss 点为基准（输入 seek 后时间戳重定零），绝对时间 = t0_ms + pts_time。"""
    def __init__(self, ffmpeg, video_path, t0_ms, dur_ms, step_frames, decode_threads=0):
        self.ffmpeg = ffmpeg
        self.video_path = video_path
        self.t0_ms = t0_ms
        self.dur_ms = dur_ms
        self.step_frames = max(1, int(step_frames))
        self.decode_threads = max(0, int(decode_threads))  # 0=ffmpeg 默认(auto)
        self.proc = None
        self.meta_q = queue.Queue()
        self._thread = None

    def _args(self):
        a = [self.ffmpeg, "-hide_banner", "-nostdin", "-nostats"]
        if self.t0_ms > 0.5:
            a += ["-ss", f"{self.t0_ms / 1000.0:.3f}"]
        if self.dur_ms > 0:
            a += ["-t", f"{self.dur_ms / 1000.0:.3f}"]
        if self.decode_threads > 0:
            a += ["-threads", str(self.decode_threads)]
        a += ["-i", self.video_path,
              "-an", "-sn", "-dn",
              "-vf", f"select='not(mod(n\\,{self.step_frames}))',showinfo",
              "-fps_mode", "passthrough",
              "-f", "rawvideo", "-pix_fmt", "gray", "-"]
        return a

    def start(self):
        self.proc = subprocess.Popen(
            self._args(), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self._thread = threading.Thread(target=self._drain_stderr, daemon=True)
        self._thread.start()

    def _drain_stderr(self):
        buf = b""
        try:
            stream = self.proc.stderr
            while True:
                chunk = stream.read1(65536)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    pm = _PTS_RE.search(line)
                    if not pm:
                        continue
                    dm = _DIMS_RE.search(line)
                    if not dm:
                        continue
                    self.meta_q.put((float(pm.group(1)), int(dm.group(1)), int(dm.group(2))))
        except Exception:
            pass
        finally:
            self.meta_q.put(None)

    def frames(self):
        """yield (abs_pts_ms, gray_ndarray)。dims 取自首帧 showinfo，中途变化则截断。"""
        w = h = None
        while True:
            try:
                meta = self.meta_q.get(timeout=120)
            except queue.Empty:
                raise RuntimeError("showinfo metadata timeout")
            if meta is None:
                break
            pts_sec, mw, mh = meta
            if w is None:
                w, h = mw, mh
            elif (mw, mh) != (w, h):
                break
            data = self._read_exact(w * h)
            if data is None:
                break
            yield self.t0_ms + pts_sec * 1000.0, \
                np.frombuffer(data, dtype=np.uint8).reshape(h, w)

    def _read_exact(self, n):
        out = self.proc.stdout
        chunks = []
        got = 0
        while got < n:
            c = out.read(n - got)
            if not c:
                return None
            chunks.append(c)
            got += len(c)
        return b"".join(chunks)

    def close(self):
        if self.proc:
            try:
                self.proc.stdout.close()
            except Exception:
                pass
            try:
                self.proc.wait(timeout=10)
            except Exception:
                self.proc.kill()
            self.proc = None


def _build_roi_masks(rois, out_w, out_h, orig_w, orig_h):
    """按输出分辨率预构建 ROI（矩形→切片坐标，多边形→fillPoly mask，一次到位）。"""
    sx = out_w / max(1, orig_w)
    sy = out_h / max(1, orig_h)
    masks = []
    for roi in rois:
        if roi.get("type") == "polygon":
            mask = np.zeros((out_h, out_w), dtype=np.uint8)
            points = roi.get("points", [])
            if len(points) >= 3:
                pts = np.array([[int(round(p[0] * sx)), int(round(p[1] * sy))]
                                for p in points], dtype=np.int32)
                cv2.fillPoly(mask, [pts], 255)
            masks.append(("mask", mask))
        else:
            x1 = max(0, int(round(roi["x"] * sx)))
            y1 = max(0, int(round(roi["y"] * sy)))
            x2 = min(out_w, int(round((roi["x"] + roi["w"]) * sx)))
            y2 = min(out_h, int(round((roi["y"] + roi["h"]) * sy)))
            masks.append(("rect", (x1, y1, x2, y2)))
    return masks


def _roi_mean(gray, m):
    kind, v = m
    if kind == "rect":
        x1, y1, x2, y2 = v
        if x2 <= x1 or y2 <= y1:
            return 0.0
        return float(np.mean(gray[y1:y2, x1:x2]))
    if not v.any():
        return 0.0
    return float(cv2.mean(gray, mask=v)[0])


def _ffmpeg_chunk_worker(idx, video_path, rois, ffmpeg, t0_ms, t1_ms, step_frames,
                         orig_w, orig_h, shared, results, decode_threads=0):
    """线程 worker：一条 ffmpeg 管道分析 [t0,t1)，抽样已在 select 滤镜完成。"""
    try:
        pipe = _FfmpegGrayPipe(ffmpeg, video_path, t0_ms, t1_ms - t0_ms, step_frames,
                               decode_threads)
        ts = []
        lum = [[] for _ in rois]
        masks = None
        pipe.start()
        for pts_ms, gray in pipe.frames():
            if masks is None:
                masks = _build_roi_masks(rois, gray.shape[1], gray.shape[0], orig_w, orig_h)
            ts.append(pts_ms)
            for k, m in enumerate(masks):
                lum[k].append(_roi_mean(gray, m))
            shared["samples"][idx] = len(ts)
        pipe.close()
        results[idx] = (ts, lum)
    except Exception as e:
        print(f"NOTE:chunk {idx} failed: {e}", file=sys.stderr, flush=True)


def _analyze_luminance_ffmpeg(video_path, rois, ffmpeg, processes,
                              start_frame, end_frame, reporter):
    """ffmpeg 管道快速路径：分块并行解码 + select 预抽样 + 灰度直出。

    解码由 ffmpeg 多线程完成（实测 SD 源 ~9000fps），Python 只处理抽样后的少量帧；
    时间戳取 showinfo 真实 PTS，与容器元数据 fps 无关（DVR 文件元数据常错误）。"""
    fps, total_frames, width, height, dur_ms = _probe_video(video_path)
    if width <= 0 or height <= 0:
        raise RuntimeError("probe failed")

    t0 = max(0.0, (start_frame / fps) * 1000.0) if start_frame else 0.0
    t1 = dur_ms
    if end_frame is not None:
        end_ms = (end_frame / fps) * 1000.0
        t1 = min(t1, end_ms) if t1 > 0 else end_ms
    if t1 <= t0:
        raise RuntimeError("unknown duration or empty range")
    span = t1 - t0

    interval_ms = max(1.0, span / MAX_ANALYSIS_FRAMES)
    step_frames = max(1, int(round(interval_ms * fps / 1000.0)))

    # 分块：单块最短 5 分钟（ffmpeg 多线程解码已很快，过细分块只剩进程开销）
    n_chunks = 1
    if processes > 1 and span >= 300000.0:
        n_chunks = max(1, min(processes * 2, int(span // 300000.0)))

    # 解码线程配额：总线程数 ≈ CPU 核数 - 2（留核给主窗口播放/操作），
    # 防 n_chunks×auto 超额订阅卡死系统；分析进程已恢复正常优先级
    # （BELOW_NORMAL 在有后台负载的机器上会被饿死，现场反馈分析变慢）
    cores = max(4, (os.cpu_count() or 4) - 2)
    threads_per_chunk = max(1, cores // n_chunks)

    if reporter.auto_total:
        reporter.set_total(int(span / interval_ms) + n_chunks)

    shared = {"samples": [0] * n_chunks}
    results = {}
    threads = []
    for i in range(n_chunks):
        ct0 = t0 + span * i / n_chunks
        ct1 = t0 + span * (i + 1) / n_chunks
        th = threading.Thread(
            target=_ffmpeg_chunk_worker,
            args=(i, video_path, rois, ffmpeg, ct0, ct1, step_frames,
                  width, height, shared, results, threads_per_chunk),
            daemon=True)
        th.start()
        threads.append(th)

    while any(t.is_alive() for t in threads):
        reporter.update(sum(shared["samples"]))
        time.sleep(0.5)
    for t in threads:
        t.join()
    reporter.update(sum(shared["samples"]))

    if not results:
        raise RuntimeError("all ffmpeg chunks failed")
    if len(results) < n_chunks:
        print(f"NOTE:{n_chunks - len(results)} chunk(s) failed, partial result",
              file=sys.stderr, flush=True)

    all_ts = []
    all_lum = [[] for _ in rois]
    for i in sorted(results):
        cts, clum = results[i]
        all_ts.extend(cts)
        for k in range(len(rois)):
            all_lum[k].extend(clum[k])

    # 边界去重：分块相位差导致的近重复点（间隔 < 半个采样周期丢弃后者）
    if all_ts:
        order = sorted(range(len(all_ts)), key=lambda j: all_ts[j])
        dedup_ts = []
        dedup_lum = [[] for _ in rois]
        last = None
        for j in order:
            t = all_ts[j]
            if last is not None and t - last < interval_ms * 0.5:
                continue
            last = t
            dedup_ts.append(t)
            for k in range(len(rois)):
                dedup_lum[k].append(all_lum[k][j])
        all_ts, all_lum = dedup_ts, dedup_lum

    if not all_ts:
        raise RuntimeError("no frames decoded via ffmpeg pipe")

    return {
        "timestamps": all_ts,
        "luminances": all_lum,
        "roi_ids": [roi.get("roi_id", -1) for roi in rois],
        "frame_step": step_frames,
        "total_frames": total_frames if total_frames > 0 else int(span / 1000.0 * fps),
        "fps": fps
    }


def _analyze_luminance_cv2(video_path, rois, start_frame=None, end_frame=None, reporter=None):
    """OpenCV 逐帧顺序路径（回退）：ffmpeg 管道不可用时使用。

    相对旧实现的优化：跳过帧用 grab() 不做像素转换；多边形 mask 预计算一次；
    进度经统一 reporter 报告（语义=已分析采样数）。返回 None 表示打开失败。"""
    reporter = reporter or _ProgressReporter()
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print(f"ERROR:Failed to open video: {video_path}", file=sys.stderr)
        return None

    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    fps = cap.get(cv2.CAP_PROP_FPS)
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))

    if fps <= 0:
        fps = 30.0

    effective_start = max(0, start_frame if start_frame is not None else 0)
    if effective_start > 0:
        cap.set(cv2.CAP_PROP_POS_FRAMES, effective_start)

    estimated_count = total_frames - effective_start if total_frames > effective_start else 5000
    if estimated_count <= 0:
        estimated_count = 5000  # fallback estimate

    frame_step = max(1, estimated_count // MAX_ANALYSIS_FRAMES) if estimated_count > MAX_ANALYSIS_FRAMES else 1
    if reporter.auto_total:
        reporter.set_total((estimated_count + frame_step - 1) // frame_step)

    # ROI 预处理：矩形→切片坐标，多边形→mask（一次构建，循环内零分配）
    masks = _build_roi_masks(rois, width, height, width, height)

    timestamps = []
    luminances = [[] for _ in rois]
    frame_idx = effective_start
    analyzed_count = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        # Honor end_frame if explicitly provided
        if end_frame is not None and frame_idx >= end_frame:
            break

        ts = (frame_idx / fps) * 1000.0
        timestamps.append(ts)

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        for i, m in enumerate(masks):
            luminances[i].append(_roi_mean(gray, m))

        frame_idx += 1
        analyzed_count += 1

        if analyzed_count % 100 == 0:
            reporter.update(analyzed_count)

        # 跳过 frame_step-1 帧：grab 维持解码器状态但不做 BGR 像素转换
        for _ in range(frame_step - 1):
            if end_frame is not None and frame_idx >= end_frame:
                break
            if not cap.grab():
                break
            frame_idx += 1

    cap.release()
    reporter.update(analyzed_count)

    return {
        "timestamps": timestamps,
        "luminances": luminances,
        "roi_ids": [roi.get("roi_id", -1) for roi in rois],
        "frame_step": frame_step,
        "total_frames": frame_idx,
        "fps": fps
    }


def analyze_luminance(video_path, rois, ffmpeg=None, processes=1,
                      start_frame=None, end_frame=None, reporter=None):
    """亮度分析统一入口：ffmpeg 分块并行管道（快速路径）+ cv2 顺序回退。

    NOTE: 返回 None 而不是 sys.exit()，多视频场景由调用方检查。"""
    reporter = reporter or _ProgressReporter()
    if ffmpeg:
        try:
            result = _analyze_luminance_ffmpeg(video_path, rois, ffmpeg, processes,
                                               start_frame, end_frame, reporter)
            if result and result["timestamps"]:
                return result
        except Exception as e:
            print(f"NOTE:ffmpeg fast path unavailable, fallback to cv2: {e}",
                  file=sys.stderr, flush=True)
    return _analyze_luminance_cv2(video_path, rois, start_frame, end_frame, reporter)



def find_ffmpeg(ffmpeg_path=None):
    """Find ffmpeg executable."""
    if ffmpeg_path and os.path.isfile(ffmpeg_path):
        return ffmpeg_path
    # Check script directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    for candidate in [os.path.join(script_dir, "ffmpeg", "ffmpeg.exe"),
                      os.path.join(script_dir, "ffmpeg.exe"),
                      os.path.join(script_dir, "ffmpeg", "ffmpeg"),
                      os.path.join(script_dir, "ffmpeg")]:
        if os.path.isfile(candidate):
            return candidate
    # System PATH
    return "ffmpeg"


_temp_files = []

def _cleanup_temp_files():
    """Clean up all registered temp files."""
    for p in _temp_files:
        try:
            if os.path.exists(p):
                os.unlink(p)
        except OSError:
            pass
    _temp_files.clear()

def probe_stream_starts(video_path, ffmpeg_path):
    """返回 (video_start_s, audio_start_s)；探测失败返回 (None, None)。
    2026-08-13 取证对齐：分析 WAV 的时间零点 = 音频流起点；而播放引擎的
    时间零点 = 视频流起点（ffmpeg_video_engine m_startPtsMs）。当音频流
    晚于视频流起始（监控导出常见，可达 ~1s），曲线会整体提前一个恒定量 ——
    必须探测后在 WAV 头部补等量静音，使分析时间轴与播放时间轴同原点。"""
    import json as _json
    ffprobe = os.path.join(os.path.dirname(os.path.abspath(ffmpeg_path)),
                           "ffprobe.exe" if os.name == "nt" else "ffprobe")
    if not os.path.exists(ffprobe):
        ffprobe = "ffprobe"   # PATH 兜底
    try:
        r = subprocess.run(
            [ffprobe, "-v", "error", "-show_entries",
             "stream=codec_type,start_time:format=start_time",
             "-of", "json", video_path],
            capture_output=True, timeout=30)
        if r.returncode != 0:
            return None, None
        info = _json.loads(r.stdout.decode("utf-8", errors="replace"))
        v_start = a_start = None
        fmt_start = info.get("format", {}).get("start_time")
        for s in info.get("streams", []):
            st = s.get("start_time")
            if st is None:
                continue
            if s.get("codec_type") == "video" and v_start is None:
                v_start = float(st)
            elif s.get("codec_type") == "audio" and a_start is None:
                a_start = float(st)
        if v_start is None and fmt_start is not None:
            v_start = float(fmt_start)   # 与引擎 start_time 回退链一致
        return v_start, a_start
    except Exception:
        return None, None


def extract_audio(video_path, ffmpeg_path, sr=24000):
    """Extract audio as WAV using ffmpeg. Returns path to temp WAV file."""
    import atexit
    import signal
    tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
    tmp.close()
    # L3: Register cleanup for temp files
    _temp_files.append(tmp.name)
    atexit.register(_cleanup_temp_files)
    # Also handle SIGTERM/SIGINT for graceful cleanup
    for sig in (signal.SIGTERM, signal.SIGINT):
        try:
            signal.signal(sig, lambda s, f: (_cleanup_temp_files(), sys.exit(1)))
        except (OSError, ValueError):
            pass  # Some signals can't be caught

    # 音频流起始偏移补齐：音频晚于视频 >20ms 时在 WAV 头部补等量静音
    # （上限 30s 防病态元数据；音频早于视频不裁切，保留全部声音证据）
    af_args = []
    v_start, a_start = probe_stream_starts(video_path, ffmpeg_path)
    if v_start is not None and a_start is not None:
        delta_ms = (a_start - v_start) * 1000.0
        if delta_ms > 20.0:
            pad_ms = min(delta_ms, 30000.0)
            af_args = ["-af", f"adelay=delays={pad_ms:.1f}:all=1"]
            print(f"WARNING:audio stream starts {delta_ms:.0f}ms after video; "
                  f"padding {pad_ms:.0f}ms leading silence for timeline alignment",
                  file=sys.stderr)

    cmd = [ffmpeg_path, "-i", video_path, "-vn", "-ac", "1",
           "-ar", str(sr)] + af_args + ["-f", "wav", "-y", tmp.name]
    result = subprocess.run(cmd, capture_output=True, timeout=300)
    if result.returncode != 0:
        print(f"WARNING:ffmpeg extraction failed: {result.stderr.decode('utf-8', errors='replace')}", file=sys.stderr)
        return None
    return tmp.name


def compute_spectrogram(audio, sr=24000, n_fft=1920, hop_length=512, progress_callback=None):
    """STFT spectrogram with log scale. Returns [freq_bins, time_frames]."""
    window = np.hanning(n_fft)
    n_frames = 1 + (len(audio) - n_fft) // hop_length
    if n_frames <= 0:
        return np.zeros((n_fft // 2 + 1, 0)), 0.0, 0.0
    spectrogram = np.zeros((n_fft // 2 + 1, n_frames))
    report_interval = max(1, n_frames // 20)
    for i in range(n_frames):
        start = i * hop_length
        frame = audio[start:start + n_fft] * window
        spectrogram[:, i] = np.abs(np.fft.rfft(frame))
        if progress_callback and (i + 1) % report_interval == 0:
            progress_callback(i + 1, n_frames)
    spectrogram = np.log10(spectrogram + 1e-10)

    spec_min = float(spectrogram.min())
    spec_max = float(spectrogram.max())
    return spectrogram, spec_min, spec_max


def compute_volume(audio, sr=24000, frame_length=2048, hop_length=512, progress_callback=None):
    """RMS volume normalized to 0-1."""
    volumes = []
    total = max(1, (len(audio) - frame_length) // hop_length)
    report_interval = max(1, total // 20)  # Report every 5%
    count = 0
    for i in range(0, len(audio) - frame_length, hop_length):
        frame = audio[i:i + frame_length]
        volumes.append(np.sqrt(np.mean(frame ** 2)))
        count += 1
        if progress_callback and count % report_interval == 0:
            progress_callback(count, total)
    if not volumes:
        return []
    volumes = np.array(volumes)
    if volumes.max() > 0:
        volumes = volumes / volumes.max()
    return volumes.tolist()


def reduce_noise_spectral(audio, sr=24000, n_fft=1920, hop_length=512, strength=1.0):
    """Spectral gating noise reduction using numpy only."""
    window = np.hanning(n_fft)
    n_frames = 1 + (len(audio) - n_fft) // hop_length
    if n_frames <= 0:
        return audio

    # STFT
    stft = np.zeros((n_fft // 2 + 1, n_frames), dtype=np.complex128)
    for i in range(n_frames):
        start = i * hop_length
        frame = audio[start:start + n_fft] * window
        stft[:, i] = np.fft.rfft(frame)

    magnitude = np.abs(stft)
    phase = np.angle(stft)

    # Estimate noise profile (20th percentile per frequency bin)
    # Higher percentile = more aggressive noise estimation
    noise_profile = np.percentile(magnitude, 20, axis=1, keepdims=True)

    # Apply strength: scale noise threshold nonlinearly
    # strength 1.0 = standard, 2.0 = 2x threshold, 5.0 = 5x threshold
    noise_threshold = noise_profile * (1.0 + strength * 0.5)

    # Soft mask: suppress frequencies below noise threshold
    mask = np.maximum(0, (magnitude - noise_threshold) / (magnitude + 1e-10))
    # Lower floor to allow stronger suppression (0.01 = 1% signal preservation)
    mask = np.maximum(mask, 0.01)

    # Apply mask + inverse STFT (overlap-add)
    stft_clean = magnitude * mask * np.exp(1j * phase)
    audio_clean = np.zeros(len(audio))
    norm = np.zeros(len(audio))
    win_sq = window * window
    for i in range(n_frames):
        start = i * hop_length
        frame_clean = np.fft.irfft(stft_clean[:, i], n_fft) * window
        audio_clean[start:start + n_fft] += frame_clean
        norm[start:start + n_fft] += win_sq

    # Normalize (avoid division by zero)
    norm = np.maximum(norm, 1e-10)
    audio_clean /= norm

    return audio_clean


def analyze_audio(video_path, ffmpeg_path, sr=24000, n_fft=1920, hop_length=512, noise_reduction=0):
    """Full audio analysis: extract + spectrogram + volume. Returns dict or None."""
    # Phase 1/4: Extract audio (0-20%)
    print("PROGRESS:1|4|20.0", file=sys.stderr, flush=True)
    wav_path = extract_audio(video_path, ffmpeg_path, sr)
    if wav_path is None:
        return None

    try:
        # Phase 2/4: Read WAV (20-30%)
        print("PROGRESS:2|4|30.0", file=sys.stderr, flush=True)
        import wave
        with wave.open(wav_path, 'rb') as wf:
            n_frames = wf.getnframes()
            sample_width = wf.getsampwidth()
            raw = wf.readframes(n_frames)

        if sample_width == 2:
            audio = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0
        elif sample_width == 1:
            audio = np.frombuffer(raw, dtype=np.uint8).astype(np.float64) / 128.0 - 1.0
        else:
            print(f"WARNING:Unsupported sample width {sample_width}", file=sys.stderr)
            return None

        # Apply noise reduction if requested
        if noise_reduction > 0:
            audio = reduce_noise_spectral(audio, sr, n_fft, hop_length, noise_reduction)

        # Phase 3/4: Compute spectrogram (30-70%)
        def spec_progress(current, total):
            pct = 30.0 + (current / total) * 40.0
            print(f"PROGRESS:3|4|{pct:.1f}", file=sys.stderr, flush=True)
        
        spec, spec_min, spec_max = compute_spectrogram(audio, sr, n_fft, hop_length, spec_progress)
        
        # Phase 4/4: Compute volume (70-100%)
        def vol_progress(current, total):
            pct = 70.0 + (current / total) * 30.0
            print(f"PROGRESS:4|4|{pct:.1f}", file=sys.stderr, flush=True)
        
        vol = compute_volume(audio, sr, n_fft, hop_length, vol_progress)
        time_res_ms = (hop_length / sr) * 1000.0

        # Adjust time resolution if spectrogram was downsampled
        n_spec_frames = spec.shape[1] if len(spec.shape) > 1 else 0
        if n_spec_frames > 0:
            actual_n_frames = 1 + (len(audio) - n_fft) // hop_length

        # Write spectrogram to temp binary file (float64, row-major)
        # This avoids slow spec.tolist() + huge JSON serialization
        spec_file = tempfile.NamedTemporaryFile(suffix=".spec", delete=False)
        spec_file.close()
        spec.astype(np.float64).tofile(spec_file.name)

        return {
            "volume": vol,
            "spectrogram_file": spec_file.name,
            "spectrogram_shape": [int(spec.shape[0]), int(spec.shape[1])],
            "sample_rate": sr,
            "hop_length": hop_length,
            "n_fft": n_fft,
            "time_resolution_ms": round(time_res_ms, 6),   # 全精度：曾 round(...,1)
            # （21.3333→21.3，0.156% 漂移 ≈ 每 10 分钟 1s，光标与声音对不上）
            "spec_min": round(spec_min, 4),
            "spec_max": round(spec_max, 4)
        }
    except Exception as e:
        print(f"WARNING:Audio analysis failed: {e}", file=sys.stderr)
        return None
    finally:
        try:
            os.unlink(wav_path)
            if wav_path in _temp_files:
                _temp_files.remove(wav_path)
        except OSError:
            pass


def analyze_single_video(video_path, rois, ffmpeg, processes=1,
                         start_frame=None, end_frame=None, reporter=None):
    """Analyze one video's luminance（ffmpeg 分块并行管道 + cv2 回退）。

    Returns a dict with keys: timestamps, luminances, frame_step, total_frames, fps.
    Returns None if luminance analysis yielded nothing.
    """
    result = analyze_luminance(video_path, rois, ffmpeg, processes,
                               start_frame, end_frame, reporter)

    if not result or not result["timestamps"]:
        return None

    return result


def analyze_multi_videos(video_paths, rois, ffmpeg, processes=1):
    """Analyze multiple videos and concatenate them on a merged timeline.

    Each video's luminance timestamps and audio frames are offset by the
    accumulated duration (ms) of preceding videos, so the result is one
    continuous timeline. ROI set is shared across all videos.
    """
    n = len(video_paths)
    # 预估全局采样数（每视频 ≤ MAX_ANALYSIS_FRAMES），多视频共用一个 reporter，
    # pct 按全局样本比例单调推进，不再用 [i/n] 前缀（会破坏 C++ 端解析）
    total_est = 0
    for vp in video_paths:
        try:
            fps_pv, tf_pv, _w, _h, dur_pv = _probe_video(vp)
            if dur_pv > 0:
                total_est += min(MAX_ANALYSIS_FRAMES, max(1, int(dur_pv / 1000.0 * fps_pv)))
                continue
        except Exception:
            pass
        total_est += MAX_ANALYSIS_FRAMES
    reporter = _ProgressReporter(total_est, auto_total=False)

    merged_ts = []
    merged_lum = None
    merged_vol = []
    # Spectrogram stored as list-of-lists per freq bin, extended across videos
    merged_spec = None
    frame_step = 1
    fps = 30.0
    total_frames = 0
    offset_ms = 0.0  # accumulated start offset for current video
    n_roi = len(rois)

    for idx, vp in enumerate(video_paths):
        single = analyze_single_video(vp, rois, ffmpeg, processes,
                                      reporter=reporter)
        if not single:
            # Skip videos that failed; advance offset by probing duration via fps check
            cap = cv2.VideoCapture(vp)
            if cap.isOpened():
                fps_v = cap.get(cv2.CAP_PROP_FPS)
                tf = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
                if fps_v <= 0:
                    fps_v = 30.0
                offset_ms += (tf / fps_v) * 1000.0
                cap.release()
            continue

        # Shift this video's luminance timestamps by the accumulated offset
        shifted_ts = [t + offset_ms for t in single["timestamps"]]
        reporter.base += len(single["timestamps"])   # 全局进度按实际样本数累加
        merged_ts.extend(shifted_ts)

        if merged_lum is None:
            merged_lum = [list(region) for region in single["luminances"]]
        else:
            for i, region in enumerate(single["luminances"]):
                if i < len(merged_lum):
                    merged_lum[i].extend(region)

        # Shift + concatenate audio
        audio = single.get("audio", {})
        vol = audio.get("volume", [])
        if vol:
            time_res = audio.get("time_resolution_ms", 32.0)
            frame_offset = int(round(offset_ms / time_res))
            merged_vol.extend(vol)  # volume samples are dense; offset is implicit in timeline
        spec = audio.get("spectrogram", [])
        if spec:
            if merged_spec is None:
                merged_spec = [list(bin_vals) for bin_vals in spec]
            else:
                for f, bin_vals in enumerate(spec):
                    if f < len(merged_spec):
                        merged_spec[f].extend(bin_vals)

        # Metadata from first video
        if idx == 0:
            frame_step = single.get("frame_step", 1)
            fps = single.get("fps", 30.0)
        total_frames += single.get("total_frames", 0)

        # Advance offset by this video's duration, using THIS video's own fps
        tf = single.get("total_frames", 0)
        fps_v = single.get("fps", 0) or 30.0
        offset_ms += (tf / fps_v) * 1000.0

    if not merged_ts or merged_lum is None:
        return None

    result = {
        "timestamps": merged_ts,
        "luminances": merged_lum,
        "frame_step": frame_step,
        "total_frames": total_frames,
        "fps": fps,
    }

    # Attach merged audio if any video produced it
    if merged_vol or merged_spec:
        result["audio"] = {
            "volume": merged_vol,
            "spectrogram": merged_spec if merged_spec is not None else [],
            "sample_rate": 24000,
            "hop_length": 512,
            "n_fft": 1920,
            "time_resolution_ms": 1000.0 * 512 / 24000,   # 全精度，勿 round 到 0.1
        }

    return result


def main():
    parser = argparse.ArgumentParser(description="Video luminance + audio analyzer")
    parser.add_argument("video_path", nargs="?", help="Path to video file")
    parser.add_argument("roi_json", nargs="?", help="ROI JSON array")
    parser.add_argument("--videos", nargs="+", default=None,
                        help="Multiple video paths to analyze & merge into one timeline (B2)")
    parser.add_argument("--check-fps", action="store_true", help="Only output video FPS")
    parser.add_argument("--start-frame", type=int, default=None, help="Start frame index")
    parser.add_argument("--end-frame", type=int, default=None, help="End frame index")
    parser.add_argument("--audio-only", action="store_true", help="Only analyze audio")
    parser.add_argument("--include-audio", action="store_true",
                        help="Also run audio analysis alongside luminance analysis")
    parser.add_argument("--processes", type=int, default=1, help="Number of parallel processes")
    parser.add_argument("--ffmpeg-path", default=None, help="Path to ffmpeg executable")
    parser.add_argument("--noise-reduction", type=float, default=0, help="Noise reduction strength (0=off, 1=standard, 2=strong)")

    args = parser.parse_args()

    # --check-fps mode
    if args.check_fps:
        if not args.video_path:
            print("ERROR:--check-fps requires video_path", file=sys.stderr)
            sys.exit(1)
        check_fps(args.video_path)
        return

    ffmpeg = find_ffmpeg(args.ffmpeg_path)

    # --audio-only mode (does not require roi_json); single video only
    if args.audio_only:
        if not args.video_path:
            print("ERROR:audio-only requires video_path", file=sys.stderr)
            sys.exit(1)
        audio_result = analyze_audio(args.video_path, ffmpeg, noise_reduction=args.noise_reduction)
        result = {"audio": audio_result if audio_result else {}}
        json_str = json.dumps(result, separators=(',', ':'))
        print(f"PROGRESS:4|4|100.0", file=sys.stderr, flush=True)
        print(json_str)
        return

    # Determine the video set: --videos (multi) takes precedence, else single positional
    if args.videos:
        video_paths = args.videos
    elif args.video_path:
        video_paths = [args.video_path]
    else:
        print("ERROR:video_path (or --videos) is required", file=sys.stderr)
        sys.exit(1)

    # Luminance analysis requires roi_json
    if not args.roi_json:
        print("ERROR:roi_json is required for luminance analysis", file=sys.stderr)
        sys.exit(1)

    try:
        rois = json.loads(args.roi_json)
    except json.JSONDecodeError as e:
        print(f"ERROR:Invalid ROI JSON: {e}", file=sys.stderr)
        sys.exit(1)

    if not rois:
        print("ERROR:No ROIs specified", file=sys.stderr)
        sys.exit(1)

    # B2: Multi-video merge path
    if len(video_paths) > 1:
        result = analyze_multi_videos(video_paths, rois, ffmpeg, args.processes)
    else:
        result = analyze_single_video(video_paths[0], rois, ffmpeg, args.processes,
                                      args.start_frame, args.end_frame)

    if not result or not result["timestamps"]:
        print("ERROR:No frames were analyzed", file=sys.stderr)
        sys.exit(1)

    # --include-audio: also run audio analysis and attach to result
    if args.include_audio:
        audio_result = analyze_audio(video_paths[0], ffmpeg, noise_reduction=args.noise_reduction)
        if audio_result:
            result["audio"] = audio_result
        else:
            result["audio"] = {}

    # Report final progress
    print(f"PROGRESS:{len(result['timestamps'])}|{len(result['timestamps'])}|100.0", file=sys.stderr, flush=True)

    json_str = json.dumps(result, separators=(',', ':'))
    print(json_str)


if __name__ == "__main__":
    main()
