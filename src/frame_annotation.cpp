/**
 * @file frame_annotation.cpp
 * @brief v1.17.0 P-80 C0：自 videowidget.cpp 逐字迁出（行为冻结，仅去类前缀）
 */
#include "frame_annotation.h"

#include "theme.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"

namespace FrameAnnotation {
QSize displaySizeForRotation(const QSize &videoSize, int rotation)
{
    if (rotation == 90 || rotation == 270)
        return QSize(videoSize.height(), videoSize.width());
    return videoSize;
}

QPoint rotateStoredToDisplay(const QPoint &p, const QSize &videoSize,
                                            int rotation)
{
    const int W = videoSize.width(), H = videoSize.height();
    switch (rotation) {
    case 90:  return QPoint(H - 1 - p.y(), p.x());
    case 180: return QPoint(W - 1 - p.x(), H - 1 - p.y());
    case 270: return QPoint(p.y(), W - 1 - p.x());
    default:  return p;
    }
}

QPoint mapStoredPointToFrame(const QPoint &stored,
                                            const QSize &frameSize,
                                            const QSize &videoSize, int rotation)
{
    const QSize disp = displaySizeForRotation(videoSize, rotation);
    if (disp.isEmpty() || frameSize.isEmpty())
        return stored;
    const QPoint d = rotateStoredToDisplay(stored, videoSize, rotation);
    return QPoint(d.x() * frameSize.width() / disp.width(),
                  d.y() * frameSize.height() / disp.height());
}

QRect mapStoredRectToFrame(const QRect &stored, const QSize &frameSize,
                                          const QSize &videoSize, int rotation)
{
    return QRect(mapStoredPointToFrame(stored.topLeft(), frameSize, videoSize, rotation),
                 mapStoredPointToFrame(stored.bottomRight(), frameSize, videoSize,
                                       rotation)).normalized();
}

void drawMagnifierIndicator(QPainter &painter, const QRect &rect,
                                           qreal zoom, int penWidth, int fontPx)
{
    if (!rect.isValid() || rect.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);   // 括号走整数像素，不抗锯齿更锐

    const QColor accent(Theme::Accent);
    // 括号臂长：短边 1/4，夹在 [8, 40]（快照高分辨率由调用方放大 penWidth，
    // 臂长仍按矩形比例，与屏上观感一致）
    const int arm = qBound(8, qMin(rect.width(), rect.height()) / 4, 40);
    const int w = qMax(1, penWidth);

    auto brackets = [&](const QPoint &off, const QColor &color, int width) {
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::SquareCap));
        const int l = rect.left() + off.x(), t = rect.top() + off.y();
        const int r = rect.right() + off.x(), b = rect.bottom() + off.y();
        // 四角括号：每角两线段（横向 + 纵向），不闭合、不进框内
        painter.drawLine(l, t, l + arm, t);          // 左上横
        painter.drawLine(l, t, l, t + arm);          // 左上纵
        painter.drawLine(r, t, r - arm, t);          // 右上横
        painter.drawLine(r, t, r, t + arm);          // 右上纵
        painter.drawLine(l, b, l + arm, b);          // 左下横
        painter.drawLine(l, b, l, b - arm);          // 左下纵
        painter.drawLine(r, b, r - arm, b);          // 右下横
        painter.drawLine(r, b, r, b - arm);          // 右下纵
    };
    brackets(QPoint(w, w), QColor(0, 0, 0, 160), w);   // 衬影（亮底可读，随主线同宽）
    brackets(QPoint(0, 0), accent, w);                 // 主体

    // 倍率徽章：深底金字「2.0×」，贴框外右上角；上方无空间则改框内左上
    if (zoom > 0.0) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QString txt = QStringLiteral("%1×").arg(zoom, 0, 'f', 1);
        QFont f = painter.font();
        f.setPixelSize(qMax(8, fontPx));
        f.setBold(true);
        painter.setFont(f);
        const QFontMetrics fm(f);
        const QRect textRect = fm.boundingRect(txt);
        const int padX = qMax(3, fontPx / 3), padY = qMax(2, fontPx / 5);
        const int bw = textRect.width() + padX * 2;
        const int bh = textRect.height() + padY * 2;
        QRect badge(rect.right() + 1 - bw, rect.top() - bh - 2, bw, bh);   // 框外右上
        if (badge.top() < 0)   // 无空间：框内左上
            badge = QRect(rect.left() + 2, rect.top() + 2, bw, bh);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, 200));
        painter.drawRoundedRect(badge, 3, 3);
        painter.setPen(accent);
        painter.drawText(badge, Qt::AlignCenter, txt);
    }
    painter.restore();
}

void burnAnnotations(QPainter &painter, const QSize &frameSize,
                                    const QSize &videoSize, int rotation,
                                    const RoiModel *regions,
                                    const RoiModel *polygons,
                                    const GuideLineModel *guideLines,
                                    int penWidth)
{
    if (frameSize.isEmpty() || videoSize.isEmpty())
        return;
    const QSize disp = displaySizeForRotation(videoSize, rotation);
    if (disp.isEmpty())
        return;

    // 存储系点 → 目标图像坐标（两步：旋转到显示系 → 等比缩放到帧尺寸）
    auto mapPt = [&](const QPoint &s) -> QPoint {
        return mapStoredPointToFrame(s, frameSize, videoSize, rotation);
    };
    auto mapRect = [&](const QRect &s) -> QRect {
        return mapStoredRectToFrame(s, frameSize, videoSize, rotation);
    };

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const int w = qMax(1, penWidth);
    QFont f = painter.font();
    f.setPixelSize(qMax(10, 6 * w));   // 与屏上 12~14px 观感成比例
    painter.setFont(f);

    if (regions) {
        const QVector<QRect> rects = regions->regions();
        for (int i = 0; i < rects.size(); ++i) {
            const QRect rc = mapRect(rects[i]);
            const QColor base = RoiModel::regionColor(i);
            QColor fill = base;
            fill.setAlpha(20);
            painter.fillRect(rc, fill);
            painter.setPen(QPen(base, w));
            painter.drawRect(rc);
            painter.setPen(Qt::white);
            painter.drawText(rc.topLeft() + QPoint(2 * w, 7 * w),
                             QStringLiteral("R%1").arg(i + 1));
        }
    }

    if (polygons) {
        const QVector<QPolygon> polys = polygons->polygons();
        for (int i = 0; i < polys.size(); ++i) {
            QPolygon target;
            for (const QPoint &pt : polys[i])
                target.append(mapPt(pt));
            const QColor base = RoiModel::polygonColor(i);
            QColor fill = base;
            fill.setAlpha(20);
            painter.setBrush(fill);
            painter.setPen(QPen(base, w));
            painter.drawPolygon(target);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(Qt::white);
            painter.drawText(target.boundingRect().topLeft() + QPoint(2 * w, 7 * w),
                             QStringLiteral("P%1").arg(i + 1));
        }
    }

    if (guideLines) {
        const QVector<GuideLine> lines = guideLines->lines();
        for (const GuideLine &gl : lines) {
            QPen pen(gl.color, w);
            pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            painter.drawLine(mapPt(gl.start), mapPt(gl.end));
        }
    }
    painter.restore();
}
}   // namespace FrameAnnotation
