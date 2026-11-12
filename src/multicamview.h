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

class QWheelEvent;

/// 多机时间线条控件：一机位一行，块位=墙钟、宽∝时长 + 共享游标
class MultiCamViewWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MultiCamViewWidget(QWidget *parent = nullptr);

    /// toleranceMs：缺口/重叠判定容差（与 domain kContinuityToleranceMs 对齐）
    void setLanes(const QVector<CamLane> &lanes, qint64 toleranceMs = 2000);

    /// 时间轴条整体纵向高度（纯函数；布局与自检共用同一口径）。
    /// v2.7 用户拍板：各宫格时间轴纵向高度压到约一半，把高度还给画面。
    static int timelineHeightFor(int laneCount);

    /// v2.9（用户拍板）：选段定好后时间轴自动放大到 AB 区间，像单路一样
    /// 只看这一段；在这个视野里再加变速断点。
    void setViewRange(qint64 t0Wall, qint64 spanMs);
    /// 变速断点（墙钟，升序）——画成竖虚线 + 段号小标
    void setSplits(const QVector<qint64> &splitsWall);
    /// v3.2：点选断点（-1=无选中）。选中的那条画成强调色实线 + 加粗，
    /// 供「按 Del 删除这个断点」使用——不再只能删最后一个。
    int selectedSplit() const { return m_selectedSplit; }
    void setSelectedSplit(int idx) { m_selectedSplit = idx; update(); }
    int splitCount() const { return m_splitsWall.size(); }
    qint64 splitAt(int idx) const
    {
        return (idx >= 0 && idx < m_splitsWall.size()) ? m_splitsWall.at(idx) : -1;
    }
    /// 断点像素命中（供点击判定；-1=没点到）
    int splitHitTest(int x) const;

    /// 共享墙钟游标（P-57）：-1 = 不显示
    void setCursorMs(qint64 wallMs);
    qint64 cursorMs() const { return m_cursorMs; }
    /// A/B 选段（P-68 多机选段导出，墙钟域；-1 清除）
    void setABRegion(qint64 a, qint64 b) { m_abA = a; m_abB = b; update(); }
    qint64 abA() const { return m_abA; }
    qint64 abB() const { return m_abB; }
    bool hasAB() const { return m_abA >= 0 && m_abB > m_abA; }

    QSize sizeHint() const override { return {720, 220}; }

    /// v2.1：当前可视窗长度（ms）；== 全量 = 未缩放
    qint64 viewSpanMs() const { return m_viewSpan; }
    qint64 fullSpanMs() const { return qMax<qint64>(m_fullT1 - m_fullT0, 1); }
    bool   isZoomed()   const { return m_viewSpan > 0 && m_viewSpan < fullSpanMs(); }

signals:
    void laneActivated(const QString &videoId);   ///< 双击块（回单路分析）
    void scrubPreview(qint64 wallMs);   ///< 游标拖动中（实时追逐，高频）
    void seekCommit(qint64 wallMs);     ///< 点击/松手（一次性精确 seek）
    /// v3.2：点中了第 index 个变速断点（供上层提示「按 Del 删除」）
    void splitClicked(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool event(QEvent *event) override;           // ToolTip 命中
    void wheelEvent(QWheelEvent *event) override;  // v2.1：滚轮缩放 / Shift 平移
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Band { QRect rect; QString label; bool overlap; };

    void rebuildLayout();
    qint64 xToWall(int x) const;                  ///< 像素 → 墙钟（布局反解）
    static QString fmtSpan(qint64 ms);            // "4:21" / "1:02:03"
    QString laneTooltip(int i) const;

    QVector<CamLane> m_lanes;
    qint64 m_tolMs = 2000;

    // P-69：块条目（行+段号+矩形+被压重叠子区）；单段路 segIdx=0
    struct BlockRect {
        QString videoId;
        int row = 0;
        int segIdx = 0;
        bool merged = false;
        QRect rect;
        QVector<QRect> overlapClips;   ///< 「先起步者赢」被压区（斜纹+⚠）
    };
    QVector<BlockRect> m_rects;                   // 机位块（合并轨一行多块）
    QVector<Band> m_bands;                        // 重叠/缺口竖带
    QVector<QPair<int, QString>> m_tickLabels;    // x 像素 -> 刻度文字

    // 游标/拖动（P-57）
    qint64 m_cursorMs = -1;
    qint64 m_abA = -1, m_abB = -1;
    QVector<qint64> m_splitsWall;   ///< v2.9 变速断点（墙钟）
    int m_selectedSplit = -1;       ///< v3.2 选中的断点（-1=无）
    bool m_scrubbing = false;
    qint64 m_layoutT0 = 0;      // rebuildLayout 记录的轴域（xToWall 用）
    double m_layoutMsPerPx = 1.0;

    // v2.1：可视窗口（缩放/平移）。m_viewSpan==0 → 全览
    qint64 m_fullT0 = 0, m_fullT1 = 0;   ///< 数据全量范围（夹紧用）
    qint64 m_viewT0 = 0;
    qint64 m_viewSpan = 0;
};
