#include "site_map_render.h"

#include <QFont>
#include <QPainterPath>
#include <QtMath>

namespace sitemaprender {

void drawPoints(QPainter &p, const SiteMapData &d, const QRectF &baseRect,
                const QHash<QString, QColor> &laneColor, int selected)
{
    if (baseRect.isEmpty())
        return;
    p.setRenderHint(QPainter::Antialiasing, true);
    const double shortSide = qMin(baseRect.width(), baseRect.height());
    const double rScale = shortSide / 100.0;
    for (int i = 0; i < d.points.size(); ++i) {
        const SiteMapPoint &pt = d.points[i];
        const QPointF c(baseRect.left() + pt.x * baseRect.width(),
                        baseRect.top() + pt.y * baseRect.height());
        const double r = pt.radiusPct * rScale;
        QColor col = pt.orphan ? QColor(200, 40, 40)
                               : laneColor.value(pt.laneRef, QColor(60, 60, 60));

        // 扇形（拍摄方向+覆盖范围；Qt 角 0=3 点钟逆时针正 → 取负转顺时针）
        const double startDeg = -(pt.headingDeg + pt.spreadDeg / 2.0);
        QColor fill = col;
        fill.setAlpha(70);
        p.setPen(QPen(col, qMax(2.0, shortSide * 0.004)));
        p.setBrush(fill);
        p.drawPie(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r),
                  int(startDeg * 16), int(pt.spreadDeg * 16));

        // 圆点
        const double dot = qMax(6.0, shortSide * 0.012);
        p.setBrush(col);
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(c, dot, dot);

        // 标签（拍板 2026-08-23：不要框框，字色=扇形/圆点色，白描边保可读）
        QFont f = p.font();
        f.setPointSizeF(qMax(8.0, shortSide * 0.028 * pt.labelScale));
        f.setBold(true);
        const QString text = pt.orphan
            ? pt.label + QStringLiteral("（已移除）") : pt.label;
        const QFontMetricsF fm(f);
        const QRectF tr = fm.boundingRect(text);
        QPainterPath tp;
        tp.addText(c.x() - tr.width() / 2, c.y() + dot + 4 + fm.ascent(),
                   f, text);
        p.setPen(QPen(Qt::white, qMax(3.0, shortSide * 0.006),
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(tp);          // 白色描边晕
        p.fillPath(tp, col);     // 机位色填充

        // 选中框
        if (i == selected) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 200, 40), 3, Qt::DashLine));
            p.drawEllipse(c, r + 8, r + 8);
        }
    }
}

QImage renderFramed(const SiteMapData &d, const QImage &base,
                    const QHash<QString, QColor> &laneColor,
                    const QString &caseNo, const QString &drawer,
                    const QString &reviewer, const QString &dateText)
{
    const int W = 2480, H = 1754;
    QImage img(W, H, QImage::Format_RGB888);
    img.fill(Qt::white);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 图框：外细（20px 边距）+ 内粗（60px 边距）
    const QRectF outer(20, 20, W - 40, H - 40);
    const QRectF inner(60, 60, W - 120, H - 120);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 2));
    p.drawRect(outer);
    p.setPen(QPen(Qt::black, 5));
    p.drawRect(inner);

    // 内容区：内框留 20 边距，底部为标题栏让位由覆盖处理（标准制图惯例）
    const QRectF content = inner.adjusted(20, 20, -20, -20);
    QRectF baseRect = content;
    if (!base.isNull()) {
        const double s = qMin(content.width() / base.width(),
                              content.height() / base.height());
        const double bw = base.width() * s, bh = base.height() * s;
        baseRect = QRectF(content.left() + (content.width() - bw) / 2,
                          content.top() + (content.height() - bh) / 2, bw, bh);
        p.setPen(QPen(QColor(160, 160, 160), 1));
        p.drawImage(baseRect, base);
    }
    drawPoints(p, d, baseRect, laneColor);

    // 右下标题栏（560×150，四行网格）
    const double tw = 620, th = 168;
    const QRectF tbox(inner.right() - tw, inner.bottom() - th, tw, th);
    p.setPen(QPen(Qt::black, 3));
    p.setBrush(QColor(255, 255, 255, 245));
    p.drawRect(tbox);
    const double rowH = th / 4;
    p.setPen(QPen(Qt::black, 1));
    for (int r = 1; r < 4; ++r)
        p.drawLine(QPointF(tbox.left(), tbox.top() + r * rowH),
                   QPointF(tbox.right(), tbox.top() + r * rowH));
    // 第 3/4 行中缝
    p.drawLine(QPointF(tbox.left() + tw / 2, tbox.top() + 2 * rowH),
               QPointF(tbox.left() + tw / 2, tbox.bottom()));

    QFont f(QStringLiteral("SimSun"));
    f.setPixelSize(24);
    p.setFont(f);
    p.setPen(Qt::black);
    auto cellText = [&](int row, int half, const QString &label,
                        const QString &val, bool bold = false) {
        QFont ff = f;
        ff.setBold(bold);
        p.setFont(ff);
        const double x0 = tbox.left() + (half ? tw / 2 : 0);
        const QRectF rc(x0 + 12, tbox.top() + row * rowH, tw / (half ? 2 : 1) - 24, rowH);
        p.drawText(rc, Qt::AlignVCenter | Qt::AlignLeft,
                   label + QStringLiteral("  ") + val);
    };
    cellText(0, 0, QStringLiteral("案件编号"), caseNo);
    {
        QFont bf = f;
        bf.setPixelSize(30);
        bf.setBold(true);
        p.setFont(bf);
        p.drawText(QRectF(tbox.left(), tbox.top() + rowH, tw, rowH),
                   Qt::AlignCenter, QStringLiteral("监控点位平面示意图"));
    }
    cellText(2, 0, QStringLiteral("制图"), drawer);
    cellText(2, 1, QStringLiteral("审核"), reviewer);
    cellText(3, 0, QStringLiteral("日期"), dateText);
    cellText(3, 1, QStringLiteral("图号"), QStringLiteral("SP-01"));
    p.end();
    return img;
}

} // namespace sitemaprender
