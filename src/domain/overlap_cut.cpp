/**
 * @file overlap_cut.cpp
 * @brief 重叠段剪切计划实现（v1.7.0 M2）
 */
#include "overlap_cut.h"

#include <cmath>

#include <QDateTime>
#include <QFileInfo>

QVector<CutPlan> planOverlapCuts(const QVector<WallSegment> &segs)
{
    QVector<CutPlan> plans;
    plans.reserve(segs.size());

    qint64 prevWallEnd = 0;   // 前一段的有效墙钟止（前段保留完整 → 不变）
    bool havePrev = false;

    for (const WallSegment &s : segs) {
        CutPlan p;
        p.file = s.file;
        p.keepEndMs = 0;      // 默认到文件尾

        if (!havePrev) {
            havePrev = true;
            prevWallEnd = s.wallEndMs;
            plans.append(p);
            continue;
        }

        // 与前一排序段比较（Q-17：剪后一段开头）
        if (s.wallStartMs < prevWallEnd) {
            // 段速率（墙钟跨度/流内时长）：变速/抽帧段的墙钟重叠量必须
            // 换算为流内修剪量（v1.12.0 实测：旧版按 rate=1 直剪会过剪）
            double rate = 1.0;
            if (s.wallEndMs > s.wallStartMs && s.streamMs > 0)
                rate = double(s.wallEndMs - s.wallStartMs)
                     / double(s.streamMs);
            if (rate < 0.01 || rate > 100.0)
                rate = 1.0;   // 异常速率防御回退（排序器已裁决，此处兑底）
            if (s.wallEndMs <= prevWallEnd) {
                // 完全包含（墙钟域）：后段整体丢弃
                p.dropped = true;
                p.trimmed = true;
            } else {
                p.keepStartMs = static_cast<qint64>(std::llround(
                    double(prevWallEnd - s.wallStartMs) / rate));
                // 防御：换算后不得吃掉全段（边界/速率异常时兑底为不丢）
                if (p.keepStartMs >= s.streamMs) {
                    p.dropped = true;
                }
                p.trimmed = true;
            }
        }
        prevWallEnd = qMax(prevWallEnd, s.wallEndMs);
        plans.append(p);
    }
    return plans;
}

QString autoOutputName(const QString &channel, const QString &fallbackName,
                       qint64 wallStartMs, qint64 wallEndMs)
{
    const QString base = channel.isEmpty() ? fallbackName : channel;
    if (wallStartMs <= 0 || wallEndMs <= wallStartMs)
        return base + QStringLiteral(".mp4");

    const QDateTime start = QDateTime::fromMSecsSinceEpoch(wallStartMs);
    const QDateTime end = QDateTime::fromMSecsSinceEpoch(wallEndMs);
    return QStringLiteral("%1_%2_%3-%4.mp4")
        .arg(base)
        .arg(start.toString(QStringLiteral("yyyyMMdd")))
        .arg(start.toString(QStringLiteral("HHmmss")))
        .arg(end.toString(QStringLiteral("HHmmss")));
}
