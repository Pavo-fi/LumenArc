#pragma once

/// @file compose_render.h
/// @brief 合成导出 P2（2026-09-03）：单视频段的分析成果烧录渲染助手——
///        ROI 叠加（矩形+多边形，源像素坐标）与曲线滚动条（亮度+音量+标签+
///        跟随游标），数据自 .vla 一次性载入（SegmentExportEngine 逐段调用）。
///        取证红线：仅演示片重编码链路使用；证据直拷模式永不经过本模块。

#include <QImage>
#include <QPolygon>
#include <QRect>
#include <QVector>
#include "domain/analysis_snapshot.h"

class QPainter;

struct ComposeOverlay {
    bool loaded = false;                 ///< .vla 成功载入
    QVector<QRect> rois;                 ///< 矩形 ROI（源像素坐标）
    QVector<QPolygon> polygons;          ///< 多边形 ROI
    QVector<ChartLabel> labels;          ///< 分析标签（曲线条竖标用）
    QVector<qint64> lumTs;               ///< 亮度共享时间轴（流内 ms）
    QVector<QVector<qreal>> lumRows;     ///< 逐 ROI 亮度行（与 lumTs 等长）
    AudioData audio;                     ///< 音量/语谱（自含时间轴）
    bool hasLum = false;
    bool hasVol = false;
    bool hasData() const { return hasLum || hasVol; }
};

/// 载入 .vla 的分析成果（亮度/音频/ROI/标签）。vlaPath 空或不存在 → loaded=false。
ComposeOverlay loadComposeOverlay(const QString &vlaPath);

/// ROI 叠加：把源像素坐标的 ROI 画到实际显示矩形 dstRect（srcSize=源帧尺寸，
/// 等比映射与 drawImage KeepAspectRatio 同式）。矩形 R1/R2… 标号+多边形描边。
void drawRoiOverlay(QPainter &painter, const QRect &dstRect, const QSize &srcSize,
                    const ComposeOverlay &ov);

/// 曲线滚动条：windowMs 宽窗口、游标固定在 2/3 处（跟随 centerMs 流内时刻）。
/// 亮度逐行折线（ROI 同色）+ 音量曲线（绿）+ 标签竖标+游标三角柄。
/// 无数据时画「无分析数据」占位。
void drawChartStrip(QPainter &painter, const QRect &stripRect,
                    const ComposeOverlay &ov, qint64 centerMs, qint64 windowMs = 30000);
