#pragma once
/**
 * @file crash_handler.h
 * @brief 崩溃黑匣子（零三方依赖：SetUnhandledExceptionFilter + dbghelp MiniDump）
 *
 * 现场机器闪退排查基建：
 * - install()        安装未处理异常过滤器 + std::terminate/SIGABRT 钩子
 * - beginSession()   写会话锁（含版本/启动时间）；上次锁残留 = 上次崩溃
 * - setStage()       阶段面包屑（覆盖写会话锁 + 内存缓冲，崩溃处理器只读内存）
 * - markCleanExit()  正常退出删锁
 * 崩溃产物：<AppLocalData>/crash/YYYYMMDD_HHMMSS_pid.dmp + crash.txt（异常码/阶段）
 */
#include <QString>

namespace CrashHandler {

void install();                  ///< main() 内 QApplication 构造后尽早调用
bool previousSessionCrashed();   ///< 上次会话未正常退出（beginSession 之前调用）
QString previousSessionInfo();   ///< 上次会话锁内容（版本/时间/最后阶段）
void beginSession();             ///< 写本会话锁
void setStage(const QString &stage);   ///< 阶段面包屑（启动里程碑处调用）
void markCleanExit();            ///< 正常退出（删会话锁）
QString crashDir();              ///< 崩溃产物目录（已确保存在）

} // namespace CrashHandler
