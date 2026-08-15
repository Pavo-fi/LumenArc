/**
 * @file overlap_cut.cpp
 * @brief 重叠段剪切计划实现（v1.7.0 M2）
 */
#include "overlap_cut.h"

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
            const qint64 keepStart = prevWallEnd - s.wallStartMs;
            if (keepStart >= s.streamMs) {
                // 完全包含：后段整体丢弃
                p.dropped = true;
                p.trimmed = true;
            } else {
                p.keepStartMs = keepStart;
                p.trimmed = true;
            }
        }
        prevWallEnd = s.wallEndMs;
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
