#!/usr/bin/env python3
"""OSD 秒跳变探针 v4（P-98）——不用 OCR 做秒级对齐（含自校验）。

原理
----
1. 画面时间戳「秒」两位数字每次跳变都可从像素上看出来；跳变的**位置**用该帧的
   **精确 PTS**（ffprobe 逐帧），不受解码丢帧影响（本片丢 145 帧 ≈ 5.8 秒漂移）。
2. 跳变的**幅度**不靠猜：把每个跳变帧的秒位字模聚类（含全两位 → ~60 类），
   数字环序差 = 精确跳幅（加速导出会 +2/+3 跳秒）。
3. 绝对时间由**稀疏 OCR 锚点**标定（每锚点区间内按幅度微调），留出锚点做独立校验。

三阶段
------
  probe   : ffprobe 取逐帧 PTS            → pts.csv
  extract : 一趟解码取字模变化与跳变补丁  → c.npy / p.npy / pf.npy
  analyze : 聚类+定幅+标定+自校验         → map.json
"""
import argparse
import json
import subprocess
import sys
from collections import Counter, defaultdict

import numpy as np

MASK_TH = 160        # 白字阈值（OSD 文字纯白）
MIN_CHANGE = 20      # 字模变化像素数下限（1 像素级抖动必须滤掉）
MIN_SEP = 8          # 跳变最小间隔（帧）
CLUSTER_TOL_PX = 35  # 字模 XOR 差异**绝对像素数**阈值
                     # （比例阈值不可用：一个数字字形才 ~200 px，
                     #   0.12×2208=265 px 会把不同数字并成一类）
MAX_STEP = 6         # 合理的画面秒跳幅上限（超过视为误判）


def run_probe(args):
    out = subprocess.run([args.ffprobe, "-v", "error", "-select_streams", "v:0",
                          "-show_entries", "frame=pts_time", "-of", "csv=p=0",
                          args.video], capture_output=True, text=True)
    pts = [float(x) for x in out.stdout.split() if x.strip()]
    with open(args.pts, "w") as f:
        f.write("\n".join("%.6f" % v for v in pts))
    print("probe: %d 帧 PTS（%.3f ~ %.3f s，有效帧率 %.4f）"
          % (len(pts), pts[0], pts[-1], (len(pts) - 1) / (pts[-1] - pts[0])))


def run_extract(args):
    rx, ry, rw, rh = (int(v) for v in args.crop.split(","))
    cmd = [args.ffmpeg, "-hide_banner", "-loglevel", "quiet", "-i", args.video,
           "-vf", "crop=%d:%d:%d:%d,format=gray" % (rw, rh, rx, ry),
           "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "gray", "-"]
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    fsz = rw * rh
    changes, patches, pframes = [], [], []
    prev, carry, n = None, b"", 0
    while True:
        chunk = proc.stdout.read(fsz * 500)
        if not chunk:
            break
        buf = carry + chunk
        cnt = len(buf) // fsz
        if cnt == 0:
            carry = buf
            continue
        carry = buf[cnt * fsz:]
        m = (np.frombuffer(buf[:cnt * fsz], dtype=np.uint8)
             .reshape(cnt, rh, rw) > MASK_TH)
        if prev is not None:
            d = int(np.logical_xor(m[0], prev).sum())
            changes.append(d)
            if d >= MIN_CHANGE:
                patches.append(m[0].copy()); pframes.append(n)
        if cnt > 1:
            cc = np.logical_xor(m[1:], m[:-1]).sum(axis=(1, 2))
            changes.extend(int(v) for v in cc)
            for j, v in enumerate(cc):
                if v >= MIN_CHANGE:
                    patches.append(m[j + 1].copy()); pframes.append(n + j + 1)
        prev = m[-1].copy()
        n += cnt
        if n % 10000 < cnt:
            sys.stderr.write("  解码 %d 帧...\n" % n)
    proc.stdout.close()
    proc.wait()
    np.save(args.changes, np.array(changes, dtype=np.int32))
    np.save(args.patches, np.array(patches, dtype=np.uint8))
    np.save(args.pframes, np.array(pframes, dtype=np.int64))
    print("extract: %d 帧；跳变候选 %d 个（字模变化 ≥%d）"
          % (n, len(pframes), MIN_CHANGE))


def cluster(patches, pidx):
    """字模贪心聚类 → (簇号数组, 代表字模矩阵)。"""
    reps = []
    labels = np.zeros(len(pidx), dtype=np.int32)
    for i, p in enumerate(pidx):
        m = patches[p].reshape(-1)
        if reps:
            R = np.stack(reps)
            d = np.logical_xor(R, m).sum(axis=1)
            c = int(np.argmin(d))
            if d[c] <= CLUSTER_TOL_PX:
                labels[i] = c
                continue
        reps.append(m)
        labels[i] = len(reps) - 1
    return labels, (np.stack(reps) if reps else np.zeros((0, 1)))


def cycle_order(labels, n_cls):
    """相邻跳变的转移关系 → 数字环序（多数转移 = 秒 +1）。"""
    trans = defaultdict(Counter)
    for i in range(len(labels) - 1):
        trans[int(labels[i])][int(labels[i + 1])] += 1
    succ = {c: cnt.most_common(1)[0][0] for c, cnt in trans.items()}
    # 找入度为 0 或任意起点，沿后继走最长环
    best = []
    for start in list(succ.keys()):
        order, seen, cur = [start], {start}, start
        while True:
            nxt = succ.get(cur)
            if nxt is None or nxt in seen:
                break
            order.append(nxt); seen.add(nxt); cur = nxt
        if len(order) > len(best):
            best = order
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", choices=["probe", "extract", "analyze"])
    ap.add_argument("--ffmpeg"); ap.add_argument("--ffprobe")
    ap.add_argument("--video")
    ap.add_argument("--crop", default="1871,1,48,46", help="x,y,w,h（秒位两位数字）")
    ap.add_argument("--pts", default="pts.csv")
    ap.add_argument("--changes", default="c.npy")
    ap.add_argument("--patches", default="p.npy")
    ap.add_argument("--pframes", default="pf.npy")
    ap.add_argument("--anchors", default="")
    ap.add_argument("--holdout", type=int, default=0)
    ap.add_argument("--out", default="")
    args = ap.parse_args()
    if args.stage == "probe":
        run_probe(args); return
    if args.stage == "extract":
        run_extract(args); return

    changes = np.load(args.changes)
    patches = np.load(args.patches)
    pframes = np.load(args.pframes)
    pts = [float(x) for x in open(args.pts) if x.strip()]

    # ---- 跳变（去抖）----
    idx = np.nonzero(changes >= MIN_CHANGE)[0]
    ppos = {int(f): i for i, f in enumerate(pframes)}
    ticks, pidx = [], []
    for i in idx:
        f = int(i) + 1
        if f not in ppos:
            continue
        if ticks and f - ticks[-1] < MIN_SEP:
            if changes[f - 1] > changes[ticks[-1] - 1]:
                ticks[-1], pidx[-1] = f, ppos[f]
            continue
        ticks.append(f); pidx.append(ppos[f])
    t_of = lambda f: pts[min(f, len(pts) - 1)]
    print("跳变 %d 次（%.1f 秒内，平均 %.2f 秒一次）"
          % (len(ticks), pts[-1], pts[-1] / max(1, len(ticks))))

    labels, reps = cluster(patches, pidx)
    print("秒位字模聚类：%d 类（期望 ~60，秒 00~59）" % len(reps))
    order = cycle_order(labels, len(reps))
    print("数字环序长度 %d" % len(order))
    pos_of = {c: i for i, c in enumerate(order)}
    L = len(order)

    steps, mags = [], []
    for i in range(len(ticks) - 1):
        a, b = int(labels[i]), int(labels[i + 1])
        s = 0
        if a in pos_of and b in pos_of and L > 1:
            s = (pos_of[b] - pos_of[a]) % L
            if s == 0 or s > MAX_STEP:
                s = 0                      # 假跳变（字模没变/倒退）
        steps.append(s)
        lo, hi = max(0, ticks[i] - 4), min(len(changes), ticks[i] + 4)
        mags.append(int(changes[lo:hi].max()) if hi > lo else 0)
    print("跳幅分布:", dict(sorted(Counter(steps).items())))

    # ---- 锚点校正（可用子集，其余留出校验）----
    anchors = []
    if args.anchors:
        anchors = sorted((int(a[0]), int(a[1]))
                         for a in json.load(open(args.anchors, encoding="utf-8"))
                         if int(a[1]) > 0)
    fit_a = [a for i, a in enumerate(anchors)
             if args.holdout <= 0 or i % args.holdout != 0 or i == 0]
    tsec = np.array([t_of(f) * 1000 for f in ticks])
    adjusted = 0
    for a in range(len(fit_a) - 1):
        t1, v1 = fit_a[a]
        t2, v2 = fit_a[a + 1]
        ks = [k for k in range(len(steps)) if t1 < tsec[k] <= t2]
        if not ks:
            continue
        need = int(round((v2 - v1) / 1000.0))
        have = sum(steps[k] for k in ks)
        delta = need - have
        # 分配依据 = **间隔**而不是变化幅度：
        # 漏检的跳变表现为「相邻跳变间隔异常长」（OSD 卡住/被遮挡后恢复），
        # 误检的跳变表现为「间隔异常短」。按幅度分配实测会把秒数搬到错的地方
        # （抽检现场：t=1564s 处偏 -8 秒，而该处 OSD 完全清晰）。
        gaps = [tsec[k] - tsec[k - 1] for k in ks]
        order = [k for _, k in sorted(zip(gaps, ks), key=lambda z: -z[0])]
        if delta > 0:
            for k in order[:delta]:
                steps[k] += 1; adjusted += 1
        elif delta < 0:
            for k in order[::-1][:abs(delta)]:
                if steps[k] > 0:
                    steps[k] = 0; adjusted += 1
    print("锚点校正：调整 %d 处（%.1f%%）" % (adjusted, adjusted * 100.0 / max(1, len(steps))))

    cum = np.concatenate([[0], np.cumsum(steps)])
    total = int(cum[-1])
    print("像素推出总秒数 %d / 流内 %.1f 秒 → 平均倍率 %.4f"
          % (total, pts[-1], total / pts[-1]))

    if args.holdout > 0 and anchors:
        t0, v0 = fit_a[0]
        k0 = min(max([k for k in range(len(ticks)) if tsec[k] <= t0], default=0), len(cum) - 2)
        print()
        print("★ 独立校验（留出点，未参与任何校正）：")
        errs = []
        for i, (t, v) in enumerate(anchors):
            if i % args.holdout != 0 or i == 0:
                continue
            k = min(max([kk for kk in range(len(ticks)) if tsec[kk] <= t], default=0),
                    len(cum) - 2)
            pred = v0 + (cum[k + 1] - cum[k0 + 1]) * 1000
            errs.append((t, v, pred, pred - v))
        for t, v, p_, e in errs:
            print("   t=%8.1fs 实测 %d 预测 %d 差 %+d ms%s"
                  % (t / 1000.0, v // 1000, p_ // 1000, e, "" if e == 0 else "  <<<"))
        if errs:
            print("   → 完全一致 %d/%d；最大偏差 %.0f ms"
                  % (sum(1 for e in errs if e[3] == 0), len(errs),
                     max(abs(e[3]) for e in errs)))

    if args.out:
        t0, v0 = (fit_a[0] if fit_a else (0, 0))
        k0 = min(max([k for k in range(len(ticks)) if tsec[k] <= t0], default=0), len(cum) - 2)
        rows = [[int(tsec[k]),
                 int(v0 + (cum[min(k + 1, len(cum) - 1)]
                           - cum[min(k0 + 1, len(cum) - 1)]) * 1000)]
                for k in range(len(ticks))]
        json.dump({"n_ticks": len(ticks), "map": rows}, open(args.out, "w", encoding="utf-8"))
        print("已写出映射表 %s（%d 个秒级锚点）" % (args.out, len(rows)))


if __name__ == "__main__":
    main()
