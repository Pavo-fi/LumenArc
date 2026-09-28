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
#include <QMetaType>
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
        if (tickMode()) {
            // P-98 秒级表：只要有真实墙钟锚点即有效（首锚墙钟须非零）
            for (const auto &a : tickAnchors)
                if (a.second != 0)
                    return true;
            return false;
        }
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

    // ---- P-98 秒级跳变对齐表（非空即生效，优先级最高）----
    /// 每项 = (流内 ms, 该时刻画面墙钟 ms)：来自「像素盯 OSD 秒位跳变 + 稀疏 OCR 锚点」，
    /// 相邻项约 1 秒、段内线性插值 → 帧级（≈40ms）精度，实测抽检 11/12 完全一致。
    /// 加速导出件的跳秒（+2/+3 秒）已由像素字模识别处理，故表内不做速率假设。
    QVector<QPair<qint64, qint64>> tickAnchors;
    /// 该片跳过的画面秒总数（= 画面秒跨度 − 跳变数；加速导出丢帧的直接度量）
    double tickSkippedSeconds = 0.0;
    static constexpr int kTickAnchorMin = 2;   ///< 少于 2 项不生效
    bool tickMode() const { return tickAnchors.size() >= kTickAnchorMin; }
    /// 秒级表内线性插值（跳变 k → k+1 之间）
    qint64 tickWallOf(qint64 streamMs) const;
    qint64 tickStreamOf(qint64 wallMs) const;

    /// 全应用唯一换算入口（C3）：监控墙钟 = offset + rate×stream
    /// 优先级：秒级表 > 分段表 > 仿射
    qint64 wallMsOf(qint64 streamMs) const
    {
        if (tickMode())
            return tickWallOf(streamMs);
        if (piecewiseMode())
            return piecewise.wallMsOf(streamMs);
        return offsetMs + static_cast<qint64>(
                   std::llround(effectiveRate() * static_cast<double>(streamMs)));
    }
    /// 反解：墙钟 → 流内毫秒
    qint64 streamMsOf(qint64 wallMs) const
    {
        if (tickMode())
            return tickStreamOf(wallMs);
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
    /// 野点残差阈值：超过则提示剔除重拟合。
    /// v1.18.x（2026-09-26 顺德平台导出件实测）：3s → 10s——该片画面时钟
    /// **内部分段速率有波动**（实测逐段 1.10~1.17），三点对全局直线天然有 1~4 秒
    /// 残差；阀值 3s 会把正常取样点误判成「读错」剔掉（剔完只剩 2 点、共线校验
    /// 也失效）。真错读是分钟/小时级（年份 2026→2022、日期/上下午读错），不是几秒。
    static constexpr double kOutlierResidualMs = 10000.0;
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

    /// v1.18.x：大倍率是否“自洽”——区分【非实时导出/变速件】与【OCR 误读】。
    /// - |rate−1| > kConfirmableRateDev（50%）→ false：没有哪种导出会压缩一倍以上，
    ///   日期/上下午读错典型就是 ~2×，必为错读；
    /// - n≥3：测点仍近乎共线（最大残差 ≤ kOutlierResidualMs）→ true（真变速，可确认采用）；    /// - n==2：无残差信息可判 → true（UI 弹确认框，要求用户核对两点）。
    /// 不自洽时继续当“误读”拒绝（静默采纳错字会污染整条时间轴）。
    static bool rateChangeSelfConsistent(const FitResult &fr);
    /// 可确认的非实时导出倍率上限（±50%，与分段重建速率上限同量级）
    static constexpr double kConfirmableRateDev = 0.5;

    /// v1.18.x（2026-09-26 顺德公安件实测「校时后还是错的」）：把「第 1 步已算出、
    /// 用户尚未点应用」的拟合结果按统一门控落到模型上，供两处共用：
    /// ① 「使用此结果 / 按此倍率校时」（onUseResult）；
    /// ② 第 2 步「对真实时间」（北京时间对时落库前）——否则该步直接落库旧的
    ///    工作面（m_working），把刚算出的三点结果静默丢弃 → 时间轴永远按 rate=1.0 走。
    /// 门控与 onUseResult 完全一致：
    /// - noDriftCorrection=true → 只定基准，不应用速率（rateApplied=false）；
    /// - 自洽的大倍率（非实时导出件）→ rateApplied=true + speedVariant=true；
    /// - 其余保持 applyFit 的判定（速率不可信则不应用）。
    static void applyFitDecision(TimeCalibration &cal, bool noDriftCorrection);

    /// v1.18.x：第 2 步（对真实时间）落库前的「并入未应用的第 1 步结果」判据
    /// （纯函数，可测）：
    /// - fitPending=false / fit 非法 / 非 OCR 三点 / 分段模式 → 不动 working，返回 false；
    /// - 否则 working = fit（先过 applyFitDecision 门控）并返回 true。
    /// 调用方（TimeSettingsDialog）负责同步自己的 pending 标志与提示文案。
    static bool absorbPendingFit(TimeCalibration &working, const TimeCalibration &fit,
                                 bool fitPending, bool noDriftCorrection);

    /// v1.18.x：单点错读时的稳健拟合——n≥3 且最差测点残差 > kOutlierResidualMs 时
    /// 剔除该点重拟合（*droppedIndex 回传被剔除样本下标，供 UI 标 ⚠）。
    /// 依据：OCR 错读是常态（真实档实测约 16% 测点 wall 本身错，如年份 2026→2022），
    /// 而一个离群点会让整条仿射拟合失真（三点里一个错点 → 速率必然荒谬 → 整单被拒）。
    /// 剔除后仍不自洽则不剔（避免把“真变速”当错读剔掉）。
    static FitResult fitDroppingWorstOutlier(const QVector<Sample> &samples,
                                             int *droppedIndex = nullptr);

    /// v1.18.x（2026-09-27 顺德件实测）：**日期合理性过滤**。
    /// 同一次取样里，各点画面日期应一致（差 ≤1 天）；差几天/几年必是 OCR 错读
    /// （实测：用户 ROI 把年份末位切掉 → 间歇读成 2023，导致拟合出 34583 倍荒谬速率，
    /// 而「残差最大剔除」反而剔掉正确点 → 整单被拒 → 用户看到「校时不成功」）。
    /// 做法：取墙钟中位数为中心，丢 |偏差| > maxDevMs（默认 1 天）的点。
    /// 返回实际剔除数；*droppedCount 回传（可空）。
    static int dropImplausibleDates(QVector<Sample> *samples,
                                    qint64 maxDevMs = 86400000);

    /// v1.18.x（2026-09-28 顺德 V18 实测）：**结构有效性**检查。
    /// 24 小时制 OSD 的小时应为两位；解析出的 rawText 若呈「单位数小时」
    /// （如 `5:00:02`），几乎必是前导位被框裁掉/漏读 —— 实测真值 15:00:02 读成
    /// 5:00:02 → 整条时间轴偏 **10 小时**（用户截图：画面 15:27:41 / 图表 05:27:40）。
    /// 返回 true = 结构可疑（单位数小时）。
    static bool rawTextHourShort(const QString &rawText);
    /// 剔除结构无效样本（先于离群剔除：否则「正确的点」会被当离群剔掉——
    /// V18 实测：00:00:02 错读 + 16:31 尾巴把中间正确的 15:33:54 挤掉）。
    /// 剩余不足 2 条则不动（返回 0）。返回实际剔除数。
    static int dropStructurallyInvalid(QVector<Sample> *samples);
    /// v1.18.x（2026-09-28 V18 实测）：**修复**「单位数小时」样本而不是丢弃。
    /// 24 小时制 OSD 的小时若只读到一位，真值几乎必是 `10+h`（前导 1 是细笔画，
    /// 压在亮背景上被漏读；实测 15:00:02 → `5:00:02`）。做法：用**其余两位小时样本**
    /// 线性外推到该点，在 {h, 10+h} 里挑与期望值近的那个（差 <2 小时才采用），
    /// 修正 wallMs 并标 ocrSuspicious（**rawText 原样保留**——取证口径）。
    /// 返回修复数。
    static int repairShortHourSamples(QVector<Sample> *samples);

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

// v1.18.x：信号/槽与 QMetaObject::invokeMethod 直驱（ui_chain 回归锁）需要元类型
Q_DECLARE_METATYPE(TimeCalibration)
