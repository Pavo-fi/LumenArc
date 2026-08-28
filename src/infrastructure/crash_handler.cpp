/**
 * @file crash_handler.cpp
 * @brief 崩溃黑匣子实现（Windows：UEF + MiniDumpWriteDump；其他平台退化为会话锁）
 *
 * 崩溃处理器内只用 WinAPI + 预准备的全局缓冲（不调 Qt/堆分配，尽力而为）。
 */
#include "infrastructure/crash_handler.h"

// APP_VERSION 宏来自 CMake add_compile_definitions（单一真源 project(VERSION)）

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dbghelp.h>
#endif

namespace {

QString g_dir;                       ///< 崩溃目录（install 时建）
QString g_lockPath;                  ///< 会话锁路径
char g_stage[192] = "boot";          ///< 当前阶段（崩溃处理器只读此内存）
std::atomic<bool> g_installed{false};

#ifdef Q_OS_WIN
wchar_t g_dirW[MAX_PATH] = {0};
PTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

/// 崩溃时落盘：minidump + crash.txt（纯 WinAPI，尽力而为）
LONG WINAPI unhandledFilter(EXCEPTION_POINTERS *ex)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t dmpPath[MAX_PATH];
    swprintf_s(dmpPath, L"%s\\%04d%02d%02d_%02d%02d%02d_%lu.dmp", g_dirW,
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
               GetCurrentProcessId());

    HANDLE hf = CreateFileW(dmpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = ex;
        mei.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hf,
                          MiniDumpNormal, &mei, nullptr, nullptr);
        CloseHandle(hf);
    }

    // crash.txt：异常码/地址/版本/最后阶段（覆盖同名文件，保留最近多次命名）
    wchar_t txtPath[MAX_PATH];
    swprintf_s(txtPath, L"%s\\crash_%04d%02d%02d_%02d%02d%02d.txt", g_dirW,
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    HANDLE ht = CreateFileW(txtPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ht != INVALID_HANDLE_VALUE) {
        char buf[768];
        const DWORD code = ex && ex->ExceptionRecord
                               ? ex->ExceptionRecord->ExceptionCode : 0;
        const void *addr = ex && ex->ExceptionRecord
                               ? ex->ExceptionRecord->ExceptionAddress : nullptr;
        int n = snprintf(buf, sizeof(buf),
                         "LumenArc %s crash\r\nexception=0x%08lX addr=%p\r\n"
                         "stage=%s\r\ndump=%04d%02d%02d_%02d%02d%02d_%lu.dmp\r\n",
                         APP_VERSION, code, addr, g_stage,
                         st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                         st.wSecond, GetCurrentProcessId());
        DWORD written = 0;
        WriteFile(ht, buf, DWORD(n > 0 ? n : 0), &written, nullptr);
        CloseHandle(ht);
    }

    if (g_prevFilter)
        return g_prevFilter(ex);
    return EXCEPTION_EXECUTE_HANDLER;
}

void onTerminate()
{
    // std::terminate（未捕获 C++ 异常等）：留标记文件（此时栈已不可靠，不做 dump）
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\terminate.txt", g_dirW);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buf[256];
        int n = snprintf(buf, sizeof(buf),
                         "std::terminate, stage=%s, version=%s\r\n",
                         g_stage, APP_VERSION);
        DWORD w = 0;
        WriteFile(h, buf, DWORD(n > 0 ? n : 0), &w, nullptr);
        CloseHandle(h);
    }
    std::abort();
}

void onAbort(int)
{
    wchar_t p[MAX_PATH];
    swprintf_s(p, L"%s\\abort.txt", g_dirW);
    HANDLE h = CreateFileW(p, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        char buf[256];
        int n = snprintf(buf, sizeof(buf), "abort, stage=%s, version=%s\r\n",
                         g_stage, APP_VERSION);
        DWORD w = 0;
        WriteFile(h, buf, DWORD(n > 0 ? n : 0), &w, nullptr);
        CloseHandle(h);
    }
    // 重抛给默认处理（WER）
    std::signal(SIGABRT, SIG_DFL);
    std::raise(SIGABRT);
}
#endif // Q_OS_WIN

} // namespace

namespace CrashHandler {

QString crashDir()
{
    if (g_dir.isEmpty()) {
        QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (base.isEmpty())
            base = QDir::temp().filePath(QStringLiteral("lumenarc"));
        g_dir = QDir(base).filePath(QStringLiteral("crash"));
    }
    QDir().mkpath(g_dir);
    return g_dir;
}

void install()
{
    if (g_installed.exchange(true))
        return;
    crashDir();   // 确保目录存在
    g_lockPath = QDir(g_dir).filePath(QStringLiteral("session.lock"));
#ifdef Q_OS_WIN
    g_dirW[0] = 0;
    wcsncpy_s(g_dirW, QDir::toNativeSeparators(g_dir).toStdWString().c_str(),
              MAX_PATH - 1);
    g_prevFilter = SetUnhandledExceptionFilter(unhandledFilter);
    std::set_terminate(onTerminate);
    std::signal(SIGABRT, onAbort);
#endif
}

bool previousSessionCrashed()
{
    return !g_lockPath.isEmpty() && QFileInfo::exists(g_lockPath);
}

QString previousSessionInfo()
{
    QFile f(g_lockPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString::fromUtf8(f.readAll()).trimmed();
    return QString();
}

void beginSession()
{
    setStage(QStringLiteral("begin"));
}

void setStage(const QString &stage)
{
    // 内存缓冲（崩溃处理器只读这里）
    const QByteArray s = stage.toUtf8().left(180);
    std::strncpy(g_stage, s.constData(), sizeof(g_stage) - 1);
    g_stage[sizeof(g_stage) - 1] = 0;
    // 会话锁（覆盖写：版本/启动保持首行由 begin 写入的简单格式）
    if (g_lockPath.isEmpty())
        return;
    QFile f(g_lockPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        f.write(QStringLiteral("version=%1\nstarted=%2\nstage=%3\n")
                    .arg(QString::fromUtf8(APP_VERSION),
                         QDateTime::currentDateTime().toString(Qt::ISODate),
                         stage)
                    .toUtf8());
        f.flush();
    }
}

void markCleanExit()
{
    if (!g_lockPath.isEmpty())
        QFile::remove(g_lockPath);
}

} // namespace CrashHandler
