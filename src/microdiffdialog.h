/**
 * @file microdiffdialog.h
 * @brief 微变分析设置面板（非模态浮窗）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 对标火察的四项交互：标记微变区域（复用主界面 ROI 绘制）、
 * 微变等级可调（本面板"等级"滑条）、颜色增强显示（显示模式）、
 * 微变局部放大（主界面放大镜——微变做在显示链上，放大镜自动获得）。
 *
 * 基准段采集是耗时操作（120 秒基准段约 16 秒），由 MainWindow 执行，
 * 本面板只发起请求并显示进度。
 */
#pragma once

#include <QDialog>

#include "microdiff.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;

class MicroDiffDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MicroDiffDialog(QWidget *parent = nullptr);

    /// @brief 当前界面参数（不含 roi；roi 由 MainWindow 按 RoiModel 填充）
    MicroDiffParams params() const;
    /// @brief 回填参数（不改基准段起点/时长以外的控件状态之外的 roi）
    void setParams(const MicroDiffParams &p);

    /// @brief 预置基准段（视频总时长、当前播放位置）
    void setDefaultSegment(double videoDurationSec, double currentSec);
    /// @brief 显示当前 ROI 数量（MainWindow 在 ROI 变化时刷新）
    void setRoiSummary(int rectCount, int polygonCount);

    void setBusy(bool on);
    void setStatus(const QString &text);
    void setProgress(int percent);
    /// @brief 基准段是否已成功采集（决定“计算变化率曲线”按钮可用性）
    void setBaselineReady(bool ok);

signals:
    /// @brief 显示参数变化（等级/强度/模式/范围/时域/σ/开关）
    void paramsChanged();
    /// @brief 请求采集基准段
    void baselineRequested(double startSec, double durationSec);
    /// @brief 请求计算变化率曲线（全片逐秒 blkMax/medD + 首帧微变判定）
    void curveRequested(double startSec, double durationSec);
    /// @brief 请求取消进行中的基准采集
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void emitChanged();
    void refreshLevelLabel();

    QCheckBox *m_enable = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_scope = nullptr;
    QSlider *m_level = nullptr;
    QLabel *m_levelLabel = nullptr;
    QSlider *m_strength = nullptr;
    QLabel *m_strengthLabel = nullptr;
    QSpinBox *m_temporal = nullptr;

    QDoubleSpinBox *m_baseStart = nullptr;
    QDoubleSpinBox *m_baseDur = nullptr;
    QPushButton *m_collectBtn = nullptr;
    QPushButton *m_curveBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_roiLabel = nullptr;

    bool m_updating = false;
    bool m_baselineReady = false;
};
