/**
 * @file microdiff_core.cpp
 * @brief 微变分析纯计算核心实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "microdiff_core.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace microdiff {

namespace {
inline int clampIdx(int v, int n)
{
    return v < 0 ? 0 : (v >= n ? n - 1 : v);
}
} // namespace

double gainForLevel(int level)
{
    const int lv = std::max(1, std::min(5, level));
    return 20.0 * std::pow(0.75, lv - 1);
}

int blurRadiusForSigma(double sigma)
{
    if (sigma <= 0.0)
        return 0;
    // 两次盒式模糊的等效 σ ≈ sqrt(2·(k²−1)/12)，取 k ≈ 1.5σ 足够接近
    const int r = static_cast<int>(std::lround(sigma * 1.5));
    return std::max(1, r);
}

void computeBaselineMedian(const std::vector<const uint8_t *> &samples,
                           int w, int h, std::vector<uint8_t> &outBase)
{
    const int n = w * h;
    if (samples.empty() || n <= 0) {          // 先校验后分配：n<0 时 size_t 回绕会巨额分配
        outBase.clear();
        return;
    }
    outBase.assign(static_cast<size_t>(n), 0);
    const int m = static_cast<int>(samples.size());
    std::vector<uint8_t> buf(static_cast<size_t>(m));
    const int mid = m / 2;
    for (int i = 0; i < n; ++i) {
        for (int s = 0; s < m; ++s)
            buf[s] = samples[s][i];
        std::nth_element(buf.begin(), buf.begin() + mid, buf.end());
        outBase[i] = buf[mid];
    }
}

void boxBlurInt16(std::vector<int16_t> &buf, int w, int h, int radius)
{
    if (radius <= 0 || w <= 0 || h <= 0)
        return;
    const int n = w * h;
    if (static_cast<int>(buf.size()) < n)
        return;
    const int k = 2 * radius + 1;
    std::vector<int16_t> tmp(static_cast<size_t>(n));

    // 水平（行内滑动和，边界钳制）
    for (int y = 0; y < h; ++y) {
        const int16_t *row = buf.data() + static_cast<size_t>(y) * w;
        int16_t *dst = tmp.data() + static_cast<size_t>(y) * w;
        int32_t sum = 0;
        for (int i = -radius; i <= radius; ++i)
            sum += row[clampIdx(i, w)];
        dst[0] = static_cast<int16_t>(sum / k);
        for (int x = 1; x < w; ++x) {
            sum += row[clampIdx(x + radius, w)];
            sum -= row[clampIdx(x - radius - 1, w)];
            dst[x] = static_cast<int16_t>(sum / k);
        }
    }
    // 垂直
    for (int x = 0; x < w; ++x) {
        int32_t sum = 0;
        for (int i = -radius; i <= radius; ++i)
            sum += tmp[static_cast<size_t>(clampIdx(i, h)) * w + x];
        buf[x] = static_cast<int16_t>(sum / k);
        for (int y = 1; y < h; ++y) {
            sum += tmp[static_cast<size_t>(clampIdx(y + radius, h)) * w + x];
            sum -= tmp[static_cast<size_t>(clampIdx(y - radius - 1, h)) * w + x];
            buf[static_cast<size_t>(y) * w + x] = static_cast<int16_t>(sum / k);
        }
    }
}

void buildJetLut(uint8_t lut[256][3])
{
    for (int i = 0; i < 256; ++i) {
        const double x = i / 255.0;
        const double r = 1.5 - std::fabs(4.0 * x - 3.0);
        const double g = 1.5 - std::fabs(4.0 * x - 2.0);
        const double b = 1.5 - std::fabs(4.0 * x - 1.0);
        lut[i][0] = static_cast<uint8_t>(255.0 * std::max(0.0, std::min(1.0, r)));
        lut[i][1] = static_cast<uint8_t>(255.0 * std::max(0.0, std::min(1.0, g)));
        lut[i][2] = static_cast<uint8_t>(255.0 * std::max(0.0, std::min(1.0, b)));
    }
}

void State::configure(int w, int h, int temporalFrames)
{
    const int nw = std::max(0, w);
    const int nh = std::max(0, h);
    int n = std::max(1, temporalFrames);
    const size_t total = static_cast<size_t>(nw) * nh;

    // 内存上限（防低内存机器上 bad_alloc 直接 terminate）：环缓冲超预算时自动降时域窗。
    // 全幅 2560×1440 默认 tf=11 约 81MB；tf=41 时约 302MB 会超预算→降档。
    constexpr size_t kRingBudgetBytes = static_cast<size_t>(256) * 1024 * 1024;
    if (total > 0) {
        const size_t maxN = std::max<size_t>(1, kRingBudgetBytes / (total * sizeof(int16_t)));
        if (static_cast<size_t>(n) > maxN)
            n = static_cast<int>(maxN);
    }

    if (nw == m_w && nh == m_h && n == m_n && !m_ring.empty())
        return;
    m_w = nw;
    m_h = nh;
    m_n = n;
    m_ring.assign(total * static_cast<size_t>(m_n), 0);
    m_accum.assign(total, 0);
    m_acc32.assign(total, 0);
    m_head = 0;
    m_count = 0;
    if (m_base.size() != total)
        m_base.clear();
}

void State::clear()
{
    std::fill(m_ring.begin(), m_ring.end(), static_cast<int16_t>(0));
    m_head = 0;
    m_count = 0;
}

void State::setBaseline(const std::vector<uint8_t> &base)
{
    const size_t total = static_cast<size_t>(m_w) * m_h;
    if (base.size() != total)
        return;
    m_base = base;
    clear();
}

void State::temporalBlur(const uint8_t *gray, double sigma, std::vector<int16_t> &dgOut)
{
    const size_t total = static_cast<size_t>(m_w) * m_h;
    dgOut.assign(total, 0);
    if (m_w <= 0 || m_h <= 0 || total == 0 || m_base.size() != total || !gray)
        return;

    int16_t *slot = m_ring.data() + static_cast<size_t>(m_head) * total;
    for (size_t i = 0; i < total; ++i)
        slot[i] = static_cast<int16_t>(static_cast<int>(gray[i]) - static_cast<int>(m_base[i]));
    m_head = (m_head + 1) % m_n;
    if (m_count < m_n)
        ++m_count;

    // 时域平均：用 int32 累加（m_count 上限已由 configure 钳制，但 int32 彻底免溢出）
    std::fill(m_acc32.begin(), m_acc32.end(), 0);
    for (int k = 0; k < m_count; ++k) {
        const int16_t *p = m_ring.data() + static_cast<size_t>(k) * total;
        for (size_t i = 0; i < total; ++i)
            m_acc32[i] += p[i];
    }
    for (size_t i = 0; i < total; ++i)
        m_accum[i] = static_cast<int16_t>(m_acc32[i] / m_count);

    const int r = blurRadiusForSigma(sigma);
    if (r > 0)
        boxBlurInt16(m_accum, m_w, m_h, r);

    dgOut = m_accum;
}

void State::process(const uint8_t *gray, double gain, double sigma,
                    std::vector<uint8_t> &magOut)
{
    const size_t total = static_cast<size_t>(m_w) * m_h;
    magOut.assign(total, 0);
    if (total == 0)
        return;
    temporalBlur(gray, sigma, m_signed);
    if (m_signed.size() != total)
        return;
    for (size_t i = 0; i < total; ++i) {
        const double v = std::fabs(static_cast<double>(m_signed[i])) - m_floor;
        if (v <= 0.0) {
            magOut[i] = 0;
            continue;
        }
        const double g = v * gain;
        magOut[i] = static_cast<uint8_t>(g >= 255.0 ? 255 : static_cast<int>(g));
    }
}

double calibrateNoiseFloor(const std::vector<const uint8_t *> &samples,
                           const std::vector<uint8_t> &base,
                           int w, int h, int temporalFrames, double sigma,
                           double percentile)
{
    const size_t total = static_cast<size_t>(w) * h;
    if (samples.size() < 2 || base.size() != total || total == 0)
        return 3.0;

    State st;
    st.configure(w, h, std::min<int>(temporalFrames, static_cast<int>(samples.size())));
    st.setBaseline(base);
    st.setNoiseFloor(0.0);          // 标定态：不扣基底、增益 1

    std::vector<uint8_t> mag;
    uint64_t hist[256] = {0};
    uint64_t counted = 0;
    for (const uint8_t *s : samples) {
        st.process(s, 1.0, sigma, mag);
        if (st.framesInWindow() < 2)
            continue;               // 时域窗未热，跳过
        for (size_t i = 0; i < total; ++i)
            ++hist[mag[i]];
        counted += total;
    }
    if (counted == 0)
        return 3.0;

    const double p = std::max(50.0, std::min(99.99, percentile));
    const uint64_t want = static_cast<uint64_t>(static_cast<double>(counted) * p / 100.0);
    uint64_t acc = 0;
    for (int v = 0; v < 256; ++v) {
        acc += hist[v];
        if (acc >= want)
            return static_cast<double>(v);
    }
    return 3.0;
}

} // namespace microdiff
