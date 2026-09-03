#!/usr/bin/env python3
"""Release 打包：build/Release → build_tmp/dist/LumenArc-v<ver>-win64.zip

红线排除（案件数据/测试程序/日志杂项）+ 必含清单校验（缺失即报错退出）——
probe_timestamps.py 误删事故（v1.16.0/v1.16.1 初包自动校时失效）的固化防线。

用法：python tools/pack_release.py <version>     例：python tools/pack_release.py 1.16.1
"""
import fnmatch
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "build", "Release")
OUT_DIR = os.path.join(ROOT, "build_tmp", "dist")

# ---- 红线排除 ----
EXCLUDE_DIRS = {"cases",                      # 真实案件数据，永远不入包
                "mlt"}                        # MLT/melt 调研基建：进程内路线不进包（拍板 2026-09-03）
EXCLUDE_FILES = {
    "case.json.lock", "lightchaser.jpg",       # 锁文件；splash 图已内嵌 qrc
    "Qt6Test.dll", "Qt6Testd.dll",             # 仅测试程序需要
    "nul", "NUL",                              # cygwin 重定向事故文件（保留设备名）
}
EXCLUDE_GLOB = ["lumenarc_*", "*.log", "*.rlog",
                "maglayout_shot*.png",         # 放大镜布局截图（开发杂项）
                "analyze_video.py"]            # 退役 Python 引擎遗物（代码仅注释引用）

# ---- 必含清单（运行时硬依赖；缺任何一个 → 打包失败）----
REQUIRED = [
    "LumenArc.exe",
    "vc_redist.x64.exe",                       # Win10 运行库（常见问题首行）
    "probe_timestamps.py",                     # 自动校时 OCR 运行时脚本！
    "python/python.exe",                       # 随包 Python（OCR 依赖）
    "ffmpeg/ffmpeg.exe", "ffmpeg/ffprobe.exe",
    "platforms/qwindows.dll",
    "tls/qschannelbackend.dll",                # HTTPS(Schannel 系统 TLS，账号/反馈链路)
    "app.ico",
    "Qt6Core.dll", "Qt6Widgets.dll", "Qt6Multimedia.dll",
    "追光者 Lumen Arc — 操作手册.pdf",          # 帮助菜单运行时查找
]


def excluded(rel, is_dir):
    name = os.path.basename(rel)
    if is_dir:
        return name in EXCLUDE_DIRS
    if name in EXCLUDE_FILES:
        return True
    return any(fnmatch.fnmatch(name, pat) for pat in EXCLUDE_GLOB)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: pack_release.py <version>")
    ver = sys.argv[1]
    if not os.path.isdir(SRC):
        sys.exit(f"not found: {SRC}")

    # 必含清单校验（在源目录上先查，早发现）
    missing = [r for r in REQUIRED if not os.path.exists(os.path.join(SRC, r))]
    if missing:
        sys.exit("REQUIRED missing in build/Release:\n  " + "\n  ".join(missing))

    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.join(OUT_DIR, f"LumenArc-v{ver}-win64.zip")
    if os.path.exists(out):
        os.remove(out)

    n_files = 0
    total = 0
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as zf:
        for dirpath, dirnames, filenames in os.walk(SRC):
            dirnames[:] = [d for d in dirnames
                           if not excluded(os.path.join(dirpath, d), True)]
            for fn in filenames:
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, SRC)
                if excluded(rel, False):
                    continue
                arc = os.path.join("LumenArc", rel)
                zf.write(full, arc)
                n_files += 1
                total += os.path.getsize(full)
                if n_files % 200 == 0:
                    print(f"  {n_files} files, {total // 1048576}MB src...", flush=True)

    # 包内复核：清单逐条在 zip 里再验一次（双保险）
    with zipfile.ZipFile(out) as zf:
        names = set(zf.namelist())
    missed_in_zip = [r for r in REQUIRED
                     if os.path.join("LumenArc", r).replace("\\", "/") not in names]
    if missed_in_zip:
        os.remove(out)
        sys.exit("REQUIRED missing IN ZIP:\n  " + "\n  ".join(missed_in_zip))

    size_mb = os.path.getsize(out) // 1048576
    print(f"OK: {out}")
    print(f"  {n_files} files, src {total // 1048576}MB -> zip {size_mb}MB")
    print(f"  required manifest: {len(REQUIRED)}/{len(REQUIRED)} present")


if __name__ == "__main__":
    main()
