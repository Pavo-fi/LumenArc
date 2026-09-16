/**
 * @file mainwindow_live.cpp
 * @brief 监控直播入口（v1.17.0 P-79 多 TU 拆分风格：主窗口只保留入口接线）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计说明：MainWindow 是已知上帝类（DEVELOPMENT_STANDARDS §8 明令"新功能
 * 禁止再往里加逻辑"），因此监控直播的全部实现放在独立窗口 LiveMonitorWindow，
 * 本文件只有"打开/前置已有窗口"这一段入口代码。
 */
#include "mainwindow.h"
#include "livemonitorwindow.h"

void MainWindow::onOpenLiveMonitor()
{
    if (m_liveMonitorWin) {
        m_liveMonitorWin->raise();
        m_liveMonitorWin->activateWindow();
        return;
    }
    // WA_DeleteOnClose：关闭即销毁，QPointer 自动置空
    auto *win = new LiveMonitorWindow(this);
    m_liveMonitorWin = win;
    win->show();
}
