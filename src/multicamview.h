/**
 * @file multicamview.h
 * @brief 多机时间线对齐只读视图（ui 层，v1.3.0 M3 任务14）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §3-M3 任务14。
 * 只读：各路 .vla 校时 → 墙钟块位 / 重叠（≥2 机位同覆，红色叠加）/
 * 缺口（零覆盖，灰纹）；画法复用 ClipTimelineWidget（块圆角/刻度首标签
 * 含日期/fmtSpan 时长串）。<2 路已校时时菜单置灰（MainWindow 判定），
 * 本对话框内同样兜底提示。双击块发 laneActivated 供主窗打开该路（不
 * 改变任何数据，保持只读语义）。
 */
#pragma once

#include <QDialog>
#include <QVector>
#include <QRect>
#include "app/cam_timeline.h"

class QLabel;

/// 多机时间线条控件：一机位一行，块位=墙钟、宽∝已分析时长
class MultiCamViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MultiCamViewWidget(QWidget *parent = nullptr);

    /// toleranceMs：缺口/重叠判定容差（与 domain kContinuityToleranceMs 对齐）
    void setLanes(const QVector<CamLane> &lanes, qint64 toleranceMs = 2000);

    QSize sizeHint() const override { return {720, 220}; }

signals:
    void laneActivated(const QString &videoId);   ///< 双击块（只读，供打开）

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;           // ToolTip 命中
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Band { QRect rect; QString label; bool overlap; };

    void rebuildLayout();
    static QString fmtSpan(qint64 ms);            // "4:21" / "1:02:03"
    QString laneTooltip(int i) const;

    QVector<CamLane> m_lanes;
    qint64 m_tolMs = 2000;

    QVector<QPair<QString, QRect>> m_rects;       // videoId -> 块矩形
    QVector<Band> m_bands;                        // 重叠/缺口竖带
    QVector<QPair<int, QString>> m_tickLabels;    // x 像素 -> 刻度文字
};

/// 多机时间线对齐对话框（只读）：时间线条 + 图例 + 未校时清单 + 关闭
class MultiCamDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MultiCamDialog(const class CaseManager *cm, QWidget *parent = nullptr);

signals:
    void openVideoRequested(const QString &videoId);
};
