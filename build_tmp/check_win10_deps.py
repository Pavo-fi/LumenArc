# 依赖闭包扫描：对 build/Release 下所有 exe/dll 跑 dumpbin /dependents，
# 依赖名既不在发布包内、也不在 Win10 系统白名单中 -> 报告为风险项。
import os
import re
import subprocess
import sys

DUMPBIN = (r"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/"
           r"VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/dumpbin.exe")
RELEASE = r"C:/code/LumenArc/LumenArc_v1.0 remake/build/Release"

# Windows 10 (1607+) 自带的系统 DLL（小写）
WIN10_SYSTEM = {
    'kernel32.dll', 'user32.dll', 'gdi32.dll', 'advapi32.dll', 'ole32.dll',
    'oleaut32.dll', 'shell32.dll', 'shlwapi.dll', 'comdlg32.dll', 'comctl32.dll',
    'ws2_32.dll', 'winmm.dll', 'imm32.dll', 'version.dll', 'msimg32.dll',
    'dwmapi.dll', 'uxtheme.dll', 'winspool.drv', 'wldap32.dll', 'crypt32.dll',
    'secur32.dll', 'sspicli.dll', 'iphlpapi.dll', 'netapi32.dll', 'userenv.dll',
    'powrprof.dll', 'setupapi.dll', 'ntdll.dll', 'rpcrt4.dll', 'msvcrt.dll',
    'd3d11.dll', 'dxgi.dll', 'dwrite.dll', 'd2d1.dll', 'windowscodecs.dll',
    'mfplat.dll', 'mf.dll', 'mfreadwrite.dll', 'mfcore.dll', 'mfuuid.dll',
    'avrt.dll', 'wininet.dll', 'urlmon.dll', 'wintrust.dll', 'bcrypt.dll',
    'ncrypt.dll', 'dbghelp.dll', 'psapi.dll', 'cfgmgr32.dll', 'devobj.dll',
    'hid.dll', 'opengl32.dll', 'glu32.dll', 'wtsapi32.dll', 'normaliz.dll',
    'mswsock.dll', 'dnsapi.dll', 'rasapi32.dll', 't2embed.dll', 'usp10.dll',
    'd3d9.dll', 'd3d12.dll', 'dcomp.dll', 'dxcore.dll', 'wlanapi.dll',
    'winhttp.dll', 'propsys.dll', 'oleacc.dll', 'clbcatq.dll', 'combase.dll',
    'gdiplus.dll', 'msctf.dll', 'textinputframework.dll', 'coreMessaging.dll',
    'coremessaging.dll', 'kernel.appcore.dll', 'win32u.dll', 'dxva2.dll',
    'evr.dll', 'ksuser.dll', 'mmdevapi.dll', 'wdmaud.drv', 'msacm32.dll',
    'msacm32.drv', 'midimap.dll', 'acm.dll', 'audioeng.dll', 'audioses.dll',
    'avicap32.dll', 'msvfw32.dll', 'devenum.dll', 'quartz.dll', 'qedit.dll',
    'amstream.dll', 'strmiids.dll', 'ks.dll', 'ksproxy.ax', 'kswdmcap.ax',
    'mjpeg.dll', 'h264dec.dll', 'hevcdec.dll', 'vp9dec.dll', 'av1dec.dll',
    'msvcp_win.dll', 'ucrtbase_enclave.dll', 'vcruntime140_clr0400.dll',
    'mscoree.dll', 'msasn1.dll', 'dpapi.dll', 'cabinet.dll', 'msi.dll',
    'sxs.dll', 'wer.dll', 'faultrep.dll', 'apphelp.dll', 'acgenral.dll',
    'shfolder.dll', 'zipfldr.dll', 'thumbcache.dll', 'twinapi.dll',
    'twinapi.appcore.dll', 'windows.storage.dll', 'edputil.dll',
    'cldapi.dll', 'fltLib.dll', 'ntmarta.dll', 'samcli.dll', 'logoncli.dll',
    'netutils.dll', 'dsrole.dll', 'wkscli.dll', 'srvcli.dll', 'activeds.dll',
    'adsldpc.dll', 'credui.dll', 'cryptui.dll', 'cryptsp.dll', 'rsaenh.dll',
    'bcryptPrimitives.dll'.lower(), 'ntasn1.dll', 'gpapi.dll', 'framedynos.dll',
    'wmi.dll', 'wbemcomn.dll', 'fastprox.dll', 'wbemprox.dll', 'wbemsvc.dll',
    'webkit.dll', 'edgehtml.dll', 'chakra.dll', 'jscript9.dll', 'jscript.dll',
    'vbscript.dll', 'mshtml.dll', 'ieframe.dll', 'mlang.dll', 'msimtf.dll',
    'd3dcompiler_47.dll',  # Win10 自带 (系统目录)
    # Win10 系统库补充
    'kernelbase.dll', 'imagehlp.dll', 'mpr.dll', 'textshaping.dll',
    'uiautomationcore.dll', 'winscard.dll', 'wsock32.dll',
}

# api-ms-win-* / ext-ms-win-* 由 Win10 加载器的 API 集合模式（schema）解析，
# 不是实体文件，无需也不应随包分发（随包的 UCRT 集合除外）
SCHEMA_PREFIXES = ('ext-ms-win-',)
SCHEMA_WHITELIST = {  # Win10 (1607+) 必然存在的 api-ms 集合
    'api-ms-win-appmodel-runtime-l1-1-0.dll',
    'api-ms-win-core-apiquery-l1-1-0.dll',
    'api-ms-win-core-atoms-l1-1-0.dll',
    'api-ms-win-core-com-l1-1-0.dll',
    'api-ms-win-core-com-l1-1-1.dll',
    'api-ms-win-core-delayload-l1-1-0.dll',
    'api-ms-win-shell32-shellfolders-l1-1-0.dll',
}

all_deps = {}
scanned = 0
for root, _dirs, files in os.walk(RELEASE):
    for f in files:
        if not f.lower().endswith(('.exe', '.dll')):
            continue
        p = os.path.join(root, f)
        try:
            out = subprocess.run([DUMPBIN, '/dependents', p],
                                 capture_output=True, timeout=60)
            text = out.stdout.decode('mbcs', errors='replace')
        except Exception as e:
            print(f'[skip] {p}: {e}')
            continue
        scanned += 1
        deps = re.findall(r'^\s+(\S+\.[Dd][Ll][Ll])\s*$', text, re.M)
        for d in deps:
            all_deps.setdefault(d.lower(), set()).add(os.path.basename(p))

shipped = {f.lower() for _r, _d, fs in os.walk(RELEASE) for f in fs}
unresolved = sorted(
    d for d in all_deps
    if d not in shipped and d not in WIN10_SYSTEM
    and not any(d.startswith(p) for p in SCHEMA_PREFIXES)
    and d not in SCHEMA_WHITELIST)
covered_sys = sorted(d for d in all_deps if d not in shipped and d in WIN10_SYSTEM)

print(f'scanned binaries: {scanned}')
print(f'unique dependent DLLs: {len(all_deps)}')
print(f'resolved by package: {len(set(all_deps) & shipped)}')
print(f'resolved by Win10 system: {len(covered_sys)}')
print()
if unresolved:
    print('=== UNRESOLVED (neither shipped nor known Win10 system) ===')
    for d in unresolved:
        users = sorted(all_deps[d])[:4]
        print(f'  {d}   <- needed by: {", ".join(users)}')
else:
    print('=== ALL DEPENDENCIES RESOLVED ===')
