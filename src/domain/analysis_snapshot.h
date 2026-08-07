/**
 * @file analysis_snapshot.h
 * @brief 不可变分析结果值类型 + CSV 导出
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
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
#include <QDateTime>
#include "time_calibration.h"

struct ChartLabel
{
    qint64 timeMs = 0;
    QString text;
    QColor color = Qt::red;
};

/// 图表辅助线的可序列化数据（UI 层的 ChartGuideLine 含图形项指针，不入状态）
struct ChartGuideData
{
    bool horizontal = true;   // true=水平（value 为 Y 值），false=垂直（value 为 X 时间 ms）
    qreal value = 0;
    QColor color = QColor(255, 255, 0, 180);
};

/**
 * @brief Audio analysis data: volume curve + spectrogram.
 *
 * The audio timeline is self-contained: each sample at index i corresponds to
 * time i * timeResolutionMs (ms). Volume/spectrogram accessors therefore do
 * NOT depend on the video luminance timestamps — this keeps audio rendering
 * correct regardless of sample rate, hop length, or whether luminance data
 * exists (e.g. --audio-only mode). (B3)
 */
struct AudioData
{
    QVector<qreal> volume;                    // Normalized volume 0-1
    QVector<QVector<qreal>> spectrogram;      // [freq_bin][time_frame]
    qreal sampleRate = 16000;
    int hopLength = 512;
    int nFft = 1280;
    qreal timeResolutionMs = 32.0;            // ms per audio frame
    qreal specMin = 0;                        // spectrogram min (from Python)
    qreal specMax = 0;                        // spectrogram max (from Python)

    bool isEmpty() const { return volume.isEmpty() && !hasSpectrogram(); }
    bool hasVolume() const { return !volume.isEmpty(); }
    bool hasSpectrogram() const
    {
        return !spectrogram.isEmpty() && !spectrogram[0].isEmpty();
    }

    /// Safe time resolution: never 0 (would cause div-by-zero in viewport math).
    qreal safeTimeResolutionMs() const
    {
        return (timeResolutionMs > 0.0) ? timeResolutionMs : 32.0;
    }

    /// Total audio duration in ms (based on whichever signal is longer).
    qint64 durationMs() const
    {
        qreal res = safeTimeResolutionMs();
        int n = 0;
        if (!volume.isEmpty())
            n = volume.size();
        if (hasSpectrogram())
            n = qMax(n, spectrogram[0].size());
        return static_cast<qint64>(n * res);
    }

    /**
     * @brief Volume points within [tMin, tMax], using the audio's own timeline.
     *
     * Each volume sample i is placed at x = i * timeResolutionMs. No dependency
     * on video timestamps, so this works for --audio-only too. (B3)
     * Downsamples to maxPoints via stride if the viewport is very dense.
     */
    QVector<QPointF> volumePointsForViewport(qint64 tMin, qint64 tMax,
                                              int maxPoints = 8000) const
    {
        QVector<QPointF> result;
        if (volume.isEmpty())
            return result;

        qreal res = safeTimeResolutionMs();
        int iStart = static_cast<int>(tMin / res);
        int iEnd = static_cast<int>(tMax / res) + 1;
        iStart = qMax(0, iStart);
        iEnd = qMin(iEnd, volume.size() - 1);
        if (iStart > iEnd)
            return result;

        int count = iEnd - iStart + 1;
        int stride = (count > maxPoints) ? (count / maxPoints) : 1;
        if (stride < 1) stride = 1;

        result.reserve(count / stride + 1);
        for (int i = iStart; i <= iEnd; i += stride) {
            result.append(QPointF(static_cast<qreal>(i) * res, volume[i]));
        }
        return result;
    }

    /**
     * @brief Spectrogram columns within [tMin, tMax], using the audio's own timeline.
     * Returns [freq_bin][time_frame] slice. (B3)
     */
    QVector<QVector<qreal>> spectrogramForViewport(qint64 tMin, qint64 tMax) const
    {
        if (!hasSpectrogram())
            return {};

        qreal res = safeTimeResolutionMs();
        int nFrames = spectrogram[0].size();
        int aIdxMin = static_cast<int>(tMin / res);
        int aIdxMax = static_cast<int>(tMax / res);
        aIdxMin = qBound(0, aIdxMin, nFrames - 1);
        aIdxMax = qBound(0, aIdxMax, nFrames - 1);

        if (aIdxMin > aIdxMax)
            return {};

        int nFreqBins = spectrogram.size();
        int nCols = aIdxMax - aIdxMin + 1;
        QVector<QVector<qreal>> result(nFreqBins);
        for (int f = 0; f < nFreqBins; ++f) {
            result[f].resize(nCols);
            for (int t = 0; t < nCols; ++t) {
                int srcIdx = aIdxMin + t;
                if (srcIdx < spectrogram[f].size())
                    result[f][t] = spectrogram[f][srcIdx];
                else
                    result[f][t] = -10.0;  // log silence
            }
        }
        return result;
    }
};

/**
 * @brief Metadata for a single data entry, tracking which ROI it belongs to.
 */
struct DataEntry {
    enum Type { Rect = 0, Polygon = 1 };
    Type type = Rect;
    int roiId = -1;
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
    QVector<DataEntry> dataEntries; // parallel to values[], tracks ROI identity
    AudioData audio;                // v0.3: audio analysis data

    bool isEmpty() const { return timestamps.isEmpty(); }
    bool hasAudio() const { return !audio.isEmpty(); }
    int pointCount() const { return timestamps.size(); }
    int regionCount() const { return values.size(); }

    /// Find the data index for a given ROI ID of the given type. Returns -1 if not found.
    int dataIndexOfRoiId(int roiId, DataEntry::Type type) const
    {
        for (int i = 0; i < dataEntries.size(); ++i)
            if (dataEntries[i].roiId == roiId && dataEntries[i].type == type)
                return i;
        return -1;
    }

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
     * @param calibration  校时模型（dateKnown 时 Time 列输出北京时间完整日期，否则 HH:MM:SS）
     * @return true on success, false on failure.
     */
    bool exportToCsv(const QString &filePath, const QVector<QRect> &regions,
                     const TimeCalibration &calibration = TimeCalibration()) const
    {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;

        QTextStream out(&file);

        // Header: raw ms + formatted time + one column per ROI + volume
        out << "Time(ms),Time";
        int rectIdx = 0, polyIdx = 0;
        for (int r = 0; r < values.size(); ++r) {
            if (r < dataEntries.size() && dataEntries[r].type == DataEntry::Polygon) {
                out << QString(",P%1_Brightness").arg(++polyIdx);
            } else {
                if (rectIdx < regions.size()) {
                    const QRect &rc = regions[rectIdx];
                    out << QString(",R%1_Brightness(x%2_y%3_%4x%5)")
                               .arg(rectIdx + 1).arg(rc.x()).arg(rc.y())
                               .arg(rc.width()).arg(rc.height());
                } else {
                    out << QString(",R%1_Brightness").arg(rectIdx + 1);
                }
                rectIdx++;
            }
        }
        if (hasAudio())
            out << ",Volume";
        out << "\n";

        for (int i = 0; i < timestamps.size(); ++i) {
            qint64 ts = timestamps[i];
            out << ts;
            out << "," << formatDisplayTime(ts, calibration);
            for (int r = 0; r < values.size(); ++r) {
                out << "," << ((i < values[r].size()) ? values[r][i] : 0.0);
            }
            if (hasAudio()) {
                // Map timestamp to audio volume index
                int aIdx = static_cast<int>(ts / audio.timeResolutionMs);
                if (aIdx >= 0 && aIdx < audio.volume.size())
                    out << "," << audio.volume[aIdx];
                else
                    out << ",";
            }
            out << "\n";
        }
        file.close();
        return true;
    }

private:
    /// dateKnown：北京时间完整日期（C6 显式精度：秒级，毫秒见 Time(ms) 列）；
    /// 否则维持旧版 HH:MM:SS（语义=日内偏移，行为与 v7 一致）
    static QString formatDisplayTime(qint64 streamMs, const TimeCalibration &cal)
    {
        if (cal.dateKnown) {
            return QDateTime::fromMSecsSinceEpoch(cal.beijingMsOf(streamMs))
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        }
        return formatTime(streamMs + cal.offsetMs);
    }

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

};
