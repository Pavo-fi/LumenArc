/**
 * @file multicamview.cpp
 * @brief 多机时间线对齐只读视图实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "multicamview.h"

#include <QDateTime>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolTip>
#include <QVBoxLayout>

#include "app/case_manager.h"
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

    // --- 机位块（行内块位=墙钟、宽∝时长，画法同 ClipTimelineWidget） ---
    for (int i = 0; i < m_lanes.size(); ++i) {
        const auto &l = m_lanes[i];
        const int y = kTopMargin + i * (kLaneH + kLaneGap);
        const int x = kLabelW + int((l.wallStartMs - t0) / msPerPx);
        const int w = qMax(4, int((l.wallEndMs - l.wallStartMs) / msPerPx));
        m_rects.append({l.videoId,
                        QRect(x, y, qMin(w, kLabelW + availW - x), kLaneH)});
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

    // 机位行：标签列 + 块
    for (int i = 0; i < m_lanes.size(); ++i) {
        const auto &l = m_lanes[i];
        const QRect r = m_rects[i].second;
        const int y = kTopMargin + i * (kLaneH + kLaneGap);

        p.setPen(textSec);
        QFont lf = p.font();
        lf.setPixelSize(11);
        p.setFont(lf);
        p.drawText(QRect(4, y, kLabelW - 10, kLaneH),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1  %2").arg(l.videoId, l.fileName));

        const QColor fill = Theme::DataPalette[i % Theme::DataPalette.size()];
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(r, 4, 4);

        // 块内时长标签（宽度足够时）
        if (r.width() > 48) {
            p.setPen(QColor(Theme::AccentOnDark));
            QFont f = p.font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.drawText(r, Qt::AlignCenter, fmtSpan(l.streamDurationMs));
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
            if (m_rects[i].second.contains(he->pos())) {
                QToolTip::showText(he->globalPos(), laneTooltip(i), this);
                return true;
            }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

void MultiCamViewWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    for (const auto &pr : m_rects)
        if (pr.second.contains(event->pos())) {
            emit laneActivated(pr.first);   // 只读：仅供主窗打开该路
            return;
        }
    QWidget::mouseDoubleClickEvent(event);
}

void MultiCamViewWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildLayout();
    update();
}

// ---------------------------------------------------------------------------
// MultiCamDialog
// ---------------------------------------------------------------------------
MultiCamDialog::MultiCamDialog(const CaseManager *cm, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lang("多机时间线对齐（只读）",
                        "Multi-camera Timeline Alignment (read-only)"));
    setMinimumSize(820, 320);
    auto *lay = new QVBoxLayout(this);

    const auto lanes = buildCamLanes(cm->caseDir(), cm->meta().videos);
    // 未校时清单（徽标缓存口径，视图条目以 .vla 实读为准）
    QStringList uncal;
    for (const auto &v : cm->meta().videos)
        if (!v.hasCalibration)
            uncal << v.id;

    auto *view = new MultiCamViewWidget(this);
    view->setLanes(lanes);
    lay->addWidget(view, 1);
    connect(view, &MultiCamViewWidget::laneActivated,
            this, &MultiCamDialog::openVideoRequested);

    auto *legend = new QLabel(
        lang("块 = 该机位已分析时段的墙钟覆盖 · 红 = ≥2 机位同时覆盖（重叠）· "
             "灰纹 = 无机位覆盖（缺口）· 双击块打开该路",
             "Block = wall-clock coverage of analyzed span · red = ≥2 cameras "
             "overlap · hatch = no coverage (gap) · double-click to open"),
        this);
    legend->setWordWrap(true);
    legend->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(legend);

    if (lanes.size() < 2) {
        auto *hint = new QLabel(
            lang("⚠ 已校时机位不足 2 路，对齐视图仅供单路预览；"
                 "校时第二路后可查看重叠/缺口。",
                 "⚠ Fewer than 2 calibrated cameras; overlap/gap analysis "
                 "needs a second calibrated video."),
            this);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Accent));
        lay->addWidget(hint);
    }
    if (!uncal.isEmpty()) {
        auto *u = new QLabel(
            lang("未校时（不参与对齐）：%1",
                 "Not calibrated (excluded): %1").arg(uncal.join("、")), this);
        u->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextMuted));
        lay->addWidget(u);
    }

    auto *btnClose = new QPushButton(lang("关闭", "Close"), this);
    auto *row = new QHBoxLayout();
    row->addStretch(1);
    row->addWidget(btnClose);
    lay->addLayout(row);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
}
