#!/usr/bin/env python3
# =============================================================================
# mac_bundle_deps.py — LumenArc macOS 依赖打包/审计/签名工具
#
#   bundle : 递归收集所有 Mach-O 对 /opt/homebrew、/usr/local 等构建机路径的
#            dylib 依赖，拷入 Contents/Frameworks，id 改为 @rpath/<name>，
#            引用方全部 install_name_tool -change 改写，并补齐 rpath。
#   audit  : 扫描 .app 内全部 Mach-O，任何残留的构建机绝对路径依赖或 rpath
#            一律报错并以非零码退出（CI 质量门）。
#   sign   : 对全部 Mach-O 及 .app 本体做 ad-hoc 签名（arm64 必须）。
#
# 用法:
#   python3 tools/mac_bundle_deps.py bundle LumenArc.app
#   python3 tools/mac_bundle_deps.py audit  LumenArc.app
#   python3 tools/mac_bundle_deps.py sign   LumenArc.app
# =============================================================================
import os
import re
import shutil
import subprocess
import sys

BAD_PREFIXES = ('/opt/homebrew', '/usr/local', '/Users/', '/Volumes/')
OK_PREFIXES = ('@rpath', '@loader_path', '@executable_path',
               '/usr/lib', '/System/Library')

MACHO_MAGICS = (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe',  # 64/32 LE
                b'\xfe\xed\xfa\xcf', b'\xfe\xed\xfa\xce',  # BE
                b'\xca\xfe\xba\xbe')                        # fat


def is_macho(path):
    try:
        with open(path, 'rb') as f:
            return f.read(4) in MACHO_MAGICS
    except OSError:
        return False


def all_machos(app):
    out = []
    for root, _dirs, files in os.walk(app):
        for name in files:
            p = os.path.join(root, name)
            if not os.path.islink(p) and is_macho(p):
                out.append(p)
    return out


def otool_l(path):
    """返回 (id_or_None, [deps...])"""
    r = subprocess.run(['otool', '-L', path], capture_output=True, text=True)
    lines = r.stdout.splitlines()[1:]  # 首行是文件路径
    entries = [re.match(r'\s+(\S+)\s+\(', ln).group(1)
               for ln in lines if re.match(r'\s+(\S+)\s+\(', ln)]
    if not entries:
        return None, []
    # 可执行文件第一条是系统库；dylib 第一条是自己的 install name
    if path.endswith(('.dylib', '.so')) or os.path.basename(path).count('.') > 0:
        return entries[0], entries[1:]
    return None, entries


def otool_rpaths(path):
    r = subprocess.run(['otool', '-l', path], capture_output=True, text=True)
    return re.findall(r'LC_RPATH.*?\n\s+path\s+(\S+)', r.stdout, re.S)


def run_tool(args, ignore_errors=True):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0 and not ignore_errors:
        raise RuntimeError(f"{' '.join(args)}\n{r.stderr}")
    return r


def is_bad(dep):
    return dep.startswith(BAD_PREFIXES)


def is_ok(dep, app):
    if dep.startswith(OK_PREFIXES):
        return True
    # 允许指向 bundle 内部的绝对路径
    return os.path.abspath(dep).startswith(os.path.abspath(app))


def strip_bad_rpaths(app):
    """删除指向构建机的 LC_RPATH（/opt/homebrew、/Users/… 、/DLC/… 等）。

    这些是第三方 wheel（Pillow/opencv）的构建残留：用户机上不存在，留着会被
    audit 判为坏 rpath；删掉不影响依赖解析——依赖已统一为 @loader_path/@rpath/
    系统库三类。@ 前缀与 bundle 内绝对路径的 rpath 一律保留。
    """
    removed = 0
    for m in all_machos(app):
        for rp in otool_rpaths(m):
            if is_bad(rp) or not is_ok(rp, app):
                run_tool(['install_name_tool', '-delete_rpath', rp, m])
                removed += 1
    return removed


def frameworks_dir(app):
    return os.path.join(app, 'Contents', 'Frameworks')


def _co_located(dep, macho, by_name):
    """绝对依赖 → bundle 内同名文件（同目录 → .dylibs 子目录 → 全 bundle 唯一命中）。
    by_name 为预建索引 {basename: [paths]}（避免每次回退都全树扫描）。"""
    base = os.path.basename(dep)
    d = os.path.dirname(macho)
    for cand in (os.path.join(d, base), os.path.join(d, '.dylibs', base)):
        if os.path.exists(cand):
            return cand
    hits = by_name.get(base, [])
    return hits[0] if len(hits) == 1 else None


def normalize_absolute_refs(app):
    """把"绝对路径但文件就在 bundle 内"的 install name / 依赖改写为 @loader_path/…。

    典型场景：opencv-python 官方 wheel 的 cv2/.dylibs/* 以构建机路径
    /DLC/cv2/.dylibs/<name> 互相引用（该路径在用户机上不存在）。cmd_bundle 的
    BAD_PREFIXES 不含 /DLC，故既不打包它、也不改写它，而 audit（"非 @ 前缀一律
    拒绝"）会报错——两边口径出现缝隙。此处统一改为 @loader_path：与 wheel 自身
    cv2.abi3.so → @loader_path/.dylibs/… 的引用方式一致，自洽且过严格门。
    只改写"目标确实存在于 bundle 内"的引用，不改写系统库。
    """
    machos = all_machos(app)          # 只走一遍文件树，建 basename 索引
    by_name = {}
    for p in machos:
        by_name.setdefault(os.path.basename(p), []).append(p)

    count = 0
    for m in machos:
        mid, deps = otool_l(m)
        # 自身 install name 是绝对路径（且就是本文件）→ 改为 @loader_path/<自身名>
        if mid and not mid.startswith(OK_PREFIXES):
            tgt = _co_located(mid, m, by_name)
            if tgt is None or os.path.abspath(tgt) == os.path.abspath(m):
                run_tool(['install_name_tool', '-id',
                          f'@loader_path/{os.path.basename(m)}', m])
                count += 1
        for dep in deps:
            if dep.startswith(OK_PREFIXES):
                continue
            tgt = _co_located(dep, m, by_name)
            if tgt is None:
                continue
            rel = os.path.relpath(tgt, os.path.dirname(m)).replace(os.sep, '/')
            run_tool(['install_name_tool', '-change', dep,
                      f'@loader_path/{rel}', m])
            count += 1
    return count


# ---------------------------------------------------------------- bundle ----
def cmd_bundle(app):
    fw = frameworks_dir(app)
    os.makedirs(fw, exist_ok=True)
    bundled = {}  # 原绝对路径 -> @rpath 名

    # 先归一 bundle 内自带的绝对引用（opencv wheel 的 /DLC/... 等），
    # 否则它们既不会被拷进 Frameworks，又会被 audit 判为坏依赖
    fixed = normalize_absolute_refs(app)
    if fixed:
        print(f'[bundle] normalized {fixed} co-located absolute reference(s)')
    dropped = strip_bad_rpaths(app)
    if dropped:
        print(f'[bundle] stripped {dropped} build-machine rpath(s)')

    # 迭代：新拷入的库可能又引入新的坏依赖，直到收敛
    round_no = 0
    while True:
        round_no += 1
        machos = all_machos(app)
        new_libs = {}  # bad_dep -> src_path
        for m in machos:
            _id, deps = otool_l(m)
            for d in deps:
                if is_bad(d) and d not in bundled:
                    if os.path.exists(d):
                        new_libs[d] = d
                    else:
                        # 引用了不存在的库：记录下来，审计阶段会报
                        bundled[d] = None
        if not new_libs:
            break
        print(f'[bundle] round {round_no}: {len(new_libs)} dylib(s)')
        for dep in sorted(new_libs):
            name = os.path.basename(dep)
            dst = os.path.join(fw, name)
            if not os.path.exists(dst):
                shutil.copy2(dep, dst, follow_symlinks=True)
                os.chmod(dst, 0o755)
            run_tool(['install_name_tool', '-id', f'@rpath/{name}', dst])
            bundled[dep] = name
            print(f'    + {name}  <-  {dep}')
        # 全部 mach-o 改写引用（-change 对无此引用的文件会报错，忽略）
        for m in machos:
            for dep, name in bundled.items():
                if name:
                    run_tool(['install_name_tool', '-change', dep,
                              f'@rpath/{name}', m])

    # ---- 补 rpath：让每个二进制都能从 @loader_path 找到 Frameworks ----
    for m in all_machos(app):
        rel = os.path.relpath(fw, os.path.dirname(m))
        rpath = '@loader_path' if rel == '.' else f'@loader_path/{rel}'
        if rpath not in otool_rpaths(m):
            run_tool(['install_name_tool', '-add_rpath', rpath, m])

    missing = [d for d, n in bundled.items() if n is None]
    if missing:
        print('[bundle] WARNING: 以下依赖在构建机上不存在，无法打包（audit 会报错）:')
        for d in missing:
            print('   !', d)
    print(f'[bundle] done, {sum(1 for n in bundled.values() if n)} dylib(s) bundled')
    return 0


# ---------------------------------------------------------------- audit ----
def cmd_audit(app):
    problems = []
    for m in all_machos(app):
        _id, deps = otool_l(m)
        for d in ([_id] if _id else []) + deps:
            if not d:
                continue
            if is_bad(d) or not is_ok(d, app):
                problems.append(f'{os.path.relpath(m, app)}: bad dep {d}')
        for rp in otool_rpaths(m):
            if is_bad(rp):
                problems.append(f'{os.path.relpath(m, app)}: bad rpath {rp}')
    if problems:
        print(f'[audit] FAIL: {len(problems)} problem(s)')
        for p in problems[:60]:
            print('   ', p)
        return 1
    print(f'[audit] OK: {len(all_machos(app))} Mach-O files clean')
    return 0


# ----------------------------------------------------------------- sign ----
def cmd_sign(app):
    files = all_machos(app)
    for m in files:
        r = subprocess.run(['codesign', '--force', '--sign', '-', m],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f'[sign] WARN {os.path.relpath(m, app)}: {r.stderr.strip()}')
    r = subprocess.run(['codesign', '--force', '--sign', '-', app],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f'[sign] FAIL app: {r.stderr}')
        return 1
    print(f'[sign] done, {len(files)} binaries + bundle signed (ad-hoc)')
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('bundle', 'audit', 'sign'):
        print(__doc__)
        sys.exit(2)
    sys.exit({'bundle': cmd_bundle, 'audit': cmd_audit,
              'sign': cmd_sign}[sys.argv[1]](sys.argv[2]))
