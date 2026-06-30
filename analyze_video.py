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
from multiprocessing import Pool

SCENE_CHANGE_THRESHOLD = 30.0
MAX_ANALYSIS_FRAMES = 5000


def check_fps(video_path):
    """Return video FPS + total frames and exit."""
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        print(f"ERROR:Failed to open video: {video_path}", file=sys.stderr)
        sys.exit(1)
    fps = cap.get(cv2.CAP_PROP_FPS)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    cap.release()
    if fps <= 0:
        fps = 30.0
    print(json.dumps({"fps": fps, "total_frames": total_frames}, separators=(',', ':')))


def analyze_luminance(video_path, rois, start_frame=None, end_frame=None):
    """Analyze luminance for given ROIs over frame range. Returns dict, or None on error.

    NOTE (B9): Returns None instead of sys.exit() so this is safe to call from
    multiprocessing workers (sys.exit in a worker aborts the whole Pool).
    Callers in the main process should check for None and sys.exit() themselves.
    """
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

    # Apply frame range; for MKV and other containers where CAP_PROP_FRAME_COUNT
    # may be unreliable, use end_frame=None to read until EOF.
    effective_start = start_frame if start_frame is not None else 0
    effective_start = max(0, effective_start)

    # Seek to start frame
    if effective_start > 0:
        cap.set(cv2.CAP_PROP_POS_FRAMES, effective_start)

    # Pre-scan: count actual readable frames for step calculation
    # Use total_frames as hint but don't rely on it for loop termination
    estimated_count = total_frames - effective_start if total_frames > effective_start else 5000
    if estimated_count <= 0:
        estimated_count = 5000  # fallback estimate

    frame_step = max(1, estimated_count // MAX_ANALYSIS_FRAMES) if estimated_count > MAX_ANALYSIS_FRAMES else 1

    timestamps = []
    luminances = [[] for _ in rois]
    frame_idx = effective_start
    analyzed_count = 0
    prev_gray = None

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        # Honor end_frame if explicitly provided
        if end_frame is not None and frame_idx >= end_frame:
            break

        if (frame_idx - effective_start) % frame_step != 0:
            frame_idx += 1
            continue

        ts = (frame_idx / fps) * 1000.0
        timestamps.append(ts)

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        # Scene change detection (unused, kept for metadata)
        if prev_gray is not None and SCENE_CHANGE_THRESHOLD > 0:
            diff = float(cv2.mean(cv2.absdiff(gray, prev_gray))[0])
            if diff > SCENE_CHANGE_THRESHOLD and frame_step > 1:
                pass
        prev_gray = gray

        for i, roi in enumerate(rois):
            roi_type = roi.get("type", "rect")

            if roi_type == "polygon":
                # 多边形ROI：使用mask计算亮度
                points = roi.get("points", [])
                if len(points) < 3:
                    luminances[i].append(0.0)
                    continue

                mask = np.zeros(gray.shape, dtype=np.uint8)
                pts = np.array(points, dtype=np.int32)
                cv2.fillPoly(mask, [pts], 255)

                # 计算mask区域的平均亮度
                masked_pixels = gray[mask > 0]
                if masked_pixels.size > 0:
                    luminances[i].append(float(np.mean(masked_pixels)))
                else:
                    luminances[i].append(0.0)
            else:
                # 矩形ROI（原有逻辑）
                x, y, w, h = roi["x"], roi["y"], roi["w"], roi["h"]
                x1 = max(0, x)
                y1 = max(0, y)
                x2 = min(width, x + w)
                y2 = min(height, y + h)

                if x2 <= x1 or y2 <= y1:
                    luminances[i].append(0.0)
                else:
                    patch = gray[y1:y2, x1:x2]
                    if patch.size > 10000:
                        patch = cv2.resize(patch, (patch.shape[1] // 2, patch.shape[0] // 2))
                    luminances[i].append(float(np.mean(patch)))

        frame_idx += 1
        analyzed_count += 1

        if analyzed_count % 100 == 0:
            pct = min(100.0, (frame_idx / max(1, total_frames)) * 100)
            print(f"PROGRESS:{frame_idx}|{total_frames}|{pct:.1f}", file=sys.stderr, flush=True)

    cap.release()

    # Report 100% at end of analysis
    print(f"PROGRESS:{total_frames}|{total_frames}|100.0", file=sys.stderr, flush=True)

    # Update total_frames with actual count
    actual_total = frame_idx

    return {
        "timestamps": timestamps,
        "luminances": luminances,
        "roi_ids": [roi.get("roi_id", -1) for roi in rois],
        "frame_step": frame_step,
        "total_frames": actual_total,
        "fps": fps
    }


def _segment_worker(args):
    """Worker for multiprocessing luminance analysis."""
    video_path, rois_json, start_frame, end_frame = args
    rois = json.loads(rois_json)
    return analyze_luminance(video_path, rois, start_frame, end_frame)


def merge_segment_results(results):
    """Merge multiple segment results into one."""
    # M7: Filter out empty segments
    results = [r for r in results if r.get("timestamps")]

    if not results:
        return {"timestamps": [], "luminances": [], "roi_ids": [], "frame_step": 1, "total_frames": 0, "fps": 30.0}
    if len(results) == 1:
        return results[0]

    # Sort by start timestamp
    results.sort(key=lambda r: r["timestamps"][0])

    merged_ts = []
    merged_lum = [[] for _ in results[0]["luminances"]]
    for r in results:
        merged_ts.extend(r["timestamps"])
        for i, lum in enumerate(r["luminances"]):
            merged_lum[i].extend(lum)

    return {
        "timestamps": merged_ts,
        "luminances": merged_lum,
        "roi_ids": results[0].get("roi_ids", []),
        "frame_step": results[0]["frame_step"],
        "total_frames": results[0]["total_frames"],
        "fps": results[0]["fps"]
    }


def find_ffmpeg(ffmpeg_path=None):
    """Find ffmpeg executable."""
    if ffmpeg_path and os.path.isfile(ffmpeg_path):
        return ffmpeg_path
    # Check script directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    for candidate in [os.path.join(script_dir, "ffmpeg", "ffmpeg.exe"),
                      os.path.join(script_dir, "ffmpeg.exe")]:
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

def extract_audio(video_path, ffmpeg_path, sr=16000):
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

    cmd = [ffmpeg_path, "-i", video_path, "-vn", "-ac", "1",
           "-ar", str(sr), "-f", "wav", "-y", tmp.name]
    result = subprocess.run(cmd, capture_output=True, timeout=300)
    if result.returncode != 0:
        print(f"WARNING:ffmpeg extraction failed: {result.stderr.decode('utf-8', errors='replace')}", file=sys.stderr)
        return None
    return tmp.name


def compute_spectrogram(audio, sr=16000, n_fft=1280, hop_length=512, progress_callback=None):
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


def compute_volume(audio, sr=16000, frame_length=2048, hop_length=512, progress_callback=None):
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


def reduce_noise_spectral(audio, sr=16000, n_fft=1280, hop_length=512, strength=1.0):
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


def analyze_audio(video_path, ffmpeg_path, sr=16000, n_fft=1280, hop_length=512, noise_reduction=0):
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
            "time_resolution_ms": round(time_res_ms, 1),
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
                         start_frame=None, end_frame=None, report_prefix=""):
    """Analyze one video's luminance (multi-process aware).

    Returns a dict with keys: timestamps, luminances, frame_step, total_frames, fps.
    Returns None if luminance analysis yielded nothing.
    `report_prefix` is prepended to PROGRESS stderr lines (e.g. "[2/3] ").
    """
    # --- Luminance analysis ---
    if processes > 1:
        # Get total frames for splitting
        cap = cv2.VideoCapture(video_path)
        if not cap.isOpened():
            print(f"ERROR:Failed to open video: {video_path}", file=sys.stderr)
            return None
        total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        cap.release()

        start = start_frame if start_frame is not None else 0
        end = end_frame if end_frame is not None else total

        # Split into segments
        seg_size = max(1, (end - start) // processes)
        segments = []
        rois_json_str = json.dumps(rois, separators=(',', ':'))
        for i in range(processes):
            s = start + i * seg_size
            e = start + (i + 1) * seg_size if i < processes - 1 else end
            segments.append((video_path, rois_json_str, s, e))

        with Pool(processes) as pool:
            results = pool.map(_segment_worker, segments)
        # B10: workers can't emit PROGRESS back through Pool, so report per-segment
        # completion here as an approximation.
        done = 0
        merged = None
        for seg in results:
            done += 1
            pct = done * 100.0 / len(segments)
            print(f"PROGRESS:{report_prefix}{int(pct)}|{len(segments)}|{pct:.1f}", file=sys.stderr, flush=True)
        result = merge_segment_results(results)
    else:
        result = analyze_luminance(video_path, rois, start_frame, end_frame)

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
        prefix = f"[{idx + 1}/{n}] "
        single = analyze_single_video(vp, rois, ffmpeg, processes,
                                      report_prefix=prefix)
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

        # Advance offset by this video's duration
        tf = single.get("total_frames", 0)
        offset_ms += (tf / fps) * 1000.0

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
            "sample_rate": 44100,
            "hop_length": 512,
            "n_fft": 2048,
            "time_resolution_ms": round(1000.0 * 512 / 44100, 1),
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
    print(f"PROGRESS:{len(result['timestamps'])}|{result['total_frames']}|100.0", file=sys.stderr, flush=True)

    json_str = json.dumps(result, separators=(',', ':'))
    print(json_str)


if __name__ == "__main__":
    main()
