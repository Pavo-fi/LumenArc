/**
 * @file livemonitorwindow.h
 * @brief 监控直播窗口：接入录像机网络流 + 辅助线/截图叠加 + 无损录制
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 定位：本窗口是"监控直播"功能的组合根——复用主程序的 VideoWidget
 * （因此辅助线、矩形/多边形 ROI、截图叠加开箱即用），不向 MainWindow 加逻辑
 * （DEVELOPMENT_STANDARDS §8 上帝类红线）。
 */
#pragma once

#include <QWidget>
#include "domain/live/live_source.h"

class VideoWidget;
class SnapshotOverlay;
class LiveStreamEngine;
class LiveRecorder;
class RoiModel;
class GuideLineModel;
class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QTimer;

class LiveMonitorWindow : public QWidget
{
    Q_OBJECT

public:
    explicit LiveMonitorWindow(QWidget *parent = nullptr);
    ~LiveMonitorWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onToggleRecord();
    void onGuideLineToggled(bool on);
    void onClearGuideLines();
    void onLoadReferenceImage();
    void onCaptureReference();
    void onClearReference();
    void onToggleReferenceVisible();
    void onChooseRecordDir();
    void onStreamStarted();
    void onStreamError(const QString &message);
    void onReconnecting(int attempt);
    void onRecordFinished(const QString &path, bool ok, const QString &message);
    void refreshStats();

private:
    /// 状态栏色调（评审 N3：不能再用 m_active 推色，“连接失败”曾因此显示为绿色）
    enum class StatusTone { Idle, Ok, Bad };

    void buildUi();
    void loadSettings();
    void saveSettings() const;
    void applySnapshotToVideo();
    void setSessionUi(bool active);
    void showStatus(const QString &text, StatusTone tone = StatusTone::Ok);
    live::LiveSourceConfig currentConfig() const;

    LiveStreamEngine *m_engine = nullptr;
    LiveRecorder *m_recorder = nullptr;

    VideoWidget *m_video = nullptr;
    SnapshotOverlay *m_snapshot = nullptr;
    RoiModel *m_roiModel = nullptr;
    GuideLineModel *m_guideLineModel = nullptr;

    // 接入参数栏
    QLineEdit *m_hostEdit = nullptr;
    QSpinBox *m_portSpin = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_passEdit = nullptr;
    QSpinBox *m_channelSpin = nullptr;
    QComboBox *m_kindCombo = nullptr;
    QComboBox *m_transportCombo = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_disconnectBtn = nullptr;

    // 工具与录制
    QPushButton *m_guideBtn = nullptr;
    QPushButton *m_clearGuideBtn = nullptr;
    QPushButton *m_refBtn = nullptr;
    QPushButton *m_captureRefBtn = nullptr;
    QPushButton *m_showRefBtn = nullptr;
    QPushButton *m_clearRefBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;
    QComboBox *m_containerCombo = nullptr;
    QPushButton *m_dirBtn = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_statLabel = nullptr;
    QTimer *m_statTimer = nullptr;

    bool m_active = false;       ///< 会话已建立（连接中或直播中）
    bool m_everConnected = false;///< 本会话是否成功出过图（评审 N5：区分“连接中”与“断线重连”）
    bool m_errorShown = false;   ///< 本轮连接是否已弹过错误框（避免重连刷屏）
};
