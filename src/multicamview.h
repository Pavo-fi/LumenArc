/**
 * @file multicamview.h
 * @brief 多机时间线条（ui 层）：块位/重叠/缺口 + 共享墙钟游标（P-57 升级可交互）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.1（2026-08-18 P-57：加游标/拖动 seek，供多机同步播放窗口；
 *           原只读 MultiCamDialog 由 MultiCamPlaybackWindow 取代删除）
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §3-M3 任务14；
 * docs/MULTICAM_PLAYBACK_TECH_DESIGN_CN.md §3.2（模式A 合并时间线）。
 * 画法：各路墙钟块位 / 重叠（≥2 机位同覆，红色叠加）/ 缺口（零覆盖，灰纹）；
 * 游标：竖线 + 墙钟标签，按下/拖动发 scrubPreview（实时追逐）、松开发
 * seekCommit（精确 seek）；双击块发 laneActivated（回单路分析）。
 */
#pragma once

#include <QVector>
#include <QRect>
#include <QWidget>
#include "app/cam_timeline.h"

/// 多机时间线条控件：一机位一行，块位=墙钟、宽∝时长 + 共享游标
class MultiCamViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MultiCamViewWidget(QWidget *parent = nullptr);

    /// toleranceMs：缺口/重叠判定容差（与 domain kContinuityToleranceMs 对齐）
    void setLanes(const QVector<CamLane> &lanes, qint64 toleranceMs = 2000);

    /// 共享墙钟游标（P-57）：-1 = 不显示
    void setCursorMs(qint64 wallMs);
    qint64 cursorMs() const { return m_cursorMs; }

    QSize sizeHint() const override { return {720, 220}; }

signals:
    void laneActivated(const QString &videoId);   ///< 双击块（回单路分析）
    void scrubPreview(qint64 wallMs);   ///< 游标拖动中（实时追逐，高频）
    void seekCommit(qint64 wallMs);     ///< 点击/松手（一次性精确 seek）

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;           // ToolTip 命中
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Band { QRect rect; QString label; bool overlap; };

    void rebuildLayout();
    qint64 xToWall(int x) const;                  ///< 像素 → 墙钟（布局反解）
    static QString fmtSpan(qint64 ms);            // "4:21" / "1:02:03"
    QString laneTooltip(int i) const;

    QVector<CamLane> m_lanes;
    qint64 m_tolMs = 2000;

    QVector<QPair<QString, QRect>> m_rects;       // videoId -> 块矩形
    QVector<Band> m_bands;                        // 重叠/缺口竖带
    QVector<QPair<int, QString>> m_tickLabels;    // x 像素 -> 刻度文字

    // 游标/拖动（P-57）
    qint64 m_cursorMs = -1;
    bool m_scrubbing = false;
    qint64 m_layoutT0 = 0;      // rebuildLayout 记录的轴域（xToWall 用）
    double m_layoutMsPerPx = 1.0;
};
