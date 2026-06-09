/**
 * @file analysis_snapshot.h
 * @brief 不可变分析结果值类型 + CSV 导出
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QVector>
#include <QRect>
#include <QPointF>
#include <QColor>
#include <QString>
#include <QFile>
#include <QTextStream>

struct ChartLabel
{
    qint64 timeMs = 0;
    QString text;
    QColor color = Qt::red;
};

/**
 * @brief Immutable value-type representing the full result of a luminance analysis run.
 *
 * All data is stored as QVector, which uses implicit sharing (copy-on-write),
 * so returning this by value is cheap.
 */
struct AnalysisSnapshot
{
    QVector<qint64> timestamps;
    QVector<QVector<qreal>> values; // outer: region index, inner: time series

    bool isEmpty() const { return timestamps.isEmpty(); }
    int pointCount() const { return timestamps.size(); }
    int regionCount() const { return values.size(); }

    QVector<qreal> series(int regionIndex) const
    {
        if (regionIndex < 0 || regionIndex >= values.size())
            return QVector<qreal>();
        return values[regionIndex];
    }

    qint64 timeAtIndex(int index) const
    {
        if (index < 0 || index >= timestamps.size())
            return -1;
        return timestamps[index];
    }

    int indexAtTime(qint64 timeMs) const
    {
        if (timestamps.isEmpty())
            return -1;
        int left = 0, right = timestamps.size() - 1;
        while (left < right) {
            int mid = left + (right - left) / 2;
            if (timestamps[mid] < timeMs)
                left = mid + 1;
            else
                right = mid;
        }
        return left;
    }

    /**
     * @brief Extract and optionally downsample data for a given viewport.
     *
     * If the number of points inside [tMin, tMax] exceeds maxPoints, a simple
     * bucket-averaging decimation is applied.
     */
    QVector<QPointF> pointsForViewport(int regionIndex,
                                       qint64 tMin, qint64 tMax,
                                       int maxPoints = 5000) const
    {
        QVector<QPointF> result;
        if (regionIndex < 0 || regionIndex >= values.size())
            return result;

        const QVector<qreal> &series = values[regionIndex];
        if (timestamps.isEmpty() || series.isEmpty())
            return result;

        // Guard: ensure series length matches timestamps (protects against malformed data)
        int safeSeriesSize = qMin(series.size(), timestamps.size());

        // Binary search for range
        int iStart = indexAtTime(tMin);
        int iEnd = indexAtTime(tMax);
        if (iEnd < timestamps.size() - 1 && timestamps[iEnd] < tMax)
            ++iEnd;
        iStart = qMax(0, iStart);
        iEnd = qMin(iEnd, safeSeriesSize - 1);
        if (iStart > iEnd)
            return result;

        int count = iEnd - iStart + 1;
        if (count <= maxPoints) {
            result.reserve(count);
            for (int i = iStart; i <= iEnd; ++i)
                result.append(QPointF(static_cast<qreal>(timestamps[i]), series[i]));
            return result;
        }

        // Bucket averaging decimation
        int bucketSize = count / maxPoints;
        if (bucketSize < 2)
            bucketSize = 2;

        result.reserve(count / bucketSize + 1);
        int i = iStart;
        while (i <= iEnd) {
            qint64 tSum = 0;
            qreal vSum = 0.0;
            int n = 0;
            int limit = qMin(i + bucketSize - 1, iEnd);
            for (int j = i; j <= limit; ++j) {
                tSum += timestamps[j];
                vSum += series[j];
                ++n;
            }
            if (n > 0)
                result.append(QPointF(static_cast<qreal>(tSum) / n, vSum / n));
            i = limit + 1;
        }
        return result;
    }

    /**
     * @brief Export data to a CSV file.
     * @param filePath  target file path
     * @param regions   ROI rectangles (for header)
     * @param timeOffsetMs  chart time offset from "Set Time" (0 = use raw ms)
     * @return true on success, false on failure.
     */
    bool exportToCsv(const QString &filePath, const QVector<QRect> &regions,
                     qint64 timeOffsetMs = 0) const
    {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream out(&file);

        // Header: raw ms + formatted time + one column per ROI
        out << "Time(ms),Time";
        for (int r = 0; r < regions.size(); ++r) {
            const QRect &rc = regions[r];
            out << QString(",R%1_Brightness(x%2_y%3_%4x%5)")
                       .arg(r + 1).arg(rc.x()).arg(rc.y())
                       .arg(rc.width()).arg(rc.height());
        }
        out << "\n";

        for (int i = 0; i < timestamps.size(); ++i) {
            qint64 ts = timestamps[i];
            out << ts;
            out << "," << formatTime(ts + timeOffsetMs);
            for (int r = 0; r < values.size(); ++r) {
                out << "," << ((i < values[r].size()) ? values[r][i] : 0.0);
            }
            out << "\n";
        }
        file.close();
        return true;
    }

private:
    static QString formatTime(qint64 ms)
    {
        if (ms < 0) ms = 0;
        int totalSeconds = static_cast<int>(ms / 1000);
        int hours = totalSeconds / 3600;
        int minutes = (totalSeconds % 3600) / 60;
        int seconds = totalSeconds % 60;
        return QString("%1:%2:%3")
            .arg(hours, 2, 10, QChar('0'))
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }

public:
};
