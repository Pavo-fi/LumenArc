/**
 * @file time_calibration.h
 * @brief 校时仿射模型（wallMs = offsetMs + rate × streamMs）+ 最小二乘拟合
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-05
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/V1_ERA_TECH_PLAN_CN.md §2.1/§3.4（Q-1/Q-2 拍板）。
 * 核心：全应用唯一墙钟换算入口 wallMsOf()——界面/图表/CSV/报告只准调它（C3）。
 * rate=1.0 且 dateKnown=false 时行为与旧"日内固定偏移"完全一致（v7 迁移路径）。
 */
#pragma once

#include <QVector>
#include <QString>
#include <QtGlobal>
#include <cmath>

/**
 * @brief 校时数据：仿射时间模型 + 测点证据。
 *
 * 值类型（隐式共享容器），可安全跨线程拷贝。原始测点观测（OCR 原文/截图）
 * 永不静默修正；修正参数（offset/rate）可审计、可关闭（rateApplied=false）。
 */
struct TimeCalibration
{
    enum class Source { None, Manual, Ocr, AbsStart, Inherited };

    Source  source = Source::None;
    qint64  offsetMs = 0;          ///< 流内 0 点对应的墙钟（epoch 毫秒，含日期）
    double  rate = 1.0;            ///< 墙钟走时速率（>1 = 相机钟偏快）
    bool    rateApplied = false;   ///< 漂移修正是否生效（false 时按 1.0 换算）
    double  conf = 0.0;            ///< 0~1，OCR 投票置信度（整体）
    bool    dateKnown = false;     ///< false=旧版日内秒偏移（v7 迁移值）

    /// 测点证据：拟合的原始观测（逐字保留）
    struct Sample {
        qint64  streamMs = -1;     ///< 取样流内位置（showinfo 实测 relMs）
        qint64  wallMs = 0;        ///< 该点解析出的墙钟（epoch 毫秒）
        QString rawText;           ///< OCR 原文
        QString frameImgPath;      ///< 证据截图（相对路径）
        double  conf = 0.0;        ///< 该点 OCR 置信度
        bool    used = true;       ///< 用户可在对话框剔除野点重新拟合
    };
    QVector<Sample> samples;
    double  sigmaRate = 0.0;       ///< 拟合速率标准误（报告用）
    qint64  calibratedAtMs = 0;    ///< 校时操作时刻

    bool   isValid() const { return source != Source::None; }
    double effectiveRate() const { return rateApplied ? rate : 1.0; }

    /// 全应用唯一换算入口（C3）：墙钟 = offset + rate×stream
    qint64 wallMsOf(qint64 streamMs) const
    {
        return offsetMs + static_cast<qint64>(
                   std::llround(effectiveRate() * static_cast<double>(streamMs)));
    }
    /// 反解：墙钟 → 流内毫秒
    qint64 streamMsOf(qint64 wallMs) const
    {
        return static_cast<qint64>(
            std::llround((wallMs - offsetMs) / effectiveRate()));
    }
    /// 报告口径：偏快/偏慢秒/天（正值=偏快；按拟合 rate，与是否应用无关）
    double driftSecondsPerDay() const { return (rate - 1.0) * 86400.0; }

    // ---- 拟合 ----

    /// 拟合警告（C1：类型化，UI 负责映射 i18n 文案，domain 不放用户文本）
    enum class FitWarning { None, OutlierSuspected, RateInsane };

    struct FitResult {
        bool    ok = false;
        int     pointsUsed = 0;
        qint64  offsetMs = 0;
        double  rate = 1.0;
        double  sigmaRate = 0.0;       ///< 速率标准误
        double  sigmaOffsetMs = 0.0;   ///< 偏移标准误
        double  maxResidualMs = 0.0;   ///< 最大测点残差
        bool    rateSignificant = false; ///< |rate-1| > max(3σ, kMinSignificantRateDev)
        bool    rateSane = true;         ///< |rate-1| ≤ kMaxSaneRateDev
        FitWarning warning = FitWarning::None;
        double  driftSecondsPerDay() const { return (rate - 1.0) * 86400.0; }
    };

    /// 漂移显著性下限：30 秒/天（低于此视为"钟准"，不修正只报告）
    static constexpr double kMinSignificantRateDev = 30.0 / 86400000.0;
    /// 两点拟合时的单点假设误差（OSD 秒级量化）：±1s
    static constexpr double kAssumedPointErrorMs = 1000.0;
    /// 野点残差阈值：超过则提示剔除重拟合
    static constexpr double kOutlierResidualMs = 3000.0;
    /// 速率合理上限（1% ≈ 14.4 分钟/天）：超出几乎必为 OCR 误读，拒绝应用
    static constexpr double kMaxSaneRateDev = 0.01;

    /**
     * @brief 对 used=true 的测点做最小二乘拟合（纯函数）。
     *
     * - n=1：rate=1.0，offset=wall-stream（现状语义）
     * - n=2：精确线；σ 用 kAssumedPointErrorMs 估计（保守：两点通常不够显著）
     * - n≥3：残差估计 σ；rateSignificant 需 |rate-1| > max(3σ, 30秒/天)
     * 可反复调用：用户剔除野点（used=false）后重新拟合。
     */
    static FitResult fit(const QVector<Sample> &samples);

    /// 应用拟合结果：rateApplied = rateSignificant && rateSane
    void applyFit(const FitResult &fr);

    /// v7 旧格式迁移：日内秒偏移 → dateKnown=false 模型（rate=1.0）
    static TimeCalibration fromLegacyOffset(qint64 dayOffsetMs)
    {
        TimeCalibration c;
        c.source = Source::Manual;
        c.offsetMs = dayOffsetMs;
        c.dateKnown = false;
        return c;
    }
};
