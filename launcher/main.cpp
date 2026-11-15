// 追光者 LumenArc 启动器（发布包专用）
//
// 为什么要它：Windows 的 DLL 搜索顺序里，可执行文件所在目录是第一位——Qt 的
// 那一堆 Qt6*.dll / api-ms-win-*.dll 必须和 exe 同目录，否则加载器在 main()
// 之前就报「找不到 DLL」。发布包如果按老办法把 80 多个 DLL 平铺在根目录，
// 用户看到的是一片文件海（现场反馈：太乱，根目录只该有文件夹＋exe＋手册）。
//
// 做法：根目录只放这个几十 KB 的启动器 + 操作手册，真正的程序与全部运行库
// 收进 runtime\。启动器把 runtime\ 挂到 PATH 前端、切好工作目录，再用
// CreateProcess 拉起 runtime\LumenArc.exe，等它退出并原样返回退出码。
// 参数（--selfcheck-shell 之类）与控制台输出都原样透传，不影响无头自检。
//
// 编译要求：/MT 静态运行库（本文件不能依赖 vcruntime140.dll，否则又在根目录
// 多出两个 DLL，失去意义）。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

namespace {

std::wstring moduleDir()
{
    wchar_t buf[MAX_PATH * 4] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf))
        return std::wstring();
    std::wstring path(buf, n);
    const std::wstring::size_type pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

// GetCommandLineW() 去掉 argv[0]（可能带引号），返回其余部分（含前导空格）。
std::wstring commandTail()
{
    const std::wstring cmd = GetCommandLineW();
    if (cmd.empty())
        return std::wstring();
    std::wstring::size_type i = 0;
    if (cmd[0] == L'"') {
        i = cmd.find(L'"', 1);
        if (i == std::wstring::npos)
            return std::wstring();
        ++i;
    } else {
        i = cmd.find(L' ');
        if (i == std::wstring::npos)
            return std::wstring();
    }
    return cmd.substr(i);
}

void fatal(const std::wstring &text)
{
    MessageBoxW(nullptr, text.c_str(), L"追光者 LumenArc",
                MB_ICONERROR | MB_OK);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    const std::wstring dir = moduleDir();
    if (dir.empty()) {
        fatal(L"无法确定程序目录。请把整个文件夹解压到本地磁盘后再运行"
              L"（不要直接在压缩包里双击）。");
        return 2;
    }

    const std::wstring runtime = dir + L"\\runtime";
    const std::wstring target = runtime + L"\\LumenArc.exe";
    const DWORD attr = GetFileAttributesW(target.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        fatal(L"缺少程序主体：\r\n" + target
              + L"\r\n\r\n安装不完整——请重新解压完整压缩包，"
                L"不要只拷贝 LumenArc.exe 一个文件。");
        return 2;
    }

    // 让子进程及它拉起的 python/ffmpeg 都能找到随包运行库
    {
        wchar_t oldPath[32767] = {};
        const DWORD n = GetEnvironmentVariableW(L"PATH", oldPath,
                                                static_cast<DWORD>(std::size(oldPath)));
        std::wstring path = runtime;
        if (n > 0 && n < std::size(oldPath)) {
            path += L";";
            path += oldPath;
        }
        SetEnvironmentVariableW(L"PATH", path.c_str());
        // 供程序内部识别「被启动器拉起」时使用（当前仅记录，不改变行为）
        SetEnvironmentVariableW(L"LUMENARC_ROOT", dir.c_str());
    }

    SetCurrentDirectoryW(runtime.c_str());

    std::wstring cmd = L"\"" + target + L"\"" + commandTail();
    std::wstring cmdBuf = cmd;   // CreateProcessW 可能就地改写

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(target.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE,
                        CREATE_UNICODE_ENVIRONMENT, nullptr, runtime.c_str(),
                        &si, &pi)) {
        fatal(L"启动失败（错误码 " + std::to_wstring(GetLastError())
              + L"）：\r\n" + target);
        return 3;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
