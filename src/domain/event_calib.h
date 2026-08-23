/**
 * @file event_calib.h
 * @brief P-73 多机同事件间接校时——锚点模型 + 拟合 + 取证链（纯函数，无头可测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-22
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 拍板（2026-08-22 用户三连拍板）：
 *  - 一步到位：单锚点偏移 + 多锚点最小二乘仿射同期落地；
 *  - 事件名必填（取证链入报告的可读性依赖）；
 *  - 允许多跳链（现实考量），但【成环禁止】——建锚时沿参考链检查；
 *  - 累积容差逐跳累加、确认卡/报告独立取证链小节如实呈现。
 *
 * 精度声明：同事件对时是【间接/相对传递】，单跳容差 = 两路较粗帧率的半帧
 * （人工对帧），链路容差逐跳累加——UI/报告必须如实展示，不得美化（C1）。
 */
#pragma once

#include <QVector>
#include <QString>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QtGlobal>
#include <cmath>

namespace eventcalib {

/// 同事件锚点：同一现实事件在两路画面中的时刻对
struct EventAnchor {
    QString refLaneId;           ///< 参考路 id（案内 videoId / "T1" 临时路）
    qint64  refStreamMs = -1;    ///< 事件在参考路的流内时刻
    qint64  refWallMs   = -1;    ///< 录入时刻参考路换算墙钟快照（防参考校时后改链断）
    qint64  targetStreamMs = -1; ///< 事件在目标路的流内时刻
    QString eventName;           ///< 事件名（必填——UI 校验，域层不拒空只透传）
    qint64  markedAtMs = 0;      ///< 操作时刻（系统墙钟，留档）
    qint64  toleranceMs = 0;     ///< 本跳对帧容差 = 500ms / min(refFps, targetFps)

    bool isValid() const {
        return !refLaneId.isEmpty() && refStreamMs >= 0 && refWallMs >= 0
               && targetStreamMs >= 0;
    }

    QJsonObject toJson() const {
        QJsonObject o;
        o[QStringLiteral("refLaneId")] = refLaneId;
        o[QStringLiteral("refStreamMs")] = static_cast<double>(refStreamMs);
        o[QStringLiteral("refWallMs")] = static_cast<double>(refWallMs);
        o[QStringLiteral("targetStreamMs")] = static_cast<double>(targetStreamMs);
        o[QStringLiteral("eventName")] = eventName;
        o[QStringLiteral("markedAtMs")] = static_cast<double>(markedAtMs);
        o[QStringLiteral("toleranceMs")] = static_cast<double>(toleranceMs);
        return o;
    }
    static EventAnchor fromJson(const QJsonObject &o) {
        EventAnchor a;
        a.refLaneId = o[QStringLiteral("refLaneId")].toString();
        a.refStreamMs = static_cast<qint64>(o[QStringLiteral("refStreamMs")].toDouble(-1));
        a.refWallMs = static_cast<qint64>(o[QStringLiteral("refWallMs")].toDouble(-1));
        a.targetStreamMs = static_cast<qint64>(o[QStringLiteral("targetStreamMs")].toDouble(-1));
        a.eventName = o[QStringLiteral("eventName")].toString();
        a.markedAtMs = static_cast<qint64>(o[QStringLiteral("markedAtMs")].toDouble(0));
        a.toleranceMs = static_cast<qint64>(o[QStringLiteral("toleranceMs")].toDouble(0));
        return a;
    }
};

/// 对帧容差：两路较粗帧率的半帧（fps 无效时回落 25fps 口径 = 20ms）
inline qint64 frameToleranceMs(double refFps, double targetFps)
{
    double f = qMin(refFps, targetFps);
    if (f <= 1.0)
        f = 25.0;
    return static_cast<qint64>(500.0 / f + 0.5);
}

/// 锚点组拟合结果
struct FitResult {
    bool ok = false;
    bool affine = false;          ///< false=单锚点偏移型（rate 按 1）
    double offsetMs = 0.0;        ///< 偏移型：wall = stream + offsetMs
    double rate = 1.0;            ///< 仿射型：wall = rate*stream + interceptMs
    double interceptMs = 0.0;
    double sigmaRate = 0.0;       ///< 速率标准误（报告用）
    QVector<double> residualsMs;  ///< 各锚点 |预测墙钟 - 录入墙钟|
    QString error;                ///< C1：错误类型化前缀
};

/// 由锚点组拟合校时参数（录入快照 refWallMs 为墙钟真值，不依赖参考路现状）
/// 0 锚点 → error；1 锚点 → 偏移型；≥2 → 最小二乘仿射（极端 rate 拒收：
/// rate≤0 或 |rate-1|>0.2 视为对帧错误，C1 报错不静默）
inline FitResult fitAnchors(const QVector<EventAnchor> &anchors)
{
    FitResult r;
    QVector<EventAnchor> valid;
    for (const auto &a : anchors)
        if (a.isValid())
            valid.append(a);
    if (valid.isEmpty()) {
        r.error = QStringLiteral("EVENTCALIB_NO_ANCHOR: 无有效锚点");
        return r;
    }
    if (valid.size() == 1) {
        r.ok = true;
        r.affine = false;
        r.offsetMs = double(valid[0].refWallMs - valid[0].targetStreamMs);
        r.rate = 1.0;
        r.residualsMs.append(0.0);
        return r;
    }
    // 最小二乘：wall = rate * stream + intercept
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const int n = valid.size();
    for (const auto &a : valid) {
        const double x = double(a.targetStreamMs);
        const double y = double(a.refWallMs);
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    const double denom = n * sxx - sx * sx;
    if (std::abs(denom) < 1.0) {   // 所有锚点目标时刻几乎相同 → 退化为偏移型
        r.ok = true;
        r.affine = false;
        r.offsetMs = double(valid[0].refWallMs - valid[0].targetStreamMs);
        r.residualsMs.resize(n);
        for (int i = 0; i < n; ++i)
            r.residualsMs[i] = std::abs(
                double(valid[i].targetStreamMs) + r.offsetMs
                - double(valid[i].refWallMs));
        return r;
    }
    r.rate = (n * sxy - sx * sy) / denom;
    r.interceptMs = (sy - r.rate * sx) / n;
    if (r.rate <= 0.0 || std::abs(r.rate - 1.0) > 0.2) {
        r.error = QStringLiteral(
            "EVENTCALIB_BAD_RATE: 拟合速率 %1 异常（锚点疑似对错事件），请检查锚点")
                      .arg(r.rate, 0, 'f', 4);
        return r;
    }
    r.affine = true;
    r.ok = true;
    r.residualsMs.resize(n);
    double s2 = 0;
    for (int i = 0; i < n; ++i) {
        const double pred = r.rate * double(valid[i].targetStreamMs) + r.interceptMs;
        r.residualsMs[i] = std::abs(pred - double(valid[i].refWallMs));
        s2 += r.residualsMs[i] * r.residualsMs[i];
    }
    if (n > 2)
        r.sigmaRate = std::sqrt(s2 / (n - 2) / denom * n);
    return r;
}

namespace detail {
/// refLane 的参考链上游是否可达 needle（多锚点多参考逐边递归）
inline bool reachableUpstream(
    const QString &from, const QString &needle,
    const QHash<QString, QVector<EventAnchor>> &byLane, QSet<QString> &visited)
{
    if (from == needle)
        return true;
    if (from.isEmpty() || visited.contains(from))
        return false;
    visited.insert(from);
    const auto it = byLane.constFind(from);
    if (it == byLane.constEnd())
        return false;
    for (const auto &a : it.value())
        if (reachableUpstream(a.refLaneId, needle, byLane, visited))
            return true;
    return false;
}
} // namespace detail

/// 成环守卫：targetLane 若以 refLane 为参考建锚，refLane 上游可达
/// targetLane 即成环——拒收（C1 显式 false，调用方提示）。调用方须把
/// 本会话已收未存的锚点也并入 anchorsByLane 再查。
inline bool wouldCreateCycle(
    const QString &targetLane, const QString &refLane,
    const QHash<QString, QVector<EventAnchor>> &anchorsByLane)
{
    if (targetLane == refLane)
        return true;
    QSet<QString> visited;
    return detail::reachableUpstream(refLane, targetLane, anchorsByLane, visited);
}

/// 取证链一跳（报告/确认卡用）
struct ChainHop {
    QString laneId;              ///< 本跳被校时的路
    EventAnchor anchor;          ///< 该路锚点（多锚点取首个/逐跳详见 anchors 全表）
    bool absolute = false;       ///< 该路为绝对校时锚（OCR/校时图片等，无上游）
};

/// 自 laneId 沿参考链展开到绝对锚（每路取首个锚点定主链；多锚点全表由
/// TimeCalibration.eventAnchors 另行列出）。成环防御：visited 截断。
inline QVector<ChainHop> expandChain(
    const QString &laneId,
    const QHash<QString, QVector<EventAnchor>> &anchorsByLane,
    const QSet<QString> &absoluteLaneIds)
{
    QVector<ChainHop> chain;
    QSet<QString> visited;
    QString cur = laneId;
    while (!cur.isEmpty() && !visited.contains(cur)) {
        visited.insert(cur);
        ChainHop hop;
        hop.laneId = cur;
        if (absoluteLaneIds.contains(cur)) {
            hop.absolute = true;
            chain.append(hop);
            break;
        }
        const auto it = anchorsByLane.constFind(cur);
        if (it == anchorsByLane.constEnd() || it->isEmpty()) {
            hop.absolute = true;   // 无锚点记录 = 链顶（防御：按绝对锚收束）
            chain.append(hop);
            break;
        }
        hop.anchor = it->first();
        chain.append(hop);
        cur = it->first().refLaneId;
    }
    return chain;
}

/// 链累积容差 = 逐跳容差之和（报告诚实性声明用）
inline qint64 cumulativeToleranceMs(const QVector<ChainHop> &chain)
{
    qint64 sum = 0;
    for (const auto &h : chain)
        if (!h.absolute)
            sum += h.anchor.toleranceMs;
    return sum;
}

/// ---------------------------------------------------------------------------
/// P-73 UX 重做（v1.13.3）：引导状态机 + 口语化文案（纯函数，可测试）
/// ---------------------------------------------------------------------------

/// 引导步骤：0=选两路（准钟/要修的钟） 1=打「同一瞬间」标记 2=可预览
/// （第 2 个标记自愿） 3=预览中（确认后保存）
inline int guidanceStep(bool lanesPicked, int anchorCount, bool previewed)
{
    if (!lanesPicked)
        return 0;
    if (anchorCount <= 0)
        return 1;
    return previewed ? 3 : 2;
}

/// 口语化钟差：offsetMs = 准钟墙钟 - 目标流内毫秒（>0 目标的钟慢）
/// 例：「目标的钟慢 2 分 14 秒」
inline QString plainClockDeltaText(qint64 offsetMs)
{
    const bool slow = offsetMs >= 0;
    qint64 ms = offsetMs < 0 ? -offsetMs : offsetMs;
    const qint64 h = ms / 3600000; ms %= 3600000;
    const qint64 m = ms / 60000;   ms %= 60000;
    const qint64 s = ms / 1000;
    QString span;
    if (h > 0)
        span = QStringLiteral("%1 小时 %2 分").arg(h).arg(m);
    else if (m > 0)
        span = QStringLiteral("%1 分 %2 秒").arg(m).arg(s);
    else
        span = QStringLiteral("%1 秒").arg(s + (ms % 1000) / 1000.0, 0, 'f', 1);
    return slow ? QStringLiteral("目标的钟慢 %1").arg(span)
                : QStringLiteral("目标的钟快 %1").arg(span);
}

} // namespace eventcalib
