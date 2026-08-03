#!/usr/bin/env python3
"""方案 B 验证套件：ffmpeg 管道亮度分析的正确性与性能验证。

验证手段：
  V1 路径一致性   新 ffmpeg 路径 vs cv2 回退路径（相关度/平均绝对误差）
  V2 Ground truth 全分辨率独立采样对比（抽样点的真实 ROI 均值）
  V3 并行一致性   processes=1 vs processes=8 结果等价
  V4 分块边界     相邻时间戳间隔分布（无重复/无缺口）
  V5 多文件鲁棒   桌面 DVR 文件批量：成功/单调/覆盖率
  V6 回退路径     伪造 ffmpeg 路径 → 自动回退 cv2 且结果正确
  V7 性能基准     cv2 顺序路径 vs ffmpeg 管道路径耗时
  V8 CLI 端到端   PROGRESS 协议 + JSON schema（模拟 C++ 调用）
"""
import json
import os
import subprocess
import sys
import time

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import analyze_video as av

FF = r"C:\code\LumenArc\LumenArc_v1.0 remake\build\Release\ffmpeg\ffmpeg.exe"
SCRIPT = r"C:\code\LumenArc\LumenArc_v1.0 remake\analyze_video.py"
VIDEO_DIR = r"C:\Users\MJ\Desktop\$RCR79YF"
MAIN_VIDEO = os.path.join(VIDEO_DIR, "02-04-52_6m.mp4")

ROIS = [
    {"type": "rect", "x": 100, "y": 100, "w": 200, "h": 150, "roi_id": 1},
    {"type": "rect", "x": 400, "y": 50, "w": 150, "h": 100, "roi_id": 3},
    {"type": "polygon", "points": [[50, 300], [300, 320], [400, 450], [80, 440]], "roi_id": 2},
]

passed, failed = [], []


def check(name, ok, detail=""):
    (passed if ok else failed).append(name)
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}  {detail}")


def full_res_masks(rois, w, h):
    return av._build_roi_masks(rois, w, h, w, h)


def ground_truth(video, rois, target_ts_ms, fps):
    """独立全分辨率顺序读取：每个目标时间戳取最近帧的 ROI 均值。"""
    cap = cv2.VideoCapture(video)
    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    masks = full_res_masks(rois, w, h)
    targets = sorted(target_ts_ms)
    half = 1000.0 / fps / 2
    out = [[] for _ in rois]
    ti = 0
    idx = 0
    while ti < len(targets):
        ret, frame = cap.read()
        if not ret:
            break
        if idx / fps * 1000.0 >= targets[ti] - half:
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            for k, m in enumerate(masks):
                out[k].append(av._roi_mean(gray, m))
            ti += 1
        idx += 1
    cap.release()
    return out, len(targets) - ti  # 值 + 未命中数


def align_pair(ts_a, val_a, ts_b, val_b, tol_ms):
    """按最近时间戳配对两路采样，返回配对值数组。"""
    pa, pb = [], []
    jb = 0
    for i, ta in enumerate(ts_a):
        while jb + 1 < len(ts_b) and abs(ts_b[jb + 1] - ta) <= abs(ts_b[jb] - ta):
            jb += 1
        if abs(ts_b[jb] - ta) <= tol_ms:
            pa.append(val_a[i])
            pb.append(val_b[jb])
    return np.array(pa), np.array(pb)


def corr_mae(a, b):
    if len(a) < 10:
        return 0.0, 999.0
    c = float(np.corrcoef(a, b)[0, 1])
    m = float(np.mean(np.abs(a - b)))
    return c, m


# ---------------------------------------------------------------- V1 路径一致性
print("V1 新 ffmpeg 路径 vs cv2 回退路径一致性")
rep1, rep2 = av._ProgressReporter(enabled=False), av._ProgressReporter(enabled=False)
r_new = av.analyze_luminance(MAIN_VIDEO, ROIS, FF, 4, reporter=rep1)
r_cv2 = av._analyze_luminance_cv2(MAIN_VIDEO, ROIS, reporter=rep2)
check("两路均产出样本", len(r_new["timestamps"]) > 100 and len(r_cv2["timestamps"]) > 100,
      f"new={len(r_new['timestamps'])} cv2={len(r_cv2['timestamps'])}")
step_ms_new = 1000.0 / r_new["fps"] * r_new["frame_step"]
for k, roi in enumerate(ROIS):
    a, b = align_pair(r_new["timestamps"], r_new["luminances"][k],
                      r_cv2["timestamps"], r_cv2["luminances"][k], step_ms_new)
    c, m = corr_mae(a, b)
    check(f"ROI{k}({roi['type']}) 相关度>0.99 且 MAE<3", c > 0.99 and m < 3.0,
          f"corr={c:.4f} MAE={m:.2f} pairs={len(a)}")

# ---------------------------------------------------------------- V2 ground truth
print("V2 Ground truth（全分辨率独立采样）")
sample_ts = [r_new["timestamps"][i] for i in
             np.linspace(0, len(r_new["timestamps"]) - 1, 12).astype(int)]
gt, missed = ground_truth(MAIN_VIDEO, ROIS, sample_ts, r_new["fps"])
check("ground truth 全部命中", missed == 0, f"missed={missed}")
idxs = [np.argmin([abs(t - s) for t in r_new["timestamps"]]) for s in sample_ts]
for k in range(len(ROIS)):
    a = np.array([r_new["luminances"][k][i] for i in idxs])
    b = np.array(gt[k])
    m = float(np.mean(np.abs(a - b)))
    check(f"ROI{k} vs GT MAE<3", m < 3.0, f"MAE={m:.2f}")

# ---------------------------------------------------------------- V3 并行一致性
print("V3 processes=1 vs processes=8")
rep3, rep4 = av._ProgressReporter(enabled=False), av._ProgressReporter(enabled=False)
r_p1 = av.analyze_luminance(MAIN_VIDEO, ROIS, FF, 1, reporter=rep3)
r_p8 = av.analyze_luminance(MAIN_VIDEO, ROIS, FF, 8, reporter=rep4)
n1, n8 = len(r_p1["timestamps"]), len(r_p8["timestamps"])
check("样本数接近（差<2%）", abs(n1 - n8) / max(n1, n8) < 0.02, f"p1={n1} p8={n8}")
check("覆盖范围一致", abs(r_p1["timestamps"][0] - r_p8["timestamps"][0]) < step_ms_new
      and abs(r_p1["timestamps"][-1] - r_p8["timestamps"][-1]) < step_ms_new,
      f"first={r_p1['timestamps'][0]:.0f}/{r_p8['timestamps'][0]:.0f} "
      f"last={r_p1['timestamps'][-1]:.0f}/{r_p8['timestamps'][-1]:.0f}")
a, b = align_pair(r_p1["timestamps"], r_p1["luminances"][0],
                  r_p8["timestamps"], r_p8["luminances"][0], step_ms_new)
c, m = corr_mae(a, b)
check("p1 vs p8 相关度>0.999", c > 0.999, f"corr={c:.5f} pairs={len(a)}")

# ---------------------------------------------------------------- V4 分块边界
print("V4 分块边界（时间戳间隔分布）")
d = np.diff(r_p8["timestamps"])
med = float(np.median(d))
check("间隔中位数 ≈ 采样周期", abs(med - step_ms_new) / step_ms_new < 0.15,
      f"median={med:.0f}ms step={step_ms_new:.0f}ms")
check("无重复点（最小间隔 > 0.3×周期）", float(d.min()) > step_ms_new * 0.3,
      f"min={d.min():.0f}ms")
gaps = int(np.sum(d > step_ms_new * 2.5))
check("缺口 ≤ 分块数+1", gaps <= 3, f"gaps={gaps} max={d.max():.0f}ms")

# ---------------------------------------------------------------- V5 多文件鲁棒
print("V5 桌面 DVR 文件批量")
files = sorted(f for f in os.listdir(VIDEO_DIR) if f.endswith(".mp4"))[:6]
for f in files:
    vp = os.path.join(VIDEO_DIR, f)
    rep = av._ProgressReporter(enabled=False)
    try:
        r = av.analyze_luminance(vp, ROIS, FF, 4, reporter=rep)
        ts = r["timestamps"]
        mono = all(b2 > a2 for a2, b2 in zip(ts, ts[1:]))
        fps_v, tf_v, _w, _h, dur_v = av._probe_video(vp)
        cover = ts[-1] / dur_v if dur_v > 0 else 0
        check(f, len(ts) > 100 and mono and cover > 0.95,
              f"samples={len(ts)} mono={mono} cover={cover*100:.1f}%")
    except Exception as e:
        check(f, False, str(e))

# ---------------------------------------------------------------- V6 回退路径
print("V6 回退路径（伪造 ffmpeg → cv2 fallback）")
rep6 = av._ProgressReporter(enabled=False)
r_fb = av.analyze_luminance(MAIN_VIDEO, ROIS, r"C:\nonexistent\ffmpeg.exe", 4, reporter=rep6)
a, b = align_pair(r_fb["timestamps"], r_fb["luminances"][0],
                  r_cv2["timestamps"], r_cv2["luminances"][0], step_ms_new)
c, m = corr_mae(a, b)
check("回退结果 == cv2 路径结果", c > 0.999 and len(r_fb["timestamps"]) == len(r_cv2["timestamps"]),
      f"corr={c:.5f} samples={len(r_fb['timestamps'])}")

# ---------------------------------------------------------------- V7 性能基准
print("V7 性能基准")
t0 = time.time(); av._analyze_luminance_cv2(MAIN_VIDEO, ROIS, reporter=av._ProgressReporter(enabled=False))
t_cv2 = time.time() - t0
t0 = time.time(); av.analyze_luminance(MAIN_VIDEO, ROIS, FF, 4, reporter=av._ProgressReporter(enabled=False))
t_ff = time.time() - t0
print(f"  [INFO] SD 720x480: cv2={t_cv2:.1f}s ffmpeg={t_ff:.1f}s speedup={t_cv2/max(0.01,t_ff):.1f}x")
# HD 1080p 才是长视频痛点场景：ROI 按分辨率放大
HD_VIDEO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hd_test.mp4")
if os.path.exists(HD_VIDEO):
    HD_ROIS = [
        {"type": "rect", "x": 267, "y": 225, "w": 533, "h": 337, "roi_id": 1},
        {"type": "rect", "x": 1067, "y": 112, "w": 400, "h": 225, "roi_id": 3},
        {"type": "polygon", "points": [[133, 675], [800, 720], [1067, 1012], [213, 990]], "roi_id": 2},
    ]
    t0 = time.time(); r_hd_cv2 = av._analyze_luminance_cv2(HD_VIDEO, HD_ROIS, reporter=av._ProgressReporter(enabled=False))
    t_hd_cv2 = time.time() - t0
    t0 = time.time(); r_hd_ff = av.analyze_luminance(HD_VIDEO, HD_ROIS, FF, 8, reporter=av._ProgressReporter(enabled=False))
    t_hd_ff = time.time() - t0
    hd_speedup = t_hd_cv2 / max(0.01, t_hd_ff)
    a, b = align_pair(r_hd_ff["timestamps"], r_hd_ff["luminances"][0],
                      r_hd_cv2["timestamps"], r_hd_cv2["luminances"][0], 200)
    c, m = corr_mae(a, b)
    check("HD 结果一致性 corr>0.99", c > 0.99, f"corr={c:.4f} MAE={m:.2f}")
    check("HD 加速比 ≥ 4x", hd_speedup >= 4.0,
          f"cv2={t_hd_cv2:.1f}s ffmpeg={t_hd_ff:.1f}s speedup={hd_speedup:.1f}x")
else:
    check("HD 测试片存在", False, HD_VIDEO)

# ---------------------------------------------------------------- V8 CLI 端到端
print("V8 CLI 端到端（模拟 C++ 调用）")
cmd = [sys.executable, SCRIPT, MAIN_VIDEO, json.dumps(ROIS),
       "--processes", "8", "--ffmpeg-path", FF]
p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
out, err = p.communicate(timeout=600)
check("exit code 0", p.returncode == 0, f"rc={p.returncode}")
prog = []
for line in err.decode("utf-8", "replace").splitlines():
    if line.startswith("PROGRESS:"):
        parts = line[9:].split("|")
        prog.append((int(parts[0]), int(parts[1]), float(parts[2])))
pcts = [x[2] for x in prog]
check("PROGRESS 行 ≥ 3", len(prog) >= 3, f"lines={len(prog)}")
check("pct 单调不减", all(b2 >= a2 for a2, b2 in zip(pcts, pcts[1:])), f"first={pcts[:5]}")
check("pct 范围 [0,100]", all(0 <= x <= 100 for x in pcts), "")
check("analyzed ≤ total", all(a2 <= b2 for a2, b2, _ in prog), f"last={prog[-1] if prog else None}")
check("最终 100%", prog and prog[-1][2] == 100.0, f"final={prog[-1] if prog else None}")
doc = json.loads(out.decode("utf-8"))
keys = {"timestamps", "luminances", "roi_ids", "frame_step", "total_frames", "fps"}
check("JSON schema 完整", keys.issubset(doc.keys()), f"missing={keys - doc.keys()}")
check("luminances 长度 == timestamps 长度",
      all(len(l) == len(doc["timestamps"]) for l in doc["luminances"]),
      f"ts={len(doc['timestamps'])}")
check("roi_ids 保持 [1,3,2]", doc["roi_ids"] == [1, 3, 2], f"{doc['roi_ids']}")

# ---------------------------------------------------------------- V9 多视频合并
print("V9 多视频合并（--videos，3 个文件）")
mv_files = [os.path.join(VIDEO_DIR, f) for f in files[:3]]
cmd = [sys.executable, SCRIPT, None, json.dumps(ROIS),
       "--videos"] + mv_files + ["--processes", "4", "--ffmpeg-path", FF]
cmd[2] = mv_files[0]   # 位置参数 video_path（--videos 优先）
p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
out, err = p.communicate(timeout=900)
check("多视频 exit code 0", p.returncode == 0, f"rc={p.returncode}")
prog = []
for line in err.decode("utf-8", "replace").splitlines():
    if line.startswith("PROGRESS:"):
        parts = line[9:].split("|")
        prog.append((int(parts[0]), int(parts[1]), float(parts[2])))
pcts = [x[2] for x in prog]
check("多视频 pct 单调不减", all(b2 >= a2 for a2, b2 in zip(pcts, pcts[1:])),
      f"lines={len(prog)}")
check("多视频 analyzed 单调不减", all(prog[i+1][0] >= prog[i][0] for i in range(len(prog)-1)), "")
doc = json.loads(out.decode("utf-8"))
ts = doc["timestamps"]
check("多视频时间戳单调", all(b2 > a2 for a2, b2 in zip(ts, ts[1:])), "")
check("多视频覆盖 ≈ 30 分钟", ts[-1] > 29 * 60 * 1000, f"last={ts[-1]/60000:.1f}min")
check("多视频最终 100%", prog and prog[-1][2] == 100.0, f"final={prog[-1] if prog else None}")

# ---------------------------------------------------------------- 汇总
print("\n" + "=" * 60)
print(f"PASS {len(passed)}  FAIL {len(failed)}")
if failed:
    print("失败项:", *failed, sep="\n  - ")
    sys.exit(1)
print("全部通过")
