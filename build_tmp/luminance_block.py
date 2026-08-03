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
    def __init__(self, ffmpeg, video_path, t0_ms, dur_ms, step_frames):
        self.ffmpeg = ffmpeg
        self.video_path = video_path
        self.t0_ms = t0_ms
        self.dur_ms = dur_ms
        self.step_frames = max(1, int(step_frames))
        self.proc = None
        self.meta_q = queue.Queue()
        self._thread = None

    def _args(self):
        a = [self.ffmpeg, "-hide_banner", "-nostdin", "-nostats"]
        if self.t0_ms > 0.5:
            a += ["-ss", f"{self.t0_ms / 1000.0:.3f}"]
        if self.dur_ms > 0:
            a += ["-t", f"{self.dur_ms / 1000.0:.3f}"]
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
                         orig_w, orig_h, shared, results):
    """线程 worker：一条 ffmpeg 管道分析 [t0,t1)，抽样已在 select 滤镜完成。"""
    try:
        pipe = _FfmpegGrayPipe(ffmpeg, video_path, t0_ms, t1_ms - t0_ms, step_frames)
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
                  width, height, shared, results),
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
