/**
 * @file microdiff_core.h
 * @brief 微变分析纯计算核心：中值基准 / 时域一致性 / 多尺度低通 / 假彩色
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 纯数据与算子，无 QObject、无 Qt Widgets（R2/R3）。被两处复用：
 *   ① 显示层 src/microdiff.cpp（逐帧叠加，播放态）
 *   ② 基准提取 src/infrastructure/microdiff_baseline.cpp（离线一遍，算 B 与噪声基底）
 *
 * 算法来源（火灾调查"微变分析"，火察同类原理）：
 *   B  = 起火前干净段的逐像素时间中值
 *   D  = I − B                      （单帧偏差，烟早期仅 2~5 灰度级）
 *   Ds = 时域平均(D, N 帧)           （噪声零均值抵消，持续变化保留）
 *   Dg = 空间低通(Ds, σ)             （只保留粗尺度，压掉 H.264 块噪声）
 *   m  = clip((|Dg| − floor) × 增益, 0, 255)
 *   floor 由基准段自身标定（噪声基底），保证"没变化处为 0"，背景不被染色。
 *
 * 复杂度：O(W·H·N) 时域 + O(W·H·r) 空间；ROI 227×209 逐帧 < 1ms（Release x64）。
 */
#pragma once

#include <cstdint>
#include <vector>

namespace microdiff {

/// @brief 等级 1..5 → 显示增益。等级越小越敏感（增益越大）。
/// @param level 1..5（越界自动钳制）
/// @return 增益倍率：1→20.0, 2→15.0, 3→11.25, 4→8.44, 5→6.33（等比 0.75）
double gainForLevel(int level);

/// @brief 由 σ 推导盒式模糊半径（两次盒式 ≈ 一次高斯）
int blurRadiusForSigma(double sigma);

/**
 * @brief 逐像素时间中值基准。
 *
 * @param samples 样本帧指针数组，每帧 w*h 字节（GRAY8）
 * @param frameCount 样本数（≥1；建议 16~32，抗车辆/行人短时遮挡）
 * @param w,h 尺寸
 * @param outBase 输出基准（w*h，GRAY8）
 *
 * 复杂度 O(frameCount·W·H)，内存 O(frameCount) 局部栈（逐像素 gather，不复制整帧）。
 */
void computeBaselineMedian(const std::vector<const uint8_t *> &samples,
                           int w, int h, std::vector<uint8_t> &outBase);

/**
 * @brief 可分离盒式模糊（两次迭代近似高斯），对 int16 缓冲。
 *
 * @param src 输入 (w*h)
 * @param dst 输出 (w*h)，可与 src 同缓冲（内部先水平后垂直、逐行原地安全）
 * @param w,h 尺寸
 * @param radius 半径（像素）；≤0 时直通
 *
 * 边界采用钳制（clamp-to-edge），避免 ROI 边缘出现暗边。
 * 复杂度 O(W·H)；用滑动和实现，与半径无关。
 */
void boxBlurInt16(std::vector<int16_t> &buf, int w, int h, int radius);

/// @brief 构建 256 项 JET 假彩色表（火察/热力图同款色带：蓝→青→绿→黄→红）
/// @param lut 输出 [256][3]，通道顺序 R,G,B
void buildJetLut(uint8_t lut[256][3]);

/**
 * @brief 微变状态：持有基准 + 时域环缓冲，逐帧产出强度图。
 *
 * 生命周期（对齐播放降噪 SpectralGateStream 的先例）：
 *   换视频 / 换 ROI / seek 跳变 → configure() 重新分配并清空环缓冲；
 *   基准由"采集基准"动作离线算好后 setBaseline()。
 */
class State
{
public:
    /// @brief 分配工作区尺寸与时域窗口。尺寸/窗口变化即清空环缓冲。
    void configure(int w, int h, int temporalFrames);

    /// @brief 清空基准与环缓冲（保留尺寸）
    void clear();

    /// @brief 设置基准（尺寸须与 configure 一致）
    void setBaseline(const std::vector<uint8_t> &base);

    bool hasBaseline() const { return !m_base.empty(); }
    int width() const { return m_w; }
    int height() const { return m_h; }
    int temporalFrames() const { return m_n; }
    /// @brief 实际生效的时域窗（内存超限时会自动降档，可能小于 configure 传入值）
    int effectiveTemporalFrames() const { return m_n; }
    /// @brief 当前时域窗内已有帧数（未满时结果偏差大，显示层可据此提示"预热中"）
    int framesInWindow() const { return m_count; }

    double noiseFloor() const { return m_floor; }
    void setNoiseFloor(double f) { m_floor = f; }

    /**
     * @brief 时域平均 + 空间低通，输出【有符号】差值 Dg。
     * @param gray 输入当前帧灰度（w*h，GRAY8）
     * @param sigma 空间低通 σ（像素）；≤0 表示不做空间低通
     * @param dgOut 输出有符号差值（w*h，int16）；正=比基准亮，负=比基准暗
     *
     * 符号很重要：烟气早期是“遮挡变暗”（负），火光/爆闪是“增亮”（正）——
     * 显示层与判据都靠这个方向区分烟与火。
     * 预热（帧数不足）时按已有帧数取平均，不阻塞播放。
     */
    void temporalBlur(const uint8_t *gray, double sigma, std::vector<int16_t> &dgOut);

    /**
     * @brief 便捷：temporalBlur + 扣基底/乘增益/取绝对值 → 强度图。
     * @param gray 输入当前帧灰度（w*h，GRAY8）
     * @param gain 显示增益（见 gainForLevel）
     * @param sigma 空间低通 σ（像素）
     * @param magOut 输出强度图（w*h，0~255）；0 表示"与基准无差异"
     */
    void process(const uint8_t *gray, double gain, double sigma, std::vector<uint8_t> &magOut);

private:
    std::vector<uint8_t> m_base;      ///< 基准（w*h）
    std::vector<int16_t> m_ring;      ///< 环缓冲（n 个 w*h 平面，紧凑排布）
    std::vector<int16_t> m_accum;     ///< 时域平均结果缓冲
    std::vector<int32_t> m_acc32;     ///< 时域累加缓冲（int32：n>128 时 int16 会静默回绕）
    std::vector<int16_t> m_signed;    ///< 有符号 Dg 输出缓冲（process 复用）
    int m_w = 0, m_h = 0, m_n = 0;
    int m_head = 0, m_count = 0;
    double m_floor = 0.0;
};

/**
 * @brief 噪声基底自动标定：把同一管线跑在基准段自身样本上，取高分位。
 *
 * @param samples 基准段样本帧（每帧 w*h）
 * @param base 已算好的基准
 * @param w,h 尺寸
 * @param temporalFrames 时域窗口（与显示一致）
 * @param sigma 空间 σ（与显示一致）
 * @param percentile 分位（建议 99.5）
 * @return 噪声基底（灰度级）；样本不足时回退 3.0
 *
 * 目的：让"没起火的地方"输出恰好为 0，背景不被染色——这是观感能否对标
 * 商用系统的关键一步（缺它则满屏彩色噪点）。
 */
double calibrateNoiseFloor(const std::vector<const uint8_t *> &samples,
                           const std::vector<uint8_t> &base,
                           int w, int h, int temporalFrames, double sigma,
                           double percentile);

} // namespace microdiff
