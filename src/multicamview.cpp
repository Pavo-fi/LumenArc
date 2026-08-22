/**
 * @file multicamview.cpp
 * @brief 多机时间线条实现（P-57：游标 + 拖动 seek；原只读对话框已删）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.1
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "multicamview.h"

#include <QDateTime>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include "i18n.h"
#include "theme.h"

namespace {
constexpr int kLabelW = 168;    // 左侧机位标签列宽
constexpr int kMarginR = 12;
constexpr int kLaneH = 34;      // 机位行高
constexpr int kLaneGap = 8;
constexpr int kTopMargin = 8;
constexpr int kTickLabelH = 20; // 底部刻度文字区
constexpr int kBandLabelH = 16; // 带标签区（缺口/重叠时长）
} // namespace

// ---------------------------------------------------------------------------
// MultiCamViewWidget
// ---------------------------------------------------------------------------
MultiCamViewWidget::MultiCamViewWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
}

void MultiCamViewWidget::setLanes(const QVector<CamLane> &lanes,
                                  qint64 toleranceMs)
{
    m_lanes = lanes;
    m_tolMs = toleranceMs;
    setMinimumHeight(kTopMargin * 2 + int(lanes.size()) * (kLaneH + kLaneGap)
                     + kBandLabelH + kTickLabelH + 8);
    rebuildLayout();
    update();
}

void MultiCamViewWidget::setCursorMs(qint64 wallMs)
{
    if (m_cursorMs == wallMs)
        return;
    m_cursorMs = wallMs;
    update();
}

qint64 MultiCamViewWidget::xToWall(int x) const
{
    const qint64 w = m_layoutT0 + static_cast<qint64>(
        (x - kLabelW) * m_layoutMsPerPx);
    return qMax<qint64>(w, m_layoutT0);
}

QString MultiCamViewWidget::fmtSpan(qint64 ms)
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

void MultiCamViewWidget::rebuildLayout()
{
    m_rects.clear();
    m_bands.clear();
    m_tickLabels.clear();

    const int availW = width() - kLabelW - kMarginR;
    if (availW < 40 || m_lanes.isEmpty())
        return;

    qint64 t0 = std::numeric_limits<qint64>::max(), t1 = 0;
    for (const auto &l : m_lanes) {
        t0 = qMin(t0, l.wallStartMs);
        t1 = qMax(t1, l.wallEndMs);
    }
    const qint64 span = qMax<qint64>(t1 - t0, 1);
    const double msPerPx = double(span) / double(availW);
    m_layoutT0 = t0;                // P-57：xToWall 反解用
    m_layoutMsPerPx = msPerPx;

    // --- 机位块（行内块位=墙钟、宽∝时长，画法同 ClipTimelineWidget） ---
    // P-69：合并轨一行多块（逐段），重叠段「先起步者赢」——后被压段重叠区
    // 记 overlapClips（paint 斜纹+⚠）
    for (int i = 0; i < m_lanes.size(); ++i) {
        const auto &l = m_lanes[i];
        const int y = kTopMargin + i * (kLaneH + kLaneGap);
        if (l.segs.isEmpty()) {
            const int x = kLabelW + int((l.wallStartMs - t0) / msPerPx);
            const int w = qMax(4, int((l.wallEndMs - l.wallStartMs) / msPerPx));
            BlockRect br;
            br.videoId = l.videoId;
            br.row = i;
            br.rect = QRect(x, y, qMin(w, kLabelW + availW - x), kLaneH);
            m_rects.append(br);
            continue;
        }
        const int base = m_rects.size();
        for (const auto &sg : l.segs) {
            const int x = kLabelW + int((sg.wallStartMs - t0) / msPerPx);
            const int w = qMax(4, int((sg.wallEndMs - sg.wallStartMs) / msPerPx));
            BlockRect br;
            br.videoId = l.videoId;
            br.row = i;
            br.segIdx = sg.segIdx;
            br.merged = true;
            br.rect = QRect(x, y, qMin(w, kLabelW + availW - x), kLaneH);
            m_rects.append(br);
        }
        // 行内两两重叠：输家（起步晚者）记被压区
        for (int a = base; a < m_rects.size(); ++a)
            for (int b = a + 1; b < m_rects.size(); ++b) {
                const qint64 sa = l.segs[m_rects[a].segIdx].wallStartMs;
                const qint64 sb = l.segs[m_rects[b].segIdx].wallStartMs;
                const int loser = (sa == sb) ? qMax(a, b) : (sa < sb ? b : a);
                const int winner = (loser == a) ? b : a;
                const QRect ov = m_rects[a].rect.intersected(m_rects[b].rect);
                if (!ov.isEmpty())
                    m_rects[loser].overlapClips.append(ov);
                Q_UNUSED(winner);
            }
    }

    // --- 覆盖率扫掠：≥2 重叠（红）/ 0 缺口（灰纹），跨行竖带 ---
    QVector<QPair<qint64, int>> ev;   // (墙钟, +1/-1)
    for (const auto &l : m_lanes) {
        ev.append({l.wallStartMs, +1});
        ev.append({l.wallEndMs, -1});
    }
    std::sort(ev.begin(), ev.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    const int bandTop = kTopMargin - 2;
    const int bandBot = kTopMargin + int(m_lanes.size()) * (kLaneH + kLaneGap)
                        - kLaneGap + 2;
    int cover = 0;
    for (int i = 0; i < ev.size(); ++i) {
        const qint64 segStart = ev[i].first;
        cover += ev[i].second;
        if (i + 1 >= ev.size())
            break;
        const qint64 segEnd = ev[i + 1].first;
        const qint64 delta = segEnd - segStart;
        if (delta <= m_tolMs)
            continue;
        const int x = kLabelW + int((segStart - t0) / msPerPx);
        const int w = qMax(3, int(delta / msPerPx));
        if (cover >= 2)
            m_bands.append({QRect(x, bandTop, w, bandBot - bandTop),
                            lang("重叠 %1", "overlap %1").arg(fmtSpan(delta)),
                            true});
        else if (cover == 0)
            m_bands.append({QRect(x, bandTop, w, bandBot - bandTop),
                            lang("缺口 %1", "gap %1").arg(fmtSpan(delta)),
                            false});
    }

    // --- 墙钟刻度（首标签含日期） ---
    static const qint64 steps[] = {1000, 5000, 15000, 30000, 60000, 300000,
                                   600000, 1800000, 3600000, 21600000,
                                   43200000, 86400000};
    qint64 step = 86400000;
    for (qint64 s : steps) {
        if (s / msPerPx >= 70) { step = s; break; }
    }
    const qint64 first = (t0 / step) * step;
    bool firstLabel = true;
    for (qint64 t = first; t <= t1; t += step) {
        const int x = kLabelW + int((t - t0) / msPerPx);
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(t, Qt::LocalTime);
        const QString fmt = step >= 86400000
            ? (firstLabel ? QStringLiteral("yyyy-MM-dd")
                          : QStringLiteral("MM-dd"))
            : (firstLabel ? QStringLiteral("MM-dd HH:mm")
                          : QStringLiteral("HH:mm"));
        m_tickLabels.append({x, dt.toString(fmt)});
        firstLabel = false;
    }
}

void MultiCamViewWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor textMut(Theme::TextMuted);
    const QColor textSec(Theme::TextSecond);

    if (m_lanes.isEmpty()) {
        p.setPen(textMut);
        p.drawText(rect(), Qt::AlignCenter,
                   lang("（无已校时机位）", "(no calibrated cameras)"));
        return;
    }

    // 覆盖率竖带（先画带后画块，块保持可读）
    for (const auto &b : m_bands) {
        p.setPen(Qt::NoPen);
        if (b.overlap) {
            QColor red(Theme::Danger);
            red.setAlpha(70);
            p.setBrush(red);
        } else {
            p.setBrush(QBrush(QColor(Theme::TextMuted), Qt::BDiagPattern));
        }
        p.drawRect(b.rect);
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(b.overlap ? QColor(Theme::Danger) : textSec);
        const QRect lr(b.rect.left() - 60, b.rect.bottom() + 2, 120, kBandLabelH);
        p.drawText(lr, Qt::AlignHCenter | Qt::AlignTop, b.label);
    }

    // 机位行标签列（块在下一步按条目画——合并轨一行多块）
    for (int i = 0; i < m_lanes.size(); ++i) {
        const auto &l = m_lanes[i];
        const int y = kTopMargin + i * (kLaneH + kLaneGap);
        p.setPen(textSec);
        QFont lf = p.font();
        lf.setPixelSize(11);
        p.setFont(lf);
        p.drawText(QRect(4, y, kLabelW - 10, kLaneH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1  %2").arg(l.videoId, l.fileName));
    }

    // 机位块（P-69：合并轨逐段多色——同色调按段号明暗交替；被压重叠区斜纹+⚠）
    for (const auto &br : m_rects) {
        const QRect r = br.rect;
        QColor fill = Theme::DataPalette[br.row % Theme::DataPalette.size()];
        if (br.merged && (br.segIdx % 2) == 1)
            fill = fill.lighter(125);   // 段号明暗交替（多色块拍板）
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, 4, 4);

        // 「先起步者赢」被压区：斜纹覆盖 + 左缘 ⚠
        for (const QRect &ov : br.overlapClips) {
            p.setBrush(QBrush(QColor(Theme::TextMuted), Qt::BDiagPattern));
            p.drawRect(ov);
            p.setPen(QColor(Theme::Danger));
            QFont wf = p.font();
            wf.setPixelSize(12);
            wf.setBold(true);
            p.setFont(wf);
            p.drawText(QRect(ov.left(), ov.top(), 16, ov.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("⚠"));
            p.setPen(Qt::NoPen);
        }

        // 块内标签：单段路=时长；合并轨=段号+时长
        if (r.width() > 48) {
            p.setPen(QColor(Theme::AccentOnDark));
            QFont f = p.font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            const auto &l = m_lanes[br.row];
            const QString txt = br.merged
                ? QStringLiteral("段%1 %2").arg(br.segIdx + 1)
                      .arg(fmtSpan(l.segs[br.segIdx].wallEndMs
                                   - l.segs[br.segIdx].wallStartMs))
                : fmtSpan(l.streamDurationMs);
            p.drawText(r, Qt::AlignCenter, txt);
        }
    }

    // 墙钟刻度
    QFont small = p.font();
    small.setPixelSize(10);
    p.setFont(small);
    const int tickY = kTopMargin + int(m_lanes.size()) * (kLaneH + kLaneGap)
                      + kBandLabelH;
    for (const auto &tk : m_tickLabels) {
        p.setPen(QPen(QColor(Theme::Border), 1));
        p.drawLine(tk.first, tickY, tk.first, tickY + 4);
        p.setPen(textMut);
        p.drawText(QRect(tk.first - 48, tickY + 4, 96, kTickLabelH),
                   Qt::AlignHCenter | Qt::AlignTop, tk.second);
    }

    // A/B 选段底纹与边界线（P-68：先于游标绘制，游标压顶）
    if (hasAB() && m_layoutMsPerPx > 0) {
        const int ax = kLabelW + int((m_abA - m_layoutT0) / m_layoutMsPerPx);
        const int bx = kLabelW + int((m_abB - m_layoutT0) / m_layoutMsPerPx);
        const int x1 = qMax(ax, int(kLabelW));
        const int x2 = qMin(bx, width() - kMarginR);
        if (x2 > x1) {
            QColor fill(Theme::Accent);
            fill.setAlpha(28);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawRect(QRect(x1, kTopMargin - 2, x2 - x1, tickY + 2 - kTopMargin + 2));
        }
        p.setPen(QPen(QColor(Theme::Info), 2, Qt::DashLine));
        if (ax >= kLabelW && ax <= width() - kMarginR)
            p.drawLine(ax, kTopMargin - 2, ax, tickY + 2);
        if (bx >= kLabelW && bx <= width() - kMarginR)
            p.drawLine(bx, kTopMargin - 2, bx, tickY + 2);
    }

    // 共享墙钟游标（P-57 模式A）：跨行竖线 + 顶部时刻标签
    if (m_cursorMs >= 0 && m_layoutMsPerPx > 0) {
        const int cx = kLabelW + int((m_cursorMs - m_layoutT0) / m_layoutMsPerPx);
        if (cx >= kLabelW && cx <= width() - kMarginR) {
            p.setPen(QPen(QColor(Theme::Accent), 2));
            p.drawLine(cx, kTopMargin - 2, cx, tickY + 2);
            const QString t = QDateTime::fromMSecsSinceEpoch(m_cursorMs, Qt::LocalTime)
                                  .toString(QStringLiteral("MM-dd HH:mm:ss"));
            QFont cf = p.font();
            cf.setPixelSize(11);
            cf.setBold(true);
            p.setFont(cf);
            const int tw = p.fontMetrics().horizontalAdvance(t) + 10;
            const int lx = qBound(kLabelW, cx - tw / 2, width() - kMarginR - tw);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(Theme::Accent));
            p.drawRoundedRect(QRect(lx, kTopMargin - 2 - 18, tw, 16), 3, 3);
            p.setPen(QColor(Theme::AccentOnDark));
            p.drawText(QRect(lx, kTopMargin - 2 - 18, tw, 16), Qt::AlignCenter, t);
        }
    }
}

QString MultiCamViewWidget::laneTooltip(int i) const
{
    const auto &l = m_lanes[i];
    const auto fmt = [](qint64 ms) {
        return QDateTime::fromMSecsSinceEpoch(ms, Qt::LocalTime)
            .toString(QStringLiteral("MM-dd HH:mm:ss"));
    };
    QString tip = QStringLiteral("%1  %2\n").arg(l.videoId, l.fileName);
    tip += lang("墙钟覆盖：%1 ~ %2", "Wall coverage: %1 ~ %2")
               .arg(fmt(l.wallStartMs), fmt(l.wallEndMs));
    tip += QStringLiteral("\n") + lang("已分析时长：%1", "Analyzed span: %1")
               .arg(fmtSpan(l.streamDurationMs));
    if (!l.calibrationSummary.isEmpty())
        tip += QStringLiteral("\n") + lang("校时：", "Calibration: ")
               + l.calibrationSummary;
    return tip;
}

bool MultiCamViewWidget::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        for (int i = 0; i < m_rects.size(); ++i)
            if (m_rects[i].rect.contains(he->pos())) {
                QToolTip::showText(he->globalPos(), laneTooltip(m_rects[i].row)
                    + (m_rects[i].merged
                       ? QStringLiteral("\n段 %1（合并轨）").arg(m_rects[i].segIdx + 1)
                       : QString()),
                    this);
                return true;
            }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void MultiCamViewWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    for (const auto &br : m_rects)
        if (br.rect.contains(event->pos())) {
            emit laneActivated(br.videoId);   // 回单路分析（U-6）
            return;
        }
    QWidget::mouseDoubleClickEvent(event);
}

void MultiCamViewWidget::mousePressEvent(QMouseEvent *event)
{
    // 游标拖动（P-57 U-4 实时追逐）：仅在条图区（标签列右侧）起效
    if (event->button() == Qt::LeftButton && event->pos().x() >= kLabelW
        && !m_lanes.isEmpty()) {
        m_scrubbing = true;
        emit scrubPreview(xToWall(event->pos().x()));
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void MultiCamViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_scrubbing) {
        emit scrubPreview(xToWall(event->pos().x()));
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MultiCamViewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_scrubbing && event->button() == Qt::LeftButton) {
        m_scrubbing = false;
        emit seekCommit(xToWall(event->pos().x()));
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void MultiCamViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildLayout();
    update();
}
