/**
 * @file snapshot_composer.h
 * @brief 证据快照合成器（v1.17.0 P-80，自 MainWindow::onSnapshotQuick 抽出的纯函数渲染模块）
 *
 * 输入集闭合（v1.17.0 方案 C1 拍板）：不接触任何 widget——
 *  视频区：frame（已含画面调节+旋转）+ 覆盖层模型 + 放大镜状态 → 标注烧录/标识框/OSD/分屏
 *  分析区：chartImg/specImg 由调用方离屏预渲染传入（widget 渲染留在 UI 层）
 *  文本：  labelText/fileName 由调用方查好传入（数据获取留在 UI 层）
 * MainWindow 保留：取帧、标签查找、图表离屏渲染、保存路径与状态汇报。
 */
#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>
#include <QDateTime>
#include <QPair>
#include "domain/time_calibration.h"

class RoiModel;
class GuideLineModel;

struct SnapshotInputs {
    QImage frame;               ///< 当前画面帧（已含画面调节+旋转，所见即所得）
    int rotation = 0;           ///< 显示旋转档位（0/90/180/270）
    QSize videoSize;            ///< 原视频原生尺寸（overlay videoSize）
    const RoiModel *regions = nullptr;        ///< ROI 模型（矩形+多边形同指针）
    const GuideLineModel *guideLines = nullptr;
    bool hasMagnifier = false;  ///< 放大镜开着（来源区有效）
    QRect magnifierSourceRect;  ///< 放大镜来源区（存储坐标）
    qreal magnifierZoom = 0.0;  ///< 倍率（≤0 不画徽章）
    QImage magnifiedImage;      ///< 放大视图（含旋转+调节）
    qint64 posMs = 0;           ///< 当前播放位置（流内时间）
    TimeCalibration calibration;///< 校时（dateKnown → OSD 走北京时间）
    QString labelText;          ///< 当前帧最近标签（±1s，无=空）
    QString fileName;           ///< 当前视频文件名（OSD 最上行）
    QImage chartImg;            ///< 预渲染亮度/音量曲线（空=无数据不画段）
    QImage specImg;             ///< 预渲染语谱图（空=无）
};

class SnapshotComposer
{
public:
    /// 全量合成。入帧为空返回 null（调用方负责状态汇报）
    static QImage compose(const SnapshotInputs &in);

    /// 时间码对：dateKnown → 北京时间（yyyyMMdd_HHmmss 标签）；否则相对时间（tH-M-S）
    static QPair<QString, QString> timeCode(const TimeCalibration &cal, qint64 posMs);
};