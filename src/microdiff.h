/**
 * @file microdiff.h
 * @brief 微变分析显示层：原彩帧 + 假彩色微变场叠加（对标火察"颜色增强显示"）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 定位：与 displayadjust 同级的【显示链路】环节。实际变换顺序（与实现一致）：
 *   原始帧 → 【微变假彩色叠加】 → 旋转 → applyDisplayLut → m_frameImage
 * 微变放在旋转【之前】：ROI 与基准都定义在【原视频坐标系】，先叠加后旋转才能保证
 * 任何旋转档位不下错位。
 *
 * 微变局部放大：MagnifierWidget / PinnedWidget / FullscreenVideoWindow 拿到的是
 * VideoWidget 算好的 MicroDiffFrame（纯数据），各自在显示末级调 renderMicroDiff()
 * 即可；renderMicroDiff 是纯函数不推进时域环缓冲，因此共享同一份缓存帧是安全的。
 *
 * 取证红线（与 displayadjust 一致）：仅影响显示与证据快照，不改分析数据、
 * 不改 ROI、不改导出证据文件本身。
 *
 * 计算只用亮度 Y（烟是亮度现象）；显示保留原彩帧，只对"变了的地方"染色：
 *   alpha = 强度/255 × 叠加强度%   → 未变化处 alpha=0，完全保留原彩画面
 *
 * 两段式 API（重要）：时域环缓冲必须【每帧只推进一次】。因此拆成
 *   ① computeMicroDiff() —— 每来一帧调一次，推进环缓冲，产出低通后的有符号差值 Dg
 *   ② renderMicroDiff()  —— 纯渲染，可反复调用（拖滑杆/改旋转/重绘都不推进环缓冲）
 */
#pragma once

#include <QImage>
#include <QRect>
#include <QSize>

#include <cstdint>
#include <vector>

#include "domain/microdiff_core.h"

/// 微变显示模式
enum class MicroDiffMode {
    Overlay = 0,     ///< 原图彩色 + 微变彩场叠加（默认，观感对标商用系统）
    ColorField = 1,  ///< 纯微变彩场（区域外原图压暗，保留位置上下文）
    SideBySide = 2,  ///< 左原图 / 右微变叠加
    OriginalOnly = 3 ///< 只显示原图彩色（微变计算关闭）
};

/// 微变显示参数（纯数据；默认关闭）
struct MicroDiffParams {
    bool enabled = false;
    MicroDiffMode mode = MicroDiffMode::Overlay;
    int level = 3;              ///< 等级 1..5（越小越敏感）
    int strengthPercent = 90;   ///< 叠加不透明度 0..100
    int temporalFrames = 11;    ///< 时域一致性窗口（帧）
    double sigma = 5.0;         ///< 空间低通 σ（像素）
    bool fullFrame = false;     ///< false = 只处理 ROI；true = 全画面
    QRect roi;                  ///< fullFrame=false 时的微变区域（画面像素坐标）
    double noiseFloor = 6.0;    ///< 噪声基底（由基准段自动标定）

    bool isIdentity() const { return !enabled || mode == MicroDiffMode::OriginalOnly; }
};

/**
 * @brief 单帧计算结果：低通后的有符号差值 Dg（未扣基底、未乘增益）。
 * 扣基底/增益/假彩色/混合都在 renderMicroDiff() 做，因此改等级或改强度
 * 不需要重算（暂停时拖滑杆即时生效）。
 */
struct MicroDiffFrame {
    std::vector<int16_t> dg;   ///< 处理区（coreRect）的有符号差值
    QRect coreRect;            ///< 处理区 = 染色区外扩一个低通半径
    QRect workRect;            ///< 实际染色区（ROI 或全画面）
    bool valid = false;

    void clear() { dg.clear(); coreRect = QRect(); workRect = QRect(); valid = false; }
};

/**
 * @brief 微变显示状态：灰度基准 + 时域环缓冲 + 工作区缓存。
 * 由 MainWindow 持有；VideoWidget 持一份用于逐帧计算。
 */
class MicroDiffDisplayState
{
public:
    /// @brief 设置灰度基准（Format_Grayscale8，视频原生尺寸）
    /// @param floor 由基准段标定的噪声基底（灰度级）
    /// @return 尺寸不合法时返回 false
    bool setBaselineGray(const QImage &gray, double noiseFloor);
    void clearBaseline();
    bool hasBaseline() const { return !m_baseGray.empty(); }
    QSize baselineSize() const { return m_size; }
    double noiseFloor() const { return m_floor; }
    /// @brief 时域窗内已有帧数（< temporalFrames 表示还在预热）
    int framesInWindow() const { return m_core.framesInWindow(); }
    /// @brief 清空时域环缓冲（seek/跳转后调用，避免跨段平均）
    void resetTemporal() { m_core.clear(); }

    std::vector<uint8_t> m_baseGray;   ///< 基准像素（Grayscale8 紧凑排布）
    QSize m_size;                      ///< 基准尺寸（= 视频原生尺寸）
    double m_floor = 6.0;
    microdiff::State m_core;
    QRect m_coreRect;                  ///< 已配置的处理区（空 = 未配置）
    int m_coreTemporal = 0;
};

/**
 * @brief 逐帧计算（推进时域环缓冲）。每来一帧调用一次，勿在重绘路径调用。
 * @param src 当前帧（原彩；内部按需转 Grayscale8）
 * @return 计算结果；不可用时 frame.valid == false
 */
MicroDiffFrame computeMicroDiff(MicroDiffDisplayState &st, const MicroDiffParams &p,
                                const QImage &src);

/**
 * @brief 纯渲染：把计算结果叠加到 src（不推进环缓冲，可反复调用）。
 *
 * @param src 原彩帧（与 computeMicroDiff 的输入同尺寸）
 * @param f computeMicroDiff 的产出
 * @param p 参数（等级/强度/模式/噪声基底在此生效）
 * @return 处理后的帧；f.valid==false 或 p.isIdentity() 时原样返回 src
 */
QImage renderMicroDiff(const QImage &src, const MicroDiffFrame &f, const MicroDiffParams &p);

/**
 * @brief 便捷组合：计算 + 渲染（一次调用完成，推进环缓冲一次）。
 * 供播放链之外的单帧场景（测试、缩略图）使用；播放路径请用两段式 API。
 */
QImage applyMicroDiff(const QImage &src, MicroDiffDisplayState &st, const MicroDiffParams &p);
