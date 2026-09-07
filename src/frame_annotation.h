/**
 * @file frame_annotation.h
 * @brief 帧坐标映射 + 覆盖层烧录（v1.17.0 P-80 C0：自 OverlayWidget 迁出的纯渲染模块）
 *
 * 迁移缘由：快照合成（app 层 SnapshotComposer）与合成导出（infrastructure 层
 * SegmentExportEngine）都需要同款渲染，但函数原先是 VideoWidget 子类的静态
 * 成员——外部层调 widgets 层静态函数违反 R1（层间依赖方向）。
 * 迁出后为无 widget 依赖的纯函数模块（仅 Qt Gui + domain 模型）。
 *
 * 行为冻结：函数体逐字迁移（仅去 OverlayWidget:: 前缀），映射语义不变：
 * 存储系（原视频坐标）→ 旋转显示系 → 等比缩放到帧尺寸（与 mapFromVideo 同式）。
 */
#pragma once

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QSize>

class RoiModel;
class GuideLineModel;

namespace FrameAnnotation {

/// 旋转档位下的显示系尺寸（90/270 宽高互换）
QSize displaySizeForRotation(const QSize &videoSize, int rotation);

/// 存储系点 → 显示系点（与 QTransform().rotate(档位) 逐点一致）
QPoint rotateStoredToDisplay(const QPoint &p, const QSize &videoSize, int rotation);

/// 存储系点 → 目标图像坐标（旋转 + 等比缩放；frameSize 可能 ≠ 原生：
/// scrub 降采样帧按比例缩放，整数式与 mapFromVideo 同一）
QPoint mapStoredPointToFrame(const QPoint &stored, const QSize &frameSize,
                             const QSize &videoSize, int rotation);

/// 存储系矩形 → 目标图像矩形（两点映射归一化）
QRect mapStoredRectToFrame(const QRect &stored, const QSize &frameSize,
                           const QSize &videoSize, int rotation);

/// 放大镜来源标识框（§14 Q1）：金色四角括号 + 1px 黑半透明衬影 +
/// 倍率徽章。rect = painter 坐标系下的目标矩形；penWidth/fontPx 由调用方按分辨率缩放。
void drawMagnifierIndicator(QPainter &painter, const QRect &rect,
                            qreal zoom, int penWidth = 2, int fontPx = 12);

/// 快照覆盖层烧录（§14 Q3 拍板）：ROI 矩形/多边形/辅助线按存储系→显示系映射，
/// 全分辨率直接画到 target（合成 PNG 是报告产物，证据文件不动）。
/// frameSize = 目标图像尺寸（可能 ≠ 原生：scrub 降采样帧按比例缩放，
/// 与 mapFromVideo 同一整数式）；模型原色 + 半透明填充，屏上同款观感。
void burnAnnotations(QPainter &painter, const QSize &frameSize,
                     const QSize &videoSize, int rotation,
                     const RoiModel *regions,
                     const RoiModel *polygons,
                     const GuideLineModel *guideLines,
                     int penWidth = 1);

}   // namespace FrameAnnotation