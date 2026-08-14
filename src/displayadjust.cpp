/**
 * @file displayadjust.cpp
 * @brief 共享 LUT 查表实现（见 displayadjust.h 头部设计要点）
 * @date 2026-08-14
 */
#include "displayadjust.h"

#include <QImage>

QImage applyDisplayLut(const QImage &src, const QByteArray &lut)
{
    if (src.isNull())
        return src;
    if (lut.isEmpty())
        return src;   // 恒等：COW 浅拷贝，零像素开销
    QImage out = src.format() == QImage::Format_ARGB32
        ? src.copy() : src.convertToFormat(QImage::Format_ARGB32);
    const auto *tab = reinterpret_cast<const uchar *>(lut.constData());
    const int h = out.height(), w = out.width();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb px = line[x];
            line[x] = qRgba(tab[qRed(px)], tab[qGreen(px)], tab[qBlue(px)],
                            qAlpha(px));
        }
    }
    return out;
}
