/**
 * @file camtilewidget.h
 * @brief 多机同步播放瓦片（ui 层，P-57 N-7）：帧显示 + OSD + 独立放大镜
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-18
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/MULTICAM_PLAYBACK_TECH_DESIGN_CN.md §3.5/§3.6。
 * 轻量自绘控件（不复用 VideoWidget——ROI/快照叠加是多路场景的状态负担）。
 * 放大镜 = 渲染侧裁剪（对已到位解码帧取源矩形放大绘制），引擎零改动：
 * 滚轮以指针为中心缩放（1x~8x），中键拖拽平移，缩回 1x 自动复位居中。
 */
#pragma once

#include <QWidget>
#include <QImage>
#include <QPointF>
#include "displayadjust.h"

class IVideoEngine;

class CamTileWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CamTileWidget(QWidget *parent = nullptr);

    /// 接引擎 frameReady（消费后 ackFrame 归还有界配额，契约同 VideoWidget）
    void setEngine(IVideoEngine *engine);
    void clearFrame();

    // ---- OSD（文本由窗口按服务时钟喂入，本控件不持业务状态）----
    void setLaneName(const QString &name) { m_name = name; update(); }
    void setOsdLines(const QString &line1, const QString &line2);
    void setOsdVisible(bool on);            ///< U-5 可选开关
    void setAudible(bool on);               ///< 🔊 角标
    void setTemporaryBadge(bool on);        ///< “临时对齐（未校时）”角标
    void setLowresBadge(bool on);           ///< “预览降清档”角标（§4 档②）
    void setTruthBadge(bool on);            ///< “✓已对时”角标（v1.16.0：北京时间对时完成）
    /// 缺口/失败占位（非清空帧——保留最后帧下压暗纹提示）
    void setPlaceholder(const QString &text);   ///< 空串 = 取消占位

    /// v1.12.7：画面调节（显示链路 LUT + 90° 步进旋转，与单路同口径；
    /// 仅影响本瓦片显示，引擎原始帧/证据链不变）。默认参数零开销直通。
    /// v1.12.8 起逐瓦片独立参数（窗口按选中瓦片下发）。
    void setDisplayAdjust(const DisplayAdjust &adj, int rotationDegrees);
    /// 选中态（v1.12.8）：强调色外框——画面调节面板作用于选中瓦片
    void setSelected(bool on);
    bool isSelected() const { return m_selected; }

    qreal zoom() const { return m_zoom; }
    /// 帧在控件内的等比适配矩形（标注框选坐标映射用；缩放>1 时语义不含缩放平移）
    QRectF videoFitRect() const { return frameFitRect(); }
    QPointF zoomCenter() const { return m_center; }  ///< 归一化源坐标（导出 PIP 快照用）

signals:
    void clicked();          ///< 单击：切听该路（U-2）
    void openRequested();    ///< 双击：回单路分析（U-6）

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QRectF frameFitRect() const;      ///< 帧在控件内的适配矩形（等比）
    void clampCenter();

    IVideoEngine *m_engine = nullptr;
    QImage m_frame;       ///< 显示帧（调节后；恒等时与 m_raw 浅共享）
    QImage m_raw;         ///< 引擎原始帧（调节输入）
    QByteArray m_lut;     ///< DisplayAdjust::buildLut；空 = 恒等
    int m_rotation = 0;   ///< 顺时针 0/90/180/270
    void rebuildDisplay();

    QString m_name;
    QString m_osd1, m_osd2;
    bool m_osdVisible = true;
    bool m_audible = false;
    bool m_tempBadge = false;
    bool m_lowresBadge = false;
    bool m_truthBadge = false;              ///< v1.16.0：“已对时”绿角标
    QString m_placeholder;

    // 放大镜状态（归一化源坐标：center ∈ [0,1]²，zoom ≥1）
    qreal m_zoom = 1.0;
    QPointF m_center{0.5, 0.5};
    bool m_panning = false;
    QPoint m_panLast;
    bool m_selected = false;
};
