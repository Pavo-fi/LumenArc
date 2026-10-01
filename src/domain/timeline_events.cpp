#include "timeline_events.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>

namespace {

/// 阶段配色（前三个沿用报告既有橙/琥珀/蓝，后续循环取用）
const QStringList &phasePalette()
{
    static const QStringList p = {
        QStringLiteral("#ff6b35"), QStringLiteral("#ffb703"),
        QStringLiteral("#5aa9e6"), QStringLiteral("#a78bfa"),
        QStringLiteral("#34d399"), QStringLiteral("#f472b6"),
    };
    return p;
}

/// 1~99 的中文数字（阶段名用）
QString cnNumber(int n)
{
    static const QStringList d = {
        QStringLiteral("零"), QStringLiteral("一"), QStringLiteral("二"),
        QStringLiteral("三"), QStringLiteral("四"), QStringLiteral("五"),
        QStringLiteral("六"), QStringLiteral("七"), QStringLiteral("八"),
        QStringLiteral("九"),
    };
    if (n <= 0 || n >= 100)
        return QString::number(n);
    if (n < 10)
        return d.at(n);
    const int t = n / 10, u = n % 10;
    return (t == 1 ? QString() : d.at(t)) + QStringLiteral("十")
           + (u == 0 ? QString() : d.at(u));
}

QString hhmmss(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss"));
}

} // namespace

qint64 snapshotWallMsFromName(const QString &fileName)
{
    // 两种快照命名都认：
    //   LumenArc 写入   <视频basename>_<yyyyMMdd_HHmmss>[_n].png（src/mainwindow_snapshot.cpp）
    //   人工/外部导出   <标签>_<yyyyMMddHHmmss>[_n].png（日期与时分秒之间无下划线）
    // 前缀限定非数字，避免从更长的数字串尾巴上误咬。未校时快照（_tHH-MM-SS）无日期返回 -1。
    static const QRegularExpression re(
        QStringLiteral("(?:^|[^0-9])(\\d{8})_?(\\d{6})(?:_\\d+)?\\.[^./]+$"));
    const QRegularExpressionMatch m = re.match(fileName);
    if (!m.hasMatch())
        return -1;
    const QDateTime dt = QDateTime::fromString(m.captured(1) + m.captured(2),
                                               QStringLiteral("yyyyMMddHHmmss"));
    if (!dt.isValid())
        return -1;
    return dt.toMSecsSinceEpoch();
}

TimelineEvents buildTimelineEvents(const QVector<ReportNodeRow> &nodes,
                                   const QVector<TimelineSnapshotRef> &snapshots,
                                   qint64 mergeWindowMs, qint64 phaseGapMs,
                                   int maxPhases,
                                   const QVector<TimelinePhaseSeed> &phaseSeeds)
{
    TimelineEvents out;

    QVector<TimelineEventItem> items;
    for (const ReportNodeRow &n : nodes) {
        if (n.wallMs <= 0)
            continue;
        TimelineEventItem e;
        e.wallMs = n.wallMs;
        e.title = n.text;
        e.sourceLabel = n.sourceLabel;
        e.source = QStringLiteral("label");
        items << e;
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const TimelineEventItem &a, const TimelineEventItem &b) {
                         return a.wallMs < b.wallMs;
                     });

    // 快照挂到最近的标签上（±窗口），挂不上的自成一条事件
    QVector<TimelineSnapshotRef> snaps;
    for (const TimelineSnapshotRef &s : snapshots)
        if (s.wallMs > 0)
            snaps << s;
    std::stable_sort(snaps.begin(), snaps.end(),
                     [](const TimelineSnapshotRef &a, const TimelineSnapshotRef &b) {
                         return a.wallMs < b.wallMs;
                     });

    QVector<bool> taken(items.size(), false);
    for (const TimelineSnapshotRef &s : snaps) {
        int best = -1;
        qint64 bestDelta = 0;
        for (int i = 0; i < items.size(); ++i) {
            if (taken.at(i) || !items.at(i).imagePath.isEmpty())
                continue;
            const qint64 d = qAbs(items.at(i).wallMs - s.wallMs);
            if (d > mergeWindowMs || (best >= 0 && d >= bestDelta))
                continue;
            best = i;
            bestDelta = d;
        }
        if (best >= 0) {
            taken[best] = true;
            items[best].imagePath = s.path;
            items[best].origName = QFileInfo(s.path).fileName();
            if (items.at(best).sourceLabel.isEmpty())
                items[best].sourceLabel = s.sourceLabel;
            continue;
        }
        TimelineEventItem e;
        e.wallMs = s.wallMs;
        e.title = QStringLiteral("关键帧快照");
        e.sourceLabel = s.sourceLabel;
        e.source = QStringLiteral("snapshot");
        e.imagePath = s.path;
        e.origName = QFileInfo(s.path).fileName();
        items << e;
    }

    std::stable_sort(items.begin(), items.end(),
                     [](const TimelineEventItem &a, const TimelineEventItem &b) {
                         return a.wallMs < b.wallMs;
                     });
    if (items.isEmpty())
        return out;

    // 阶段：优先用人工种子（阶段划分本来就是主观的）；没给种子才用间隔启发式
    out.t0Ms = items.first().wallMs;
    out.totalSec = (items.last().wallMs - out.t0Ms) / 1000;

    QVector<int> phaseOf(items.size(), 0);
    QStringList seedNames;
    if (!phaseSeeds.isEmpty()) {
        QVector<TimelinePhaseSeed> seeds = phaseSeeds;
        std::stable_sort(seeds.begin(), seeds.end(),
                         [](const TimelinePhaseSeed &a, const TimelinePhaseSeed &b) {
                             return a.wallMs < b.wallMs;
                         });
        for (const TimelinePhaseSeed &s : seeds)
            seedNames << s.name;
        for (int i = 0; i < items.size(); ++i) {
            int p = 0;
            for (int s = 0; s < seeds.size(); ++s)
                if (seeds.at(s).wallMs <= items.at(i).wallMs)
                    p = s;
            phaseOf[i] = p;
        }
    } else {
        // 候选切点：间隔超过阈值才算；候选多于上限时只留间隔最大的几个
        QVector<QPair<qint64, int>> cuts;   // (间隔, 切点 = 切在该下标事件之后)
        if (phaseGapMs > 0)
            for (int i = 1; i < items.size(); ++i) {
                const qint64 gap = items.at(i).wallMs - items.at(i - 1).wallMs;
                if (gap > phaseGapMs)
                    cuts << qMakePair(gap, i - 1);
            }
        if (maxPhases > 1 && cuts.size() > maxPhases - 1) {
            std::stable_sort(cuts.begin(), cuts.end(),
                             [](const QPair<qint64, int> &a, const QPair<qint64, int> &b) {
                                 return a.first > b.first;
                             });
            cuts.resize(maxPhases - 1);
        }
        QVector<bool> cutAfter(items.size(), false);
        for (const QPair<qint64, int> &c : cuts)
            cutAfter[c.second] = true;
        int n = 1;
        for (int i = 1; i < items.size(); ++i) {
            if (cutAfter.at(i - 1))
                ++n;
            phaseOf[i] = n - 1;
        }
        for (int p = 0; p < n; ++p)
            seedNames << QString();
    }
    const int phaseCount =
        *std::max_element(phaseOf.constBegin(), phaseOf.constEnd()) + 1;
    for (int p = 0; p < phaseCount; ++p) {
        TimelinePhaseItem ph;
        ph.id = QString(QChar(QLatin1Char('a' + (p % 26))));
        ph.name = seedNames.value(p).isEmpty()
            ? QStringLiteral("阶段") + cnNumber(p + 1)
            : seedNames.at(p);
        ph.shortName = ph.name;
        ph.color = phasePalette().at(p % phasePalette().size());
        qint64 first = 0, last = 0;
        int n = 0;
        for (int i = 0; i < items.size(); ++i) {
            if (phaseOf.at(i) != p)
                continue;
            if (n == 0)
                first = items.at(i).wallMs;
            last = items.at(i).wallMs;
            ++n;
        }
        ph.firstMs = first;
        ph.lastMs = last;
        ph.n = n;
        out.phases << ph;
    }
    out.eventPhase = phaseOf;

    out.events = items;
    return out;
}
