/**
 * @file reportpreflightdialog.h
 * @brief P-28 批次③：报告生成前「自检 + 信息补录」对话框
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 流程拍板（2026-08-23）：生成报告前必须自检（❌阻断/⚠️放行）并在软件内
 * 补录信息（审核人/批准人/送检人 + 逐路拍摄方向/提取方式/存储介质），
 * 补录持久化到 case.json extraFields["report/…"]，下次生成记忆。
 */

#pragma once

#include <QDialog>

class CaseManager;
class VideoStateManager;
class QTreeWidget;
class QLineEdit;
class QTableWidget;
class QPushButton;

class ReportPreflightDialog : public QDialog
{
    Q_OBJECT
public:
    ReportPreflightDialog(CaseManager *cm, VideoStateManager *vsm,
                          QWidget *parent = nullptr);

private:
    void recheck();          ///< 重新聚合（跳过哈希）+ 自检列表刷新
    void persistExtras();    ///< 补录 → extraFields 落盘

    CaseManager *m_cm;
    VideoStateManager *m_vsm;
    QTreeWidget *m_checks = nullptr;
    QLineEdit *m_reviewer = nullptr;
    QLineEdit *m_approver = nullptr;
    QLineEdit *m_sender = nullptr;       ///< 送检人
    QTableWidget *m_videoTable = nullptr; ///< 逐路补录：方向/提取方式/存储介质
    QPushButton *m_genBtn = nullptr;
    QStringList m_videoIds;              ///< 补录表行序 ↔ 视频 id
};
