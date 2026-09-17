#!/usr/bin/env python3
"""Release 打包：build/Release → build_tmp/dist/LumenArc-v<ver>-win64.zip

三道闸门：
  1. 红线排除（案件数据/测试程序/日志杂项）
  2. 必含清单校验（缺失即报错退出）—— probe_timestamps.py 误删事故
     （v1.16.0/v1.16.1 初包自动校时失效）的固化防线
  3. **可运行性校验（v1.18.0 新增）**—— 真跑一次 ffmpeg/ffprobe/随包 Python。
     存在性清单挡不住"文件在但跑不起来"：真实事故是 build/Release/ffmpeg/
     ffprobe.exe 为动态链接版（229KB，需 swscale-10.dll 等），而 FFmpeg DLL
     平铺在**父目录** build/Release/ —— Windows 不搜父目录，ffprobe 直接
     exit=127 起不来，自动校时（probe_timestamps.py 靠它取时长）整条失效，
     却照样通过了必含清单。故这里在隔离 cwd + 最小 PATH 下实跑。

用法：python tools/pack_release.py <version>     例：python tools/pack_release.py 1.16.1
"""
import fnmatch
import os
import re
import subprocess
import sys
import tempfile
import zipfile

# Windows / CI 控制台不一定是 UTF-8（实测 GitHub runner 上是 cp1252）：
# 直接 print 中文会 UnicodeEncodeError 把脚本打挂（fetch_ffmpeg_cli.py 已在 CI
# 上因此失败过一次）。统一把 stdout/stderr 重配为 UTF-8 + replace，一劳永逸。
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

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

# ---- FFmpeg 陈旧运行库剔除（v1.18.0）----
# 根因：FFmpeg SDK 换过大版本（7.x → 8.x：avcodec-61→-63、avutil-59→-61、
# swresample-5→-7、swscale-8→-10），旧 DLL 一直留在 build/Release 里，而打包是
# 整目录扫描 → 旧版 DLL 随包发行（约 17MB 冗余，且存在被误加载的隐患）。
# 现改为按 third_party/ffmpeg/bin 的实况自动剔除：只保留当前 SDK 真正提供的
# 那一套，将来 SDK 再升级也无需修改本文件。
FFMPEG_BIN = os.path.join(ROOT, "third_party", "ffmpeg", "bin")
FFMPEG_DLL_RE = re.compile(r"^(?:av|sw)[a-z0-9_]*-?\d*\.dll$", re.IGNORECASE)


def current_ffmpeg_dlls():
    """当前 SDK 提供的顶层 FFmpeg DLL 名（小写）；SDK 目录缺失时返回 None＝不剔除。"""
    if not os.path.isdir(FFMPEG_BIN):
        return None
    return {n.lower() for n in os.listdir(FFMPEG_BIN) if n.lower().endswith(".dll")}


def is_stale_ffmpeg_dll(rel, current):
    """顶层目录下、看着像 FFmpeg DLL、但不在当前 SDK 名单里 → 陈旧残留。"""
    if current is None:
        return False
    if os.path.dirname(rel):          # 只看顶层（随包 DLL 一律平铺在根目录）
        return False
    name = os.path.basename(rel).lower()
    return bool(FFMPEG_DLL_RE.match(name)) and name not in current

# ---- 必含清单（运行时硬依赖；缺任何一个 → 打包失败）----
REQUIRED = [
    "LumenArc.exe",
    "vc_redist.x64.exe",                       # Win10 运行库（常见问题首行）
    "setup_python_deps.bat",                  # 随包一键装 Python 依赖（旧系统备用路径）
    # VC++ 运行库：干净 Windows 启动硬依赖。由 CMake 从 portable/sysdlls 拷入
    # （见 CMakeLists.txt 的 SYSDLLS 块）。此前守卫条件误依赖 dxgi.dll 而静默失效，
    # 导致 v1.16.0/v1.16.1/v1.17.0 包在未装运行库的机器上双击即报缺 DLL。
    # 此处列为必含项，令同类回归在打包阶段就报错，而不是等用户报障。
    "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll",
    "probe_timestamps.py",                     # 自动校时 OCR 运行时脚本！
    "python/python.exe",                       # 随包 Python（OCR 依赖）
    "ffmpeg/ffmpeg.exe", "ffmpeg/ffprobe.exe",
    "platforms/qwindows.dll",
    "tls/qschannelbackend.dll",                # HTTPS(Schannel 系统 TLS，账号/反馈链路)
    "app.ico",
    "Qt6Core.dll", "Qt6Widgets.dll", "Qt6Multimedia.dll",
    "追光者 Lumen Arc — 操作手册.pdf",          # 帮助菜单运行时查找
]

# ---- 可运行性校验清单（v1.18.0）：(相对路径, 参数, 输出必含串, 人话说明) ----
RUNNABLE = [
    ("ffmpeg/ffmpeg.exe", ["-version"], "ffmpeg version",
     "随包 ffmpeg CLI"),
    ("ffmpeg/ffprobe.exe", ["-version"], "ffprobe version",
     "随包 ffprobe（自动校时取时长用）"),
    ("python/python.exe",
     ["-c", "import cv2, numpy; print('OCR deps ok', cv2.__version__, numpy.__version__)"],
     "OCR deps ok", "随包 Python + OCR 依赖（opencv/numpy）"),
]


def minimal_env():
    """只留系统目录的 PATH —— 让"缺 DLL"无法借本机 PATH 里的 FFmpeg 目录蒙混过关。"""
    root = os.environ.get("SystemRoot", r"C:\Windows")
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join([os.path.join(root, "System32"), root])
    return env


def run_check(rel, args, expect, desc):
    """在隔离 cwd + 最小 PATH 下实跑。返回 (错误串|None, 首行输出)。"""
    exe = os.path.join(SRC, *rel.split("/"))
    if not os.path.exists(exe):
        return f"{desc}：{rel} 不存在", ""
    with tempfile.TemporaryDirectory(prefix="lumenarc_packchk_") as td:
        try:
            p = subprocess.run([exe] + args, cwd=td, env=minimal_env(),
                               capture_output=True, timeout=180)
        except subprocess.TimeoutExpired:
            return f"{desc}：启动超时（>180s，疑似卡在 DLL 解析）", ""
        except OSError as e:
            return f"{desc}：无法启动（{e}）", ""
    text = ((p.stdout or b"") + (p.stderr or b"")).decode("utf-8", "replace")
    info = next((l.strip() for l in text.splitlines() if l.strip()), "")
    if p.returncode != 0:
        return (f"{desc}：退出码 {p.returncode}（{rel}）"
                + (f"　→　{info[:220]}" if info else "")), info
    if expect and expect not in text:
        return f"{desc}：输出缺少预期串 {expect!r}（{rel}）", info
    return None, info


def verify_runnable():
    """三道闸门的第 3 道。任一失败即终止打包。"""
    print("verifying executables actually run (isolated cwd + minimal PATH)...")
    fails = []
    for rel, args, expect, desc in RUNNABLE:
        err, info = run_check(rel, args, expect, desc)
        if err:
            fails.append(err)
            print(f"  FAIL {desc}")
        else:
            print(f"  OK   {desc}　→　{info[:110]}")
    if fails:
        sys.exit(
            "RUNNABILITY check failed —— 文件在但跑不起来"
            "（存在性清单挡不住这类缺陷，例如动态链接的 ffprobe 放在子目录里找不到父目录的 DLL）:\n  "
            + "\n  ".join(fails))


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

    # 可运行性校验（比"存在"更强：隔离环境实跑一次）
    verify_runnable()

    os.makedirs(OUT_DIR, exist_ok=True)
    out = os.path.join(OUT_DIR, f"LumenArc-v{ver}-win64.zip")
    if os.path.exists(out):
        os.remove(out)

    n_files = 0
    total = 0
    stale_skipped = []
    current_ffmpeg = current_ffmpeg_dlls()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as zf:
        for dirpath, dirnames, filenames in os.walk(SRC):
            dirnames[:] = [d for d in dirnames
                           if not excluded(os.path.join(dirpath, d), True)]
            for fn in filenames:
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, SRC)
                if excluded(rel, False):
                    continue
                if is_stale_ffmpeg_dll(rel, current_ffmpeg):
                    stale_skipped.append(rel)
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
    if stale_skipped:
        stale_mb = sum(os.path.getsize(os.path.join(SRC, r)) for r in stale_skipped) // 1048576
        print(f"  stale FFmpeg DLLs excluded: {len(stale_skipped)} ({stale_mb}MB)")
        for r in sorted(stale_skipped):
            print(f"    - {r}")


if __name__ == "__main__":
    main()
