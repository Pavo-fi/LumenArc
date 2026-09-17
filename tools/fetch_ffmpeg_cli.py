#!/usr/bin/env python3
"""统一 ffmpeg / ffprobe CLI 的来源：下载 BtbN GPL 静态构建 → 安装到目标目录。

为什么需要它（v1.18.0）
----------------------
build/Release/ffmpeg/ 曾出现「静态 ffmpeg.exe + 动态链接 ffprobe.exe」的混装：

  ffmpeg.exe   144,550,912 B   （GPL 静态构建，能独立跑）
  ffprobe.exe      229,376 B   （LGPL **shared** 构建，需 swscale-10.dll 等）

而 FFmpeg 的 DLL 平铺在**父目录** build/Release/ —— Windows 不搜父目录，
于是 ffprobe 直接 `exit=127  error while loading shared libraries: swscale-10.dll`，
自动校时（probe_timestamps.py 靠 ffprobe 取时长）整条失效，却照样通过了
pack_release.py 的必含清单（清单只查文件在不在）。

本脚本是这两个 CLI 的**唯一入口**：CI 与本机都调它，装完立刻在「隔离 cwd +
最小 PATH」下实跑 `-version` 自检 —— 动态链接版会在此被拒绝并删除，
不可能再落进 build/Release。

用法
----
  python tools/fetch_ffmpeg_cli.py                    # 装到 build/Release/ffmpeg
  python tools/fetch_ffmpeg_cli.py <目标目录>
  python tools/fetch_ffmpeg_cli.py --from-zip <zip>   # 离线：用已下载的压缩包
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

# Windows / CI 控制台不一定是 UTF-8（实测 GitHub runner 上是 cp1252）：直接 print
# 中文会 UnicodeEncodeError 把脚本打挂 —— 本脚本就因此在 CI 上失败过一次
# （下载/安装/自检都成功，死在最后那句中文提示）。重配为 UTF-8 + replace。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DEST = os.path.join(ROOT, "build", "Release", "ffmpeg")

# BtbN "latest" 的 GPL 构建：ffmpeg.exe / ffprobe.exe 均为**静态**自足二进制
GPL_URL = ("https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/"
           "ffmpeg-master-latest-win64-gpl.zip")

TOOLS = ["ffmpeg.exe", "ffprobe.exe"]
# 显式声明每个工具版本输出里必须出现的串（不从文件名推导——那样 ffmpeg/ffprobe
# 容易串位，且以后加工具时静默失效）
EXPECT = {"ffmpeg.exe": "ffmpeg version", "ffprobe.exe": "ffprobe version"}


def log(msg):
    print(msg, flush=True)


def download(url, dest):
    log(f"downloading {url}")
    with urllib.request.urlopen(url, timeout=120) as r, open(dest, "wb") as f:
        total = int(r.headers.get("Content-Length") or 0)
        got, mark = 0, 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
            got += len(chunk)
            if total and got * 100 // total >= mark + 20:
                mark = got * 100 // total
                log(f"  {mark}%  ({got // 1048576} MiB / {total // 1048576} MiB)")
    log(f"  done: {os.path.getsize(dest) // 1048576} MiB")


def extract_tools(zip_path, dest):
    """从压缩包里取出 bin/ffmpeg.exe 与 bin/ffprobe.exe 装到 dest。"""
    os.makedirs(dest, exist_ok=True)
    found = {}
    with zipfile.ZipFile(zip_path) as z:
        for name in z.namelist():
            base = os.path.basename(name)
            if base in TOOLS and "/bin/" in name.replace("\\", "/"):
                out = os.path.join(dest, base)
                with z.open(name) as src, open(out, "wb") as f:
                    shutil.copyfileobj(src, f)
                found[base] = out
    missing = [t for t in TOOLS if t not in found]
    if missing:
        sys.exit(f"压缩包里没找到（需 bin/ 下的静态构建）：{', '.join(missing)}")
    return found


def minimal_env():
    """只留系统目录的 PATH —— 不让本机 PATH 里的 FFmpeg 目录掩盖「缺 DLL」。"""
    root = os.environ.get("SystemRoot", r"C:\Windows")
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([os.path.join(root, "System32"), root])
    return env


def verify_selfcontained(exe, label, expect):
    """隔离 cwd + 最小 PATH 下跑 -version。返回 None 通过，否则返回错误串。

    注意：验证的是**最终安装位置**下的自足性 —— 动态链接版在它自己的主目录
    （DLL 就在旁边）能跑，但一旦落到 build/Release/ffmpeg/（DLL 在父目录，
    Windows 不搜父目录）就跑不起来。这正是要拦的情形。
    """
    with tempfile.TemporaryDirectory(prefix="ffcli_chk_") as td:
        try:
            p = subprocess.run([exe, "-version"], cwd=td, env=minimal_env(),
                               capture_output=True, timeout=180)
        except subprocess.TimeoutExpired:
            return f"{label}: 启动超时（>180s，疑似卡在 DLL 解析）"
        except OSError as e:
            return f"{label}: 无法启动（{e}）"
    text = ((p.stdout or b"") + (p.stderr or b"")).decode("utf-8", "replace")
    first = next((l.strip() for l in text.splitlines() if l.strip()), "")
    if p.returncode != 0:
        return (f"{label}: 退出码 {p.returncode} —— 不是自足二进制"
                + (f"　→　{first[:200]}" if first else ""))
    if expect not in text:
        return f"{label}: 输出不含 {expect!r}（{first[:120]}）"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dest", nargs="?", default=DEFAULT_DEST,
                    help=f"目标目录（默认 {os.path.relpath(DEFAULT_DEST, ROOT)}）")
    ap.add_argument("--from-zip", metavar="ZIP",
                    help="离线：用已下载的 BtbN GPL 压缩包，不联网")
    args = ap.parse_args()

    dest = os.path.abspath(args.dest)
    tmp = tempfile.mkdtemp(prefix="ffcli_")
    try:
        if args.from_zip:
            zip_path = os.path.abspath(args.from_zip)
            if not os.path.exists(zip_path):
                sys.exit(f"not found: {zip_path}")
            log(f"using local archive: {zip_path}")
        else:
            zip_path = os.path.join(tmp, "ffmpeg-gpl.zip")
            download(GPL_URL, zip_path)

        log(f"installing {', '.join(TOOLS)} -> {dest}")
        installed = extract_tools(zip_path, dest)

        # 自检：不合格就删掉，绝不留下半成品
        fails = []
        for tool in TOOLS:
            err = verify_selfcontained(installed[tool], tool, EXPECT[tool])
            if err:
                fails.append(err)
                log(f"  FAIL {tool}")
            else:
                log(f"  OK   {tool}")
        if fails:
            for tool in TOOLS:
                try:
                    os.remove(installed[tool])
                except OSError:
                    pass
            sys.exit("自检失败（已删除，build/Release 保持干净）:\n  " + "\n  ".join(fails))

        for tool in TOOLS:
            log(f"  {tool}: {os.path.getsize(installed[tool]) // 1048576} MiB")
        log("OK: ffmpeg CLI 已就位且自足可运行")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
