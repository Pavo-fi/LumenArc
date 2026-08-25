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
#include <QRect>
#include <QJsonObject>
#include <QJsonArray>
#include <QtGlobal>
#include <cmath>
#include "time_piecewise.h"
#include "event_calib.h"

/**
 * @brief 校时数据：仿射时间模型 + 测点证据。
 *
 * 值类型（隐式共享容器），可安全跨线程拷贝。原始测点观测（OCR 原文/截图）
 * 永不静默修正；修正参数（offset/rate）可审计、可关闭（rateApplied=false）。
 */
struct TimeCalibration
{
    enum class Source { None, Manual, Ocr, AbsStart, Inherited,
                        CrossCamEvent };  ///< P-73 多机同事件间接校时

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
        bool    ocrSuspicious = false; ///< v1.2.1：检测为 OCR 异常（错读），已自动排除
    };
    QVector<Sample> samples;
    double  sigmaRate = 0.0;       ///< 拟合速率标准误（报告用）
    qint64  calibratedAtMs = 0;    ///< 校时操作时刻

    // ---- 北京时间校验（人工：监控时间 ↔ 真实北京时间整体偏移）----
    qint64  truthOffsetMs = 0;     ///< 北京时间偏移：beijing = wall + truthOffset
    bool    truthSet = false;      ///< 是否做过北京时间校验
    qint64  truthCheckedAtMs = 0;  ///< 校验操作时刻
    QString truthNote;             ///< 校验说明（如对时来源，留档）

    // ---- v1.12.5 北京时间对时留档（2026-08-21 拍板：图片框选 OCR / 手动输入）----
    QString truthSource;         ///< "" / "photo"（校时图片框选）/ "manualTimes"
                                 ///< （手输两个时间）/ "manualOffset"（直输偏移量）
    QString truthImagePath;      ///< photo 来源：校时图片绝对路径（另复制入案件留档）
    QRect   truthMonitorBox;     ///< 框 1 监控主机时间（图片像素坐标）
    QRect   truthBeijingBox;     ///< 框 2 北京时间（图片像素坐标）
    QString truthMonitorText;    ///< 框 1 OCR 原文（留档）
    QString truthBeijingText;    ///< 框 2 OCR 原文（留档）

    // ---- v1.15.3 校时差值注记（间接校时结论存盘，报告读出白话说差值）----
    QString calibNote;           ///< 校时差值/结论人读（如「目标路的钟原本就基本准<1 秒」）

    // ---- 分段重建（v1.2.1 时间重建：变速/抽帧文件查表校时）----
    PiecewiseTimeMap piecewise;   ///< 分段映射表（isValid = 重建产物）
    bool    piecewiseApplied = false; ///< 分段模式是否生效（优先于仿射）
    bool    speedVariant = false;     ///< 检测结论：变速/抽帧文件
    int     boundaryCount = 0;        ///< 变速边界数
    double  totalWallSpanSec = 0.0;   ///< OSD 总跨度（秒，报告用）
    bool    audioConsistent = true;   ///< 音频时长校验结论
    bool    audioKnown = false;       ///< 是否取得音频时长做过校验

    /// 分段模式生效（piecewise 有效且 piecewiseApplied）
    bool piecewiseMode() const
    {
        return piecewiseApplied && piecewise.isValid();
    }

    bool   isValid() const { return source != Source::None; }

    /**
     * @brief 有效校时（徽标/摘要语义）：不止 source 非 None，还必须有实际
     * 效果——日期已知、非零偏移、速率修正、分段重建或验证点任一。
     * 旧 v7 数据 time_offset=0 迁移产物（source=Manual 且全零）不算。
     */
    bool   isEffective() const
    {
        if (!isValid())
            return false;
        if (piecewiseMode()) {
            // v1.12.8（天河案实测）：分段全部零锚（sidecar 源均未校时，
            // wallStartMs=0）=「没有时间信息」——不算有效，否则继承后
            // 时间轴以 1970-01-01 起显示。
            for (const auto &s : piecewise.segments)
                if (s.wallStartMs != 0)
                    return true;
            return false;
        }
        return dateKnown || offsetMs != 0 || rateApplied || truthSet;
    }
    double effectiveRate() const { return rateApplied ? rate : 1.0; }

    /// 全应用唯一换算入口（C3）：监控墙钟 = offset + rate×stream
    /// 分段模式（piecewiseMode）下走查表，否则仿射
    qint64 wallMsOf(qint64 streamMs) const
    {
        if (piecewiseMode())
            return piecewise.wallMsOf(streamMs);
        return offsetMs + static_cast<qint64>(
                   std::llround(effectiveRate() * static_cast<double>(streamMs)));
    }
    /// 反解：墙钟 → 流内毫秒
    qint64 streamMsOf(qint64 wallMs) const
    {
        if (piecewiseMode())
            return piecewise.streamMsOf(wallMs);
        return static_cast<qint64>(
            std::llround((wallMs - offsetMs) / effectiveRate()));
    }
    /// 北京时间（最终报时口径）= 监控墙钟 + 北京时间偏移
    qint64 beijingMsOf(qint64 streamMs) const
    {
        return wallMsOf(streamMs) + truthOffsetMs;
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

    /// 漂移显著性下限：10 秒/天（低于此视为“钟准”，不修正只报告）
    /// v1.15.3 用户拍板：30→10——两小时录像 10 秒/天仅差 0.8 秒，可接受；
    /// 超过即应修正，不放任。
    static constexpr double kMinSignificantRateDev = 10.0 / 86400000.0;
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
     * - n≥3：残差估计 σ；rateSignificant 需 |rate-1| > max(3σ, 10秒/天)
     * 可反复调用：用户剔除野点（used=false）后重新拟合。
     */
    static FitResult fit(const QVector<Sample> &samples);

    /// 应用拟合结果：rateApplied = rateSignificant && rateSane
    void applyFit(const FitResult &fr);

    /// v7 旧格式迁移：日内秒偏移 → dateKnown=false 模型（rate=1.0）。
    /// 偏移为 0（旧数据"未校时"）→ Source::None，不产生空校时模型
    /// （空模型会让案件徽标误亮 ⏰ 而图表毫无变化——用户实测反馈）
    static TimeCalibration fromLegacyOffset(qint64 dayOffsetMs)
    {
        TimeCalibration c;
        if (dayOffsetMs == 0)
            return c;   // Source::None
        c.source = Source::Manual;
        c.offsetMs = dayOffsetMs;
        c.dateKnown = false;
        return c;
    }

    // ---- 序列化（.vla v8 META / 未来 case.json 共用，QtCore only）----
    // ---- P-73 同事件间接校时溯源（source == CrossCamEvent 时有效）----
    QVector<eventcalib::EventAnchor> eventAnchors;  ///< 本路锚点全表（含参考路/事件名/快照墙钟/容差）

    QJsonObject toJson() const;
    static TimeCalibration fromJson(const QJsonObject &o);
    /// source ↔ 字符串（F5：写/读/文档三处同步）
    static QString sourceToString(Source s);
    static Source sourceFromString(const QString &s);
};

/// sidecar（<视频>.lumencal.json）→ 分段校时（v1.12.3 自 app 层下沉 domain：
/// 供 cam_timeline 等轻量调用方直用；CalibrationService::loadSidecar 同名转发
/// 本函数，单实现 SSOT）。解析分段锚点 → piecewise 查表校时 + 缺口警告
/// （"gaps:<数量>:<最大ms>" 类型化前缀，C1）。文件缺失/格式不符 → false。
bool loadSidecarCalibration(const QString &videoPath, TimeCalibration *out,
                            QString *warning);
