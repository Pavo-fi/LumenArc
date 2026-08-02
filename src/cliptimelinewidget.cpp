/**
 * @file cliptimelinewidget.cpp
 * @brief 前处理-片段时间线条实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "cliptimelinewidget.h"
#include "theme.h"
#include "i18n.h"

#include <QPainter>
#include <QMouseEvent>
#include <QDateTime>

namespace {
constexpr int kMarginL = 10;
constexpr int kMarginR = 10;
constexpr int kTickLabelH = 16;     // 顶部刻度文字区
constexpr int kBlockH = 46;         // 片段块高
constexpr int kGapLabelH = 18;      // 底部缺口标签区
constexpr int kUnknownBlockW = 56;  // 无时间片段等宽
constexpr int kSeparatorW = 20;     // 已知区与未知区之间分隔
} // namespace

ClipTimelineWidget::ClipTimelineWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(kTickLabelH + kBlockH + kGapLabelH + 16);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
}

void ClipTimelineWidget::setClips(const QVector<TimelineClip> &clips,
                                  qint64 toleranceMs)
{
    m_clips = clips;
    m_tolMs = toleranceMs;
    rebuildLayout();
    update();
}

void ClipTimelineWidget::setSelectedPath(const QString &path)
{
    if (m_selected == path)
        return;
    m_selected = path;
    update();
}

QString ClipTimelineWidget::fmtSpan(qint64 ms)
{
    const qint64 s = qAbs(ms) / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3")
            .arg(s / 3600)
            .arg((s % 3600) / 60, 2, 10, QLatin1Char('0'))
            .arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2")
        .arg(s / 60)
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

void ClipTimelineWidget::rebuildLayout()
{
    m_rects.clear();
    m_regions.clear();
    m_tickLabels.clear();

    const int availW = width() - kMarginL - kMarginR;
    if (availW < 40 || m_clips.isEmpty())
        return;

    const int blockTop = kTickLabelH + 6;
    // --- 已知/未知时间片段拆分 ---
    int unknownCount = 0;
    qint64 t0 = std::numeric_limits<qint64>::max(), t1 = 0;
    for (const auto &c : m_clips) {
        if (c.timeKnown && c.startMs > 0 && c.durationMs > 0) {
            t0 = qMin(t0, c.startMs);
            t1 = qMax(t1, c.startMs + c.durationMs);
        } else {
            ++unknownCount;
        }
    }
    const bool hasKnown = t1 > t0;

    int knownW = availW;
    if (unknownCount > 0)
        knownW -= unknownCount * kUnknownBlockW + (hasKnown ? kSeparatorW : 0);
    if (knownW < 40)
        knownW = 40;

    double msPerPx = 1.0;
    if (hasKnown) {
        const qint64 span = qMax<qint64>(t1 - t0, 1);
        msPerPx = double(span) / double(knownW);
    }

    // --- 已知时间片段：位置=墙钟、宽∝时长 ---
    int unknownX = kMarginL + (hasKnown ? knownW + kSeparatorW : 0);
    QMap<QString, QRect> byPath;
    for (const auto &c : m_clips) {
        QRect r;
        if (c.timeKnown && hasKnown && c.startMs > 0 && c.durationMs > 0) {
            const int x = kMarginL + int((c.startMs - t0) / msPerPx);
            const int w = qMax(4, int(c.durationMs / msPerPx));
            r = QRect(x, blockTop, qMin(w, kMarginL + knownW - x), kBlockH);
        } else {
            r = QRect(unknownX, blockTop, kUnknownBlockW, kBlockH);
            unknownX += kUnknownBlockW;
        }
        m_rects.append({c.filePath, r});
        byPath.insert(c.filePath, r);
    }

    // --- 相邻片段缺口/重叠（仅同组相邻且双方有时间） ---
    for (int i = 1; i < m_clips.size(); ++i) {
        const TimelineClip &a = m_clips[i - 1];
        const TimelineClip &b = m_clips[i];
        if (a.groupIndex != b.groupIndex || !a.timeKnown || !b.timeKnown
            || a.startMs <= 0 || b.startMs <= 0)
            continue;
        const qint64 delta = b.startMs - (a.startMs + a.durationMs);
        if (delta > m_tolMs) {
            const int x = kMarginL + int((a.startMs + a.durationMs - t0) / msPerPx);
            const int w = qMax(3, int(delta / msPerPx));
            m_regions.append({QRect(x, blockTop, w, kBlockH),
                              lang("缺口 %1", "gap %1").arg(fmtSpan(delta)), false});
        } else if (delta < -m_tolMs) {
            const qint64 ovStart = b.startMs;
            const qint64 ovEnd = qMin(a.startMs + a.durationMs,
                                      b.startMs + b.durationMs);
            if (ovEnd > ovStart) {
                const int x = kMarginL + int((ovStart - t0) / msPerPx);
                const int w = qMax(3, int((ovEnd - ovStart) / msPerPx));
                m_regions.append({QRect(x, blockTop, w, kBlockH),
                                  lang("重叠 %1", "overlap %1").arg(fmtSpan(delta)), true});
            }
        }
    }

    // --- 时间刻度（首标签含日期） ---
    if (hasKnown) {
        static const qint64 steps[] = {1000, 5000, 15000, 30000,
                                       60000, 300000, 600000, 1800000, 3600000};
        qint64 step = 3600000;
        for (qint64 s : steps) {
            if (s / msPerPx >= 70) { step = s; break; }
        }
        const qint64 first = (t0 / step) * step;
        bool firstLabel = true;
        for (qint64 t = first; t <= t1; t += step) {
            const int x = kMarginL + int((t - t0) / msPerPx);
            const QDateTime dt = QDateTime::fromMSecsSinceEpoch(t, Qt::LocalTime);
            m_tickLabels.append({x, dt.toString(firstLabel
                ? QStringLiteral("MM-dd HH:mm") : QStringLiteral("HH:mm"))});
            firstLabel = false;
        }
    }
}

void ClipTimelineWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor textMut(Theme::TextMuted);
    const QColor textSec(Theme::TextSecond);

    // 刻度
    QFont small = p.font();
    small.setPixelSize(10);
    p.setFont(small);
    for (const auto &tk : m_tickLabels) {
        p.setPen(QPen(QColor(Theme::Border), 1));
        p.drawLine(tk.first, kTickLabelH, tk.first, kTickLabelH + 4);
        p.setPen(textMut);
        p.drawText(QRect(tk.first - 40, 0, 80, kTickLabelH),
                   Qt::AlignHCenter | Qt::AlignVCenter, tk.second);
    }

    if (m_clips.isEmpty()) {
        p.setPen(textMut);
        p.drawText(rect(), Qt::AlignCenter,
                   lang("（无片段）", "(no clips)"));
        return;
    }

    // 片段块
    for (const auto &pr : m_rects) {
        const QString &path = pr.first;
        const QRect &r = pr.second;
        const TimelineClip *clip = nullptr;
        for (const auto &c : m_clips)
            if (c.filePath == path) { clip = &c; break; }
        if (!clip)
            continue;

        QColor fill;
        QString label;
        if (clip->timeKnown && clip->startMs > 0) {
            fill = clip->groupIndex == 0
                ? QColor(Theme::Accent)
                : Theme::DataPalette[clip->groupIndex % Theme::DataPalette.size()];
            label = QString::number(clip->displayIndex);
        } else {
            fill = QColor(Theme::BgCard);
            label = QStringLiteral("?");
        }

        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, 4, 4);

        // 选中/存疑描边
        if (path == m_selected) {
            p.setPen(QPen(QColor(Theme::AccentHover), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        } else if (clip->dubious) {
            QPen pen(QColor(Theme::Danger), 2, Qt::DashLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        }

        p.setPen(clip->timeKnown ? QColor(Theme::AccentOnDark)
                                 : QColor(Theme::TextSecond));
        QFont f = p.font();
        f.setPixelSize(15);
        f.setBold(true);
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, label);
    }

    // 缺口/重叠标记
    for (const auto &rg : m_regions) {
        if (rg.overlap) {
            QColor red(Theme::Danger);
            red.setAlpha(110);
            p.setPen(Qt::NoPen);
            p.setBrush(red);
            p.drawRect(rg.rect);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(QBrush(QColor(Theme::TextMuted), Qt::BDiagPattern));
            p.drawRect(rg.rect);
        }
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(rg.overlap ? QColor(Theme::Danger) : QColor(Theme::TextSecond));
        const QRect lr(rg.rect.left() - 60, rg.rect.bottom() + 2, 120, kGapLabelH);
        p.drawText(lr, Qt::AlignHCenter | Qt::AlignTop, rg.label);
    }
}

void ClipTimelineWidget::mousePressEvent(QMouseEvent *event)
{
    for (const auto &pr : m_rects) {
        if (pr.second.contains(event->pos())) {
            emit clipClicked(pr.first);
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ClipTimelineWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildLayout();
    update();
}
