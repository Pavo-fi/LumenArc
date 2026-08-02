/**
 * @file cliptimelinewidget.h
 * @brief 前处理-片段时间线条（自绘控件，纯展示，无业务状态）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_UI_REDESIGN_CN.md §3 第②幕。
 * 块位置=墙钟起点、宽∝时长；缺口=灰纹区+标签；重叠=红色叠加；
 * 无墙钟信息的片段排列在末尾等宽"?"区（用户拍板的退化模式）。
 */
#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

struct TimelineClip {
    QString filePath;
    qint64  startMs = 0;        // 墙钟起点（timeKnown=false 时忽略）
    qint64  durationMs = 0;
    bool    timeKnown = false;
    bool    dubious = false;    // 时长存疑/截断文件
    int     groupIndex = 0;     // 分组序号（>1 组时着色区分）
    int     displayIndex = 0;   // 组内序号（块内编号，1 起）
};

class ClipTimelineWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ClipTimelineWidget(QWidget *parent = nullptr);

    /// toleranceMs：连续性容差（与 domain kContinuityToleranceMs 对齐）
    void setClips(const QVector<TimelineClip> &clips, qint64 toleranceMs);
    void setSelectedPath(const QString &path);

    QSize sizeHint() const override { return {400, 104}; }

signals:
    void clipClicked(const QString &filePath);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Region { QRect rect; QString label; bool overlap; };

    void rebuildLayout();
    static QString fmtSpan(qint64 ms);          // 缺口/重叠时长 "4:21" / "1:02:03"

    QVector<TimelineClip> m_clips;
    qint64 m_tolMs = 2000;
    QString m_selected;

    QVector<QPair<QString, QRect>> m_rects;     // filePath -> 块矩形（点击命中）
    QVector<Region> m_regions;                  // 缺口/重叠标记区
    QVector<QPair<int, QString>> m_tickLabels;  // x 像素 -> 刻度文字
};
