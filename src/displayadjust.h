/**
 * @file displayadjust.h
 * @brief 播放画面调节参数包 + 共享 256 级 LUT 构建器（2026-08-14）
 *
 * 适用范围：VideoWidget 主画面 / MagnifierWidget 放大镜 / PinnedWidget 钉图
 * 的【显示链路】——仅影响显示与证据快照（所见即所得）；分析、ROI、语谱图、
 * 证据文件永远走原始帧，不受任何调节影响（取证红线）。
 *
 * LUT 流水线固定顺序（复合到一张 256 级查找表，逐帧零额外开销）：
 *   v → 反色(invert) → 色阶(黑点/白点拉伸) → 伽马 → 亮度/对比度(既有公式)
 * 全部参数默认时 isIdentity()==true，buildLut() 返回空表 = 恒等直通。
 *
 * 亮度/对比度公式与 applyBrightnessContrast（i18n.cpp，截图叠加/放大镜融合
 * 共用）逐位一致：out = clamp(cf*(in + b*2 - 128) + 128)（截尾舍入）。
 */
#pragma once

#include <QByteArray>
#include <QtGlobal>
#include <cmath>

class QImage;

struct DisplayAdjust {
    int brightness = 0;      ///< -50..50（0=不变）
    int contrast = 0;        ///< -50..50（0=不变）
    int gammaPercent = 100;  ///< 30..300（100 = γ1.0 不变；>100 提亮中间调/暗部）
    int blackPoint = 0;      ///< 0..127（黑场：以下裁为 0）
    int whitePoint = 255;    ///< 128..255（白场：以上拉为 255）
    bool invert = false;     ///< 反色（负片）

    bool isIdentity() const {
        return brightness == 0 && contrast == 0 && gammaPercent == 100
               && blackPoint == 0 && whitePoint == 255 && !invert;
    }
    bool operator==(const DisplayAdjust &o) const {
        return brightness == o.brightness && contrast == o.contrast
               && gammaPercent == o.gammaPercent
               && blackPoint == o.blackPoint && whitePoint == o.whitePoint
               && invert == o.invert;
    }
    bool operator!=(const DisplayAdjust &o) const { return !(*this == o); }

    /// 构建 256 级 LUT；恒等时返回空 QByteArray（调用方零开销直通）。
    QByteArray buildLut() const {
        if (isIdentity())
            return QByteArray();
        // 色阶防御：黑/白点最小间距 16，防止滑杆对撞产生病态除法
        const int bp = qBound(0, blackPoint, 127);
        const int wp = qBound(bp + 16, whitePoint, 255);
        const double gamma = qBound(30, gammaPercent, 300) / 100.0;
        const double invG = 1.0 / gamma;
        const double cf = (259.0 * (contrast + 255)) / (255.0 * (259 - contrast));

        QByteArray lut(256, 0);
        for (int v = 0; v < 256; ++v) {
            double x = v;
            if (invert)
                x = 255.0 - x;                       // ① 反色
            x = (x - bp) * 255.0 / double(wp - bp);  // ② 色阶拉伸
            x = qBound(0.0, x, 255.0);
            x = 255.0 * std::pow(x / 255.0, invG);   // ③ 伽马
            x = cf * (x + brightness * 2 - 128) + 128;  // ④ 亮度/对比度
            lut[v] = static_cast<char>(qBound(0, int(x), 255));   // 截尾（与既有实现逐位一致）
        }
        return lut;
    }
};

/// LUT 查表应用（共享实现，避免三处复制）。空表 = 恒等浅拷贝。
QImage applyDisplayLut(const QImage &src, const QByteArray &lut);
