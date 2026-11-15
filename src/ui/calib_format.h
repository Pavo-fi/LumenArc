/**
 * @file calib_format.h
 * @brief 校时速率唯一人读口径：把 rate 翻成「比标准时间每天快/慢 X 秒」
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-11-15
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 为什么收口到这里（2026-11-15 用户拍板：「已经确定了每天比标准时间快/慢了，
 * 要明确是快还是慢」）：此前界面 12 处各写各的口径——「· 漂移 2.3 秒/天」
 * 「时钟每天快/慢 %1 秒」（二选一没落地，等于没说）「画面每走 100 秒真实约走
 * X 秒」（rate>1 是画面钟快，那句却写「画面相对真实慢」，方向是反的；而且 100
 * 秒窗口上 2 位小数的分辨率只有约 8.6 秒/天，2.3 秒/天会显示成「100.00、慢
 * 0.00%」）。统一后：**方向词（快/慢）永远显式写出**，数值一律「每天多少秒」。
 *
 * 域内口径与 TimeCalibration 完全一致（本文件只产文案，不做任何换算——
 * 换算唯一入口仍是 wallMsOf()，见 C3）：
 *   wallMs = offsetMs + rate × streamMs；rate > 1 = 画面（机位）时钟走快；
 *   每天漂移 = (rate − 1) × 86400（正=快、负=慢）；每小时 = 每天 / 24；
 *   rateApplied=false 时 rate 仍是拟合值，只是换算取 1.0（effectiveRate()）；
 *   不确定度 = sigmaRate × 86400（±秒/天）。
 *
 * 三档（同一句话只用一档，别把长短口径串起来）：
 *   calibDriftPhrase()  整句，不带「已修正/未修正」括注——上下文由调用方自己接
 *   calibDriftText()    = 整句 +「每 1 小时」细节 + 已修正/未修正括注（默认档）
 *   calibDriftBadge()   一行紧凑（检视器 / 案件树 / .vla 徽标）
 * 双语一律 lang("中文","English")；报告模块通篇中文（见 domain/report_fmt.h），
 * 故另有 *Zh() 强制中文档，避免界面切英文时把英文句子插进中文报告。
 */
#pragma once

#include "domain/time_calibration.h"
#include "i18n.h"

#include <QString>
#include <cmath>

/// 间接校时（同事件对时，源=CrossCamEvent）的 rate 语义与本文件默认口径相反：
/// event_calib 拟合的是 refWall = rate × targetStream，分子是**参考机位**的钟
/// （src/domain/event_calib.h 最小二乘那段）。所以 rate>1 表示「本路时间轴比参考
/// 机位慢，要乘 >1 拉伸」；直接套 (rate-1) 会得到相反方向——2026-11-15 审查 P0。
inline bool calibIsIndirect(const TimeCalibration &cal)
{
    return cal.source == TimeCalibration::Source::CrossCamEvent;
}

/// 每天漂移秒数（正 = 机位/画面时钟走快）。唯一口径来源，调用方勿自行重算
inline double calibDriftPerDaySec(const TimeCalibration &cal)
{
    // 间接校时取倒数：把「参考路墙钟/本路流」翻成「本路相对参考路」的快慢
    if (calibIsIndirect(cal) && cal.rate > 0.0)
        return (1.0 / cal.rate - 1.0) * 86400.0;
    return cal.driftSecondsPerDay();
}

/// 比较基准的名字：直接校时比的是标准时间，间接校时比的是参考机位
inline QString calibRefNoun(bool zh)
{
    return zh ? QStringLiteral("参考机位") : QStringLiteral("the reference camera");
}

/// 每天漂移不确定度（±秒/天，由拟合速率标准误 σ_rate 折算）
inline double calibDriftSigmaSec(const TimeCalibration &cal)
{
    return std::fabs(cal.sigmaRate) * 86400.0;
}

/// 每天差不到 1 秒 → 钟是准的。这种时候报方向只会误导（本就是读数噪声）
inline bool calibDriftNegligible(const TimeCalibration &cal)
{
    return std::fabs(calibDriftPerDaySec(cal)) < 1.0;
}

/// σ > 1.5×|漂移| → 方向都定不下来：数值只是「最佳估计」，不是结论
inline bool calibDriftUnmeasurable(const TimeCalibration &cal)
{
    return calibDriftSigmaSec(cal) > std::fabs(calibDriftPerDaySec(cal)) * 1.5;
}

/// 秒数可读化：|x| ≥ 100 取整（±202 秒/天），否则 1 位小数（9.0 / ±5.3）
inline QString calibSecText(double sec)
{
    const double a = std::fabs(sec);
    return QString::number(a, 'f', a >= 100.0 ? 0 : 1);
}

namespace calibfmt {

/// 一天的量用「秒/分/小时」表达：变速件那句「每天快 12960 秒」没人看得懂
inline QString perDay(double perDaySec, bool zh)
{
    const double a = std::fabs(perDaySec);
    if (a < 100.0)
        return zh ? QStringLiteral("%1 秒").arg(calibSecText(a))
                  : QStringLiteral("%1 s").arg(calibSecText(a));
    if (a < 3600.0)
        return zh ? QStringLiteral("%1 分").arg(calibSecText(a / 60.0))
                  : QStringLiteral("%1 min").arg(calibSecText(a / 60.0));
    return zh ? QStringLiteral("%1 小时").arg(calibSecText(a / 3600.0))
              : QStringLiteral("%1 h").arg(calibSecText(a / 3600.0));
}

/// 方向词（rate − 1 > 0 = 快）——所有口径都必须带上它
inline QString word(bool fast, bool zh)
{
    if (zh)
        return fast ? QStringLiteral("快") : QStringLiteral("慢");
    return fast ? QStringLiteral("faster") : QStringLiteral("slower");
}

// ---- 三档 × 双语的落地（对外一律经 lang() 取用）----

inline QString phraseZh(const TimeCalibration &cal)
{
    // 间接校时的比较基准是参考机位（rate 语义相反，见 calibIsIndirect）
    const QString ref = calibIsIndirect(cal) ? calibRefNoun(true)
                                             : QStringLiteral("标准时间");
    if (calibDriftNegligible(cal)) {
        return calibIsIndirect(cal)
            ? QStringLiteral("与参考机位的时间差每天不到 1 秒")
            : QStringLiteral("时钟与标准时间基本一致（每天差 ≤1 秒）");
    }
    const double d = calibDriftPerDaySec(cal);
    const bool fast = d > 0.0;
    if (calibDriftUnmeasurable(cal)) {
        // 测不准：方向词 + 最佳估计 + 误差都要留着，最后落在「测不准」上——
        // 只报一个数，用户会把噪声当结论
        return QStringLiteral("该机位时钟比%1%2，最佳估计每天约 %3 秒，"
                              "但本次测量误差 ±%4 秒/天——观测跨度不够，快慢测不准")
            .arg(ref, word(fast, true), calibSecText(d),
                 calibSecText(calibDriftSigmaSec(cal)));
    }
    return QStringLiteral("该机位时钟比%1每天%2约 %3 秒")
        .arg(ref, word(fast, true), calibSecText(d));
}

inline QString phraseEn(const TimeCalibration &cal)
{
    const QString ref = calibIsIndirect(cal)
        ? calibRefNoun(false)
        : QStringLiteral("standard time");
    if (calibDriftNegligible(cal)) {
        return calibIsIndirect(cal)
            ? QStringLiteral("Within 1 s/day of the reference camera")
            : QStringLiteral("Clock agrees with standard time (within 1 s/day)");
    }
    const double d = calibDriftPerDaySec(cal);
    const bool fast = d > 0.0;
    if (calibDriftUnmeasurable(cal))
        return QStringLiteral("This camera's clock runs %1 than %2; best estimate "
                              "~%3 s/day, but measurement error ±%4 s/day — "
                              "observation span too short - fast/slow NOT measurable")
            .arg(word(fast, false), ref, calibSecText(d),
                 calibSecText(calibDriftSigmaSec(cal)));
    return QStringLiteral("This camera's clock runs %1 than %2 by about %3 per day")
        .arg(word(fast, false), ref, perDay(d, false));
}

inline QString textZh(const TimeCalibration &cal, bool withDetail)
{
    QString s = phraseZh(cal);
    if (calibDriftNegligible(cal))
        return s;                        // 准钟：细节和括注都不再加
    const bool fuzzy = calibDriftUnmeasurable(cal);
    // 方向句后面接「每 1 小时」，最后才是状态括注（是一句话，不是三段拼贴）
    if (withDetail && !fuzzy)
        s += QStringLiteral("，约合每 1 小时 %1 秒")
                 .arg(calibSecText(calibDriftPerDaySec(cal) / 24.0));
    // 测不准时「每小时」没有意义（方向都没定下来），只交代本次有没有动手
    if (fuzzy)
        return s + (cal.rateApplied
                        ? QStringLiteral("（本次已按最佳估计修正时间轴）")
                        : QStringLiteral("（本次未修正）"));
    if (cal.rateApplied)
        return s + QStringLiteral("（已修正到时间轴）");
    const double sigma = calibDriftSigmaSec(cal);
    return s + (sigma > 0.0
                    ? QStringLiteral("（不确定度 ±%1 秒/天，本次未修正，"
                                     "时间轴按「不快不慢」处理）").arg(calibSecText(sigma))
                    : QStringLiteral("（本次未修正，时间轴按「不快不慢」处理）"));
}

inline QString textEn(const TimeCalibration &cal, bool withDetail)
{
    QString s = phraseEn(cal);
    if (calibDriftNegligible(cal))
        return s;
    const bool fuzzy = calibDriftUnmeasurable(cal);
    if (withDetail && !fuzzy)
        s += QStringLiteral(", about %1 per hour")
                 .arg(perDay(calibDriftPerDaySec(cal) / 24.0, false));
    if (fuzzy)
        return s + (cal.rateApplied ? QStringLiteral(" (applied at best estimate)")
                                    : QStringLiteral(" (NOT applied this run)"));
    if (cal.rateApplied)
        return s + QStringLiteral(" (corrected on the timeline)");
    const double sigma = calibDriftSigmaSec(cal);
    return s + (sigma > 0.0
                    ? QStringLiteral(" (±%1 s/day uncertainty, NOT applied: the "
                                     "timeline treats this clock as neither fast "
                                     "nor slow)").arg(calibSecText(sigma))
                    : QStringLiteral(" (NOT applied: the timeline treats this clock "
                                     "as neither fast nor slow)"));
}

inline QString badgeZh(const TimeCalibration &cal)
{
    if (calibDriftNegligible(cal))
        return QStringLiteral("每天差 ≤1 秒");
    // 倍率型（>1% ≈ 14.4 分/天）基本是变速/非实时导出件：说「每天 X 秒」没人
    // 有概念，直接报倍率（与旧「1.003×」口径可对照）
    if (std::fabs(cal.rate - 1.0) > TimeCalibration::kMaxSaneRateDev)
        return QStringLiteral("时钟倍率 %1×").arg(cal.rate, 0, 'f', 3);
    const double d = calibDriftPerDaySec(cal);
    if (calibDriftUnmeasurable(cal))
        return QStringLiteral("快慢测不准（±%1 秒/天）")
            .arg(calibSecText(calibDriftSigmaSec(cal)));
    return QStringLiteral("每天%1约 %2").arg(word(d > 0.0, true), perDay(d, true));
}

inline QString badgeEn(const TimeCalibration &cal)
{
    if (calibDriftNegligible(cal))
        return QStringLiteral("within 1 s/day");
    if (std::fabs(cal.rate - 1.0) > TimeCalibration::kMaxSaneRateDev)
        return QStringLiteral("clock rate %1x").arg(cal.rate, 0, 'f', 3);
    const double d = calibDriftPerDaySec(cal);
    if (calibDriftUnmeasurable(cal))
        return QStringLiteral("fast/slow unmeasurable (±%1 s/day)")
            .arg(calibSecText(calibDriftSigmaSec(cal)));
    return QStringLiteral("about %1/day %2").arg(perDay(d, false),
                                                 word(d > 0.0, false));
}

} // namespace calibfmt

/// 整句档（不带状态括注）：适合「结果行」「测得…但未采用…」这类自带上下文的文案
inline QString calibDriftPhrase(const TimeCalibration &cal)
{
    return lang(calibfmt::phraseZh(cal), calibfmt::phraseEn(cal));
}

/// 默认档：整句 +「每 1 小时」细节 + 已修正/未修正括注
inline QString calibDriftText(const TimeCalibration &cal, bool withDetail = true)
{
    return lang(calibfmt::textZh(cal, withDetail), calibfmt::textEn(cal, withDetail));
}

/// 一行紧凑档（检视器 / 案件树徽标 / .vla 徽标）
inline QString calibDriftBadge(const TimeCalibration &cal)
{
    return lang(calibfmt::badgeZh(cal), calibfmt::badgeEn(cal));
}

/// 强制中文三档：报告/留档串专用（报告模块通篇中文，不随界面语言切换）
inline QString calibDriftPhraseZh(const TimeCalibration &cal)
{
    return calibfmt::phraseZh(cal);
}
inline QString calibDriftTextZh(const TimeCalibration &cal, bool withDetail = true)
{
    return calibfmt::textZh(cal, withDetail);
}
inline QString calibDriftBadgeZh(const TimeCalibration &cal)
{
    return calibfmt::badgeZh(cal);
}
