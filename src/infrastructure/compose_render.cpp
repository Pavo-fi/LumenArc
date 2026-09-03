#include "compose_render.h"

#include "domain/roi_model.h"
#include "domain/timeline_model.h"
#include "theme.h"

#include <QFileInfo>
#include <QPainter>
#include <QtMath>

ComposeOverlay loadComposeOverlay(const QString &vlaPath)
{
    ComposeOverlay ov;
    if (vlaPath.isEmpty() || !QFileInfo::exists(vlaPath))
        return ov;
    TimelineModel model;
    QVector<QRect> regions;
    QVector<QPolygon> polygons;
    QVector<ChartLabel> labels;
    if (!model.loadFromFile(vlaPath, &regions, nullptr, nullptr, &labels,
                            nullptr, nullptr, &polygons))
        return ov;
    ov.loaded = true;
    ov.rois = regions;
    ov.polygons = polygons;
    ov.labels = labels;
    const AnalysisSnapshot snap = model.snapshot();
    if (snap.hasLuminance()) {
        ov.lumTs = snap.timestamps;
        ov.lumRows = snap.lumRows();
        ov.hasLum = !ov.lumTs.isEmpty() && !ov.lumRows.isEmpty();
    }
    const auto it = snap.channels.constFind(AnalysisChannels::audio());
    if (it != snap.channels.constEnd() && !it->audio.volume.isEmpty()) {
        ov.audio = it->audio;
        ov.hasVol = true;
    }
    return ov;
}

void drawRoiOverlay(QPainter &painter, const QRect &dstRect, const QSize &srcSize,
                    const ComposeOverlay &ov)
{
    if (!ov.loaded || (ov.rois.isEmpty() && ov.polygons.isEmpty())
        || srcSize.isEmpty() || dstRect.isEmpty())
        return;
    // KeepAspectRatio 居中映射：源像素 → 目标矩形
    const double scale = qMin(double(dstRect.width()) / srcSize.width(),
                              double(dstRect.height()) / srcSize.height());
    const QSizeF fit(srcSize.width() * scale, srcSize.height() * scale);
    const QPointF off(dstRect.x() + (dstRect.width() - fit.width()) / 2.0,
                      dstRect.y() + (dstRect.height() - fit.height()) / 2.0);
    auto mapPt = [&](const QPoint &pt) {
        return QPointF(off.x() + pt.x() * scale, off.y() + pt.y() * scale);
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const int w = qMax(2, int(2.0 * scale + 0.5));
    QFont f = painter.font();
    f.setPixelSize(qMax(12, 7 * w));
    painter.setFont(f);
    painter.setClipRect(dstRect);
    for (int i = 0; i < ov.rois.size(); ++i) {
        const QRect &rc = ov.rois[i];
        const QRectF mapped(mapPt(rc.topLeft()),
                            QSizeF(rc.width() * scale, rc.height() * scale));
        const QColor base = RoiModel::regionColor(i);
        QColor fill = base;
        fill.setAlpha(28);
        painter.fillRect(mapped, fill);
        painter.setPen(QPen(base, w));
        painter.drawRect(mapped);
        painter.setPen(Qt::white);
        painter.drawText(mapped.topLeft() + QPointF(2 * w, 8 * w),
                         QStringLiteral("R%1").arg(i + 1));
    }
    for (int i = 0; i < ov.polygons.size(); ++i) {
        QPolygonF poly;
        for (const QPoint &pt : ov.polygons[i])
            poly << mapPt(pt);
        const QColor base = RoiModel::regionColor(ov.rois.size() + i);
        QColor fill = base;
        fill.setAlpha(28);
        painter.setBrush(fill);
        painter.setPen(QPen(base, w));
        painter.drawPolygon(poly);
        painter.setBrush(Qt::NoBrush);
    }
    painter.restore();
}

void drawChartStrip(QPainter &painter, const QRect &stripRect,
                    const ComposeOverlay &ov, qint64 centerMs, qint64 windowMs)
{
    painter.save();
    painter.fillRect(stripRect, QColor(Theme::BgPanel));
    painter.setPen(QPen(QColor(Theme::Border), 1));
    painter.drawRect(stripRect.adjusted(0, 0, -1, -1));
    const QRect plot = stripRect.adjusted(48, 8, -10, 18);   // 左留白给 y 轴标签

    if (!ov.hasData()) {
        painter.setPen(QColor(Theme::TextMuted));
        painter.drawText(plot, Qt::AlignCenter,
                         QStringLiteral("（该片段无 .vla 分析数据，曲线滚动条缺席）"));
        painter.restore();
        return;
    }
    // 窗口：游标固定在 2/3 处（回看 2/3，前瞻 1/3）
    const qint64 winStart = centerMs - windowMs * 2 / 3;
    const qint64 winEnd = centerMs + windowMs / 3;
    auto xOf = [&](qint64 t) {
        return plot.x() + double(t - winStart) / double(windowMs) * plot.width();
    };

    // ---- 音量曲线（绿，0..maxV 自适应）----
    if (ov.hasVol) {
        const auto pts = ov.audio.volumePointsForViewport(winStart, winEnd, 1200);
        if (pts.size() >= 2) {
            qreal maxV = 0.0;
            for (const auto &pt : pts) maxV = qMax(maxV, pt.y());
            if (maxV <= 0.0) maxV = 1.0;
            QPolygonF line;
            for (const auto &pt : pts)
                line << QPointF(xOf(qint64(pt.x())),
                                plot.bottom() - pt.y() / maxV * plot.height());
            painter.setPen(QPen(QColor(80, 200, 120, 200), 1.5));
            painter.drawPolyline(line);
        }
    }
    // ---- 亮度折线（逐 ROI 行，RoiModel 同色）----
    if (ov.hasLum) {
        qreal maxY = 1.0;
        for (const auto &row : ov.lumRows)
            for (qreal v : row) maxY = qMax(maxY, v);
        const int n = ov.lumTs.size();
        for (int r = 0; r < ov.lumRows.size(); ++r) {
            const auto &row = ov.lumRows[r];
            QPolygonF line;
            const int stride = qMax(1, n / 1200);
            for (int i = 0; i < n; i += stride) {
                const qint64 t = ov.lumTs[i];
                if (t < winStart || t > winEnd || i >= row.size())
                    continue;
                line << QPointF(xOf(t),
                                plot.bottom() - row[i] / maxY * plot.height());
            }
            if (line.size() >= 2) {
                painter.setPen(QPen(RoiModel::regionColor(r), 1.5));
                painter.drawPolyline(line);
            }
        }
    }
    // ---- 标签竖标（同色竖线+文字，窗内才画）----
    for (const auto &lb : ov.labels) {
        if (lb.timeMs < winStart || lb.timeMs > winEnd)
            continue;
        const qreal x = xOf(lb.timeMs);
        painter.setPen(QPen(lb.color, 1.5, Qt::DashLine));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.setPen(lb.color);
        QFont f = painter.font();
        f.setPixelSize(11);
        painter.setFont(f);
        painter.drawText(QRectF(x + 2, plot.top(), 120, 14),
                         Qt::AlignLeft | Qt::AlignTop, lb.text);
    }
    // ---- 游标（2/3 处，白线+顶三角柄）----
    const qreal cx = xOf(centerMs);
    painter.setPen(QPen(Qt::white, 2));
    painter.drawLine(QPointF(cx, plot.top()), QPointF(cx, plot.bottom()));
    QPolygonF tri;
    tri << QPointF(cx - 6, stripRect.top()) << QPointF(cx + 6, stripRect.top())
        << QPointF(cx, plot.top());
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawPolygon(tri);
    // 底部时刻注记（窗口起止）
    painter.setPen(QColor(Theme::TextMuted));
    QFont f = painter.font();
    f.setPixelSize(11);
    painter.setFont(f);
    auto fmtS = [](qint64 ms) {
        return QStringLiteral("%1:%2").arg(ms / 60000, 2, 10, QLatin1Char('0'))
                                      .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
    };
    painter.drawText(stripRect.adjusted(4, 0, 0, -2), Qt::AlignLeft | Qt::AlignBottom,
                     fmtS(winStart));
    painter.drawText(stripRect.adjusted(0, 0, -4, -2), Qt::AlignRight | Qt::AlignBottom,
                     fmtS(winEnd));
    painter.restore();
}
