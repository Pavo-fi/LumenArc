/**
 * @file microdiff.cpp
 * @brief 微变分析显示层实现（两段式：computeMicroDiff / renderMicroDiff）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "microdiff.h"

#include <QColor>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

/// JET 色带（进程内构建一次）
const uint8_t (*jetLut())[3]
{
    static uint8_t lut[256][3];
    static bool inited = false;
    if (!inited) {
        microdiff::buildJetLut(lut);
        inited = true;
    }
    return lut;
}

/// 把 QImage 的某个矩形抽成紧凑 GRAY8 缓冲（处理 bytesPerLine 行对齐填充）
void extractGrayCompact(const QImage &src, const QRect &rect, std::vector<uint8_t> &out)
{
    const QImage sub = src.copy(rect).convertToFormat(QImage::Format_Grayscale8);
    const int w = rect.width(), h = rect.height();
    out.assign(static_cast<size_t>(w) * h, 0);
    if (sub.isNull())
        return;
    for (int y = 0; y < h; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * w, sub.constScanLine(y),
                    static_cast<size_t>(w));
}

/// 从整帧基准裁出子矩形（紧凑）
void cropBase(const std::vector<uint8_t> &base, int baseW, const QRect &rect,
              std::vector<uint8_t> &out)
{
    const int w = rect.width(), h = rect.height();
    out.assign(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y)
        std::memcpy(out.data() + static_cast<size_t>(y) * w,
                    base.data() + static_cast<size_t>(rect.y() + y) * baseW + rect.x(),
                    static_cast<size_t>(w));
}

} // namespace

bool MicroDiffDisplayState::setBaselineGray(const QImage &gray, double noiseFloor)
{
    if (gray.isNull() || gray.width() <= 0 || gray.height() <= 0)
        return false;
    const QImage g = (gray.format() == QImage::Format_Grayscale8)
                         ? gray
                         : gray.convertToFormat(QImage::Format_Grayscale8);
    const int w = g.width(), h = g.height();
    m_baseGray.assign(static_cast<size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y)
        std::memcpy(m_baseGray.data() + static_cast<size_t>(y) * w, g.constScanLine(y),
                    static_cast<size_t>(w));
    m_size = QSize(w, h);
    m_floor = noiseFloor;
    m_coreRect = QRect();     // 强制重配工作区
    m_coreTemporal = 0;
    m_core.clear();
    return true;
}

void MicroDiffDisplayState::clearBaseline()
{
    m_baseGray.clear();
    m_size = QSize();
    m_coreRect = QRect();
    m_coreTemporal = 0;
    m_core.clear();
}

MicroDiffFrame computeMicroDiff(MicroDiffDisplayState &st, const MicroDiffParams &p,
                                const QImage &src)
{
    MicroDiffFrame out;
    if (src.isNull() || p.isIdentity() || !st.hasBaseline())
        return out;
    if (src.size() != st.m_size)
        return out;   // 基准与当前帧尺寸不符（换了视频/分辨率）：不可用

    const QRect frame(0, 0, src.width(), src.height());
    const QRect work = p.fullFrame ? frame : p.roi.intersected(frame);
    if (work.width() < 4 || work.height() < 4)
        return out;

    // 处理区 = 工作区外扩一个低通半径，保证 ROI 边缘取到真实邻域（否则出现暗边）
    const int pad = microdiff::blurRadiusForSigma(p.sigma);
    const QRect core = work.adjusted(-pad, -pad, pad, pad).intersected(frame);
    const int tf = std::max(1, p.temporalFrames);

    if (st.m_coreRect != core || st.m_coreTemporal != tf) {
        st.m_coreRect = core;
        st.m_coreTemporal = tf;
        st.m_core.configure(core.width(), core.height(), tf);
        std::vector<uint8_t> sub;
        cropBase(st.m_baseGray, src.width(), core, sub);
        st.m_core.setBaseline(sub);
    }

    std::vector<uint8_t> gray;
    extractGrayCompact(src, core, gray);
    st.m_core.setNoiseFloor(0.0);   // 扣基底移到渲染段：改噪声基底/等级无需重算
    st.m_core.temporalBlur(gray.data(), p.sigma, out.dg);
    if (out.dg.size() != static_cast<size_t>(core.width()) * core.height())
        return MicroDiffFrame();
    out.coreRect = core;
    out.workRect = work;
    out.valid = true;
    return out;
}

QImage renderMicroDiff(const QImage &src, const MicroDiffFrame &f, const MicroDiffParams &p)
{
    if (src.isNull() || !f.valid || p.isIdentity() || f.dg.empty())
        return src;
    if (f.coreRect.width() <= 0 || f.coreRect.height() <= 0)
        return src;
    if (f.dg.size() != static_cast<size_t>(f.coreRect.width()) * f.coreRect.height())
        return src;

    const int w = src.width(), h = src.height();
    const QRect work = f.workRect.intersected(QRect(0, 0, w, h));
    if (work.width() < 1 || work.height() < 1)
        return src;

    QImage out = src.convertToFormat(QImage::Format_RGB32);
    const bool colorField = (p.mode == MicroDiffMode::ColorField);
    if (colorField) {
        QPainter pt(&out);
        pt.fillRect(out.rect(), QColor(0, 0, 0, 165));   // 区域外压暗，保留位置上下文
        pt.end();
    }

    const uint8_t (*lut)[3] = jetLut();
    const double gain = microdiff::gainForLevel(p.level);
    const int strength = std::max(0, std::min(100, p.strengthPercent));
    const int ox = work.x() - f.coreRect.x();
    const int oy = work.y() - f.coreRect.y();

    for (int y = 0; y < work.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(work.y() + y)) + work.x();
        const int16_t *dline = f.dg.data() + static_cast<size_t>(oy + y) * f.coreRect.width() + ox;
        for (int x = 0; x < work.width(); ++x) {
            const double dev = std::fabs(static_cast<double>(dline[x])) - p.noiseFloor;
            if (dev <= 0.0) {
                if (!colorField)
                    continue;          // 未变化：完全保留原彩画面
                line[x] = qRgb(0, 0, 0);
                continue;
            }
            const double g = dev * gain;
            const int m = static_cast<int>(g >= 255.0 ? 255 : g);
            const uint8_t *jc = lut[m];
            if (colorField) {
                line[x] = qRgb(jc[0], jc[1], jc[2]);
                continue;
            }
            const int a = m * strength / 100;   // 0..255
            if (a <= 0)
                continue;
            const QRgb s = line[x];
            line[x] = qRgb((qRed(s) * (255 - a) + jc[0] * a) / 255,
                           (qGreen(s) * (255 - a) + jc[1] * a) / 255,
                           (qBlue(s) * (255 - a) + jc[2] * a) / 255);
        }
    }

    if (p.mode != MicroDiffMode::SideBySide)
        return out;

    // 左原图 / 右微变
    QImage canvas(w * 2, h, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    QPainter pt(&canvas);
    pt.drawImage(0, 0, src.convertToFormat(QImage::Format_RGB32));
    pt.drawImage(w, 0, out);
    pt.end();
    return canvas;
}

QImage applyMicroDiff(const QImage &src, MicroDiffDisplayState &st, const MicroDiffParams &p)
{
    const MicroDiffFrame f = computeMicroDiff(st, p, src);
    return renderMicroDiff(src, f, p);
}
