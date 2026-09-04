#pragma once

/// @file compose_render.h
/// @brief 合成导出 P2（2026-09-03）：单视频段的分析成果烧录渲染助手——
///        ROI 叠加（矩形+多边形，源像素坐标）与曲线滚动条（亮度+音量+标签+
///        跟随游标），数据自 .vla 一次性载入（SegmentExportEngine 逐段调用）。
///        取证红线：仅演示片重编码链路使用；证据直拷模式永不经过本模块。

#include <QImage>
#include <QPolygon>
#include <QRect>
#include <QRectF>
#include <QVector>
#include "domain/analysis_snapshot.h"
#include "infrastructure/segment_export_engine.h"

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

/// 标注轨（P2.7）：聚光灯（剩余区变暗+聚焦框平滑放大至满幅）/箭头（起→止）/
/// 字幕（底部黑带白字）。dispRect=帧在画布上的实际显示矩形；srcMs=当前源域时刻。
void drawAnnotations(QPainter &painter, const QRect &dispRect, const QImage &frame,
                     const QVector<SegmentExportEngine::Params::ComposeAnno> &annos,
                     qint64 srcMs);

/// 曲线条（P2.8 全量时间段口径，对齐主窗图表）：[rangeStartMs,rangeEndMs) 整段铺显
/// 不滚动不缩放，游标=当前时刻白线贯穿语谱+曲线两区；语谱带（蓝→红热力，低频在下）
/// +亮度逐行折线（ROI 同色）+音量曲线（绿）+标签竖标。无数据时画「无分析数据」占位。
void drawChartStrip(QPainter &painter, const QRect &stripRect,
                    const ComposeOverlay &ov, qint64 cursorMs,
                    qint64 rangeStartMs, qint64 rangeEndMs);
