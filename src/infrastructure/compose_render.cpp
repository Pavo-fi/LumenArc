#include "compose_render.h"

#include "domain/roi_model.h"
#include "domain/timeline_model.h"
#include "theme.h"

#include <QFileInfo>
#include <QPainter>
#include <QtMath>
#include <cmath>

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
                    const ComposeOverlay &ov, qint64 cursorMs,
                    qint64 rangeStartMs, qint64 rangeEndMs)
{
    painter.save();
    painter.fillRect(stripRect, QColor(Theme::BgPanel));
    painter.setPen(QPen(QColor(Theme::Border), 1));
    painter.drawRect(stripRect.adjusted(0, 0, -1, -1));
    const QRect plot = stripRect.adjusted(48, 8, -10, 18);   // 左留白给 y 轴标签

    if (!ov.hasData()) {
        painter.setPen(QColor(Theme::TextMuted));
        painter.drawText(plot, Qt::AlignCenter,
                         QStringLiteral("（该片段无 .vla 分析数据，曲线条缺席）"));
        painter.restore();
        return;
    }
    // P2.8：全量时间段铺显（不滚动不缩放，对齐主窗图表/旧版导出口径）+ 游标移动
    if (rangeEndMs <= rangeStartMs)
        rangeEndMs = rangeStartMs + 1;
    const qint64 winStart = rangeStartMs, winEnd = rangeEndMs;
    auto xOf = [&](qint64 t) {
        return plot.x() + double(t - winStart) / double(winEnd - winStart) * plot.width();
    };

    // ---- 语谱带（顶部 40% 高，低频在下；蓝→红热力）----
    QRect curvePlot = plot;
    if (ov.audio.hasSpectrogram()) {
        const int specH = qMax(24, plot.height() * 2 / 5);
        const QRect specRect(plot.x(), plot.y(), plot.width(), specH);
        curvePlot.setTop(plot.y() + specH + 4);
        const auto &sg = ov.audio.spectrogram;      // [freq][time]
        const int fb = sg.size();
        const int tf = sg[0].size();
        const double tRes = ov.audio.safeTimeResolutionMs();
        // 该段时间轴对应的语谱帧范围
        const int f0 = qBound(0, int(winStart / tRes), tf - 1);
        const int f1 = qBound(f0 + 1, int(winEnd / tRes) + 1, tf);
        const double vMin = ov.audio.specMin, vMax = ov.audio.specMax;
        const double vSpan = (vMax > vMin) ? (vMax - vMin) : 1.0;
        QImage img(specRect.size(), QImage::Format_RGB32);
        for (int x = 0; x < specRect.width(); ++x) {
            const int fi = f0 + int(double(x) / specRect.width() * (f1 - f0));
            const int fiC = qBound(f0, fi, f1 - 1);
            for (int y = 0; y < specRect.height(); ++y) {
                const int bi = qBound(0, int((1.0 - double(y) / specRect.height()) * fb),
                                      fb - 1);
                const qreal v = (sg[bi].size() > fiC) ? sg[bi][fiC] : vMin;
                const double n = qBound(0.0, double((v - vMin) / vSpan), 1.0);
                // 简易热力：暗蓝→青→黄→红
                const int r = int(255 * qBound(0.0, n * 2.0 - 0.6, 1.0));
                const int g = int(255 * qBound(0.0, n * 1.6 - 0.2, 1.0));
                const int b = int(120 * (1.0 - n) + 30);
                img.setPixelColor(x, y, QColor(r, g, b));
            }
        }
        painter.drawImage(specRect, img);
        painter.setPen(QPen(QColor(Theme::Border), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(specRect);
    }

    // ---- 音量曲线（绿，0..maxV 自适应；画在曲线区 curvePlot）----
    if (ov.hasVol) {
        const auto pts = ov.audio.volumePointsForViewport(winStart, winEnd, 1200);
        if (pts.size() >= 2) {
            qreal maxV = 0.0;
            for (const auto &pt : pts) maxV = qMax(maxV, pt.y());
            if (maxV <= 0.0) maxV = 1.0;
            QPolygonF line;
            for (const auto &pt : pts)
                line << QPointF(xOf(qint64(pt.x())),
                                curvePlot.bottom() - pt.y() / maxV * curvePlot.height());
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
                                curvePlot.bottom() - row[i] / maxY * curvePlot.height());
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
        painter.drawLine(QPointF(x, curvePlot.top()), QPointF(x, curvePlot.bottom()));
        painter.setPen(lb.color);
        QFont f = painter.font();
        f.setPixelSize(11);
        painter.setFont(f);
        painter.drawText(QRectF(x + 2, curvePlot.top(), 120, 14),
                         Qt::AlignLeft | Qt::AlignTop, lb.text);
    }
    // ---- 游标（跟随当前时刻，贯穿语谱+曲线全区；白线+顶三角柄）----
    const qreal cx = xOf(qBound(winStart, cursorMs, winEnd));
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

// ============================================================================
// P2.7 标注轨：聚光灯 / 箭头 / 字幕
// ============================================================================
namespace {
/// 平滑插值（smoothstep 缓入缓出）
inline double easeInOut(double t) {
    t = qBound(0.0, t, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
inline QRectF lerpRect(const QRectF &a, const QRectF &b, double t) {
    return QRectF(a.x() + (b.x() - a.x()) * t,
                  a.y() + (b.y() - a.y()) * t,
                  a.width() + (b.width() - a.width()) * t,
                  a.height() + (b.height() - a.height()) * t);
}
}  // namespace

void drawAnnotations(QPainter &painter, const QRect &dispRect, const QImage &frame,
                     const QVector<SegmentExportEngine::Params::ComposeAnno> &annos,
                     qint64 srcMs)
{
    using ComposeAnno = SegmentExportEngine::Params::ComposeAnno;
    if (dispRect.isEmpty() || frame.isNull())
        return;
    painter.save();
    for (const auto &a : annos) {
        if (srcMs < a.inMs || srcMs >= a.outMs)
            continue;
        const QColor color = QColor::fromRgb(a.colorRgb);
        const double D = double(a.outMs - a.inMs);
        const double tRel = double(srcMs - a.inMs);

        if (a.type == ComposeAnno::Caption) {
            // 底部黑带白字（显示区底上方 64px，避开 OSD/曲线条）
            QFont f = painter.font();
            f.setPixelSize(qBound(18, dispRect.height() / 22, 44));
            f.setBold(true);
            painter.setFont(f);
            const QRect band(dispRect.x() + 10, dispRect.bottom() - 70,
                             dispRect.width() - 20, 40);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 165));
            painter.drawRoundedRect(band, 5, 5);
            painter.setPen(Qt::white);
            painter.drawText(band, Qt::AlignCenter,
                             painter.fontMetrics().elidedText(a.text, Qt::ElideRight,
                                                              band.width() - 24));
            continue;
        }

        // 归一化 → 显示矩形 / 源帧像素
        const QRectF tgtDisp(dispRect.x() + a.rect.x() * dispRect.width(),
                             dispRect.y() + a.rect.y() * dispRect.height(),
                             a.rect.width() * dispRect.width(),
                             a.rect.height() * dispRect.height());
        const QRectF tgtSrc(a.rect.x() * frame.width(), a.rect.y() * frame.height(),
                            a.rect.width() * frame.width(),
                            a.rect.height() * frame.height());

        if (a.type == ComposeAnno::Arrow) {
            // rect 左上=起点，右下=终点
            const QPointF pa(dispRect.x() + a.rect.left() * dispRect.width(),
                             dispRect.y() + a.rect.top() * dispRect.height());
            const QPointF pb(dispRect.x() + a.rect.right() * dispRect.width(),
                             dispRect.y() + a.rect.bottom() * dispRect.height());
            painter.setPen(QPen(color, 5, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(pa, pb);
            // 箭头头
            const double ang = std::atan2(pb.y() - pa.y(), pb.x() - pa.x());
            const double hl = 20.0;
            QVector<QPointF> head;
            head << pb
                 << QPointF(pb.x() - hl * std::cos(ang - 0.42),
                            pb.y() - hl * std::sin(ang - 0.42))
                 << QPointF(pb.x() - hl * std::cos(ang + 0.42),
                            pb.y() - hl * std::sin(ang + 0.42));
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF(head));
            continue;
        }

        // Spotlight（P2.8 拍板：放大上限=屏幕 50% 面积而非满屏）：
        // 剩余区变暗（前 500ms 淡入/末 400ms 淡出，全程保持）+聚焦框平滑放大至
        // 居中 50% 面积（等比）——突出重点同时保留周边情境
        const double dimIn = qBound(0.0, tRel / 500.0, 1.0);
        const double dimOut = qBound(0.0, (D - tRel) / 400.0, 1.0);
        const double dimP = qMin(dimIn, dimOut);
        const double zoomSpan = qMax(600.0, D - 900.0);
        const double zoomP = easeInOut(qBound(0.0, (tRel - 400.0) / zoomSpan, 1.0));
        // 终点矩形：dispRect 居中、面积约 50%（边长 ×0.7071），保持聚焦框自身宽高比
        const double boxW = dispRect.width() * 0.7071, boxH = dispRect.height() * 0.7071;
        const double fit = qMin(boxW / qMax(1.0, tgtDisp.width()),
                                boxH / qMax(1.0, tgtDisp.height()));
        const QRectF endRect(dispRect.center().x() - tgtDisp.width() * fit / 2,
                             dispRect.center().y() - tgtDisp.height() * fit / 2,
                             tgtDisp.width() * fit, tgtDisp.height() * fit);
        if (dimP > 0.01) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, int(145 * dimP)));
            painter.drawRect(dispRect);   // 剩余区域变暗（聚焦区随后提亮覆盖）
        }
        const QRectF dest = lerpRect(tgtDisp, endRect, zoomP);
        painter.drawImage(dest, frame, tgtSrc);
        painter.setPen(QPen(color, 3));   // 聚焦框边常驻
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(dest);
    }
    painter.restore();
}
