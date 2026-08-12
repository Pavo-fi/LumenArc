/**
 * @file mainwindow.h
 * @brief 主窗口：菜单/工具栏/拖放/快捷键/分析流程编排
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QMainWindow>
#include <QPointer>
#include "domain/analysis_snapshot.h"
#include "domain/timeline_model.h"
#include "domain/time_calibration.h"
#include "videolistpanel.h"
#include "videostatemanager.h"

class TimeSettingsDialog;
class QSystemTrayIcon;

class VideoWidget;
class MagnifierWidget;
class ChartPanel;
class SnapshotOverlay;
class PinnedWidget;
class IVideoEngine;
class IAnalysisEngine;
class CalibrationService;
class RegionModel;
class PolygonModel;
class GuideLineModel;
class QPushButton;
class QLabel;
class QSplitter;
class QDockWidget;
class QProgressBar;
class QSlider;
class VideoListPanel;
class SpectrogramPanelEnhanced;

/** @brief 主窗口，协调所有子组件：视频播放/图表/放大镜/截图叠加/分析引擎 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    enum AnalysisPhase { None, Luminance, Audio };

private slots:
    /// @brief 打开文件对话框并加载视频
    void onOpenFile();
    void onSetStartTime();
    /// @brief 播放视频
    void onPlay();
    /// @brief 暂停播放
    void onPause();
    /// @brief 停止播放
    void onStop();
    /// @brief 启动亮度分析流程（仅当前视频）
    void onAnalyze();
    /// @brief 启动音频分析流程（独立于亮度分析）
    void onAudioAnalysis();
    void onClearRegions();
    void onClearData();
    void onExportCsv();
    void onSaveAnalysis();
    void onLoadAnalysis();
    /// @brief 加载截图叠加参考图
    void onLoadOverlayImage();
    void onDurationChanged(qint64 durationMs);
    void onPositionChanged(qint64 timeMs);
    void onSeekFromChart(qint64 timeMs);

    // Magnifier
    void onMagnifierWheelZoom(int delta, QPoint videoPos);
    void removeMagnifier();

    // Analysis engine callbacks
    void onAnalysisProgress(int analyzed, int total, qreal percent);
    void onAnalysisFinished(const AnalysisSnapshot &snapshot);
    void onAnalysisFailed(const QString &error);

    // Multi-video
    void onVideoSelected(int index);

    // v0.5: 多边形ROI和辅助线
    void onRectMode();
    void onPolygonMode();
    void onGuideLineMode();
    void onCopyRoi();
    void onPasteRoi();
    void onPasteRoiToAll();

private:
    void createMenus();
    void createToolBar();
    void setupConnections();
    void updateTimeDisplay();
    QString formatTime(qint64 ms) const;
    void openVideoFile(const QString &path);
    /// @brief 自动检测系统中的 Python 解释器路径
    QString detectPythonPath() const;
    /// @brief 用 Python 分析引擎的真实帧数/FPS 计算可信时长
    qint64 trustedDurationFor(const QString &path) const;
    void createMagnifier();
    void showVideoContextMenu(const QPoint &pos);
    void updatePinnedImage(const QImage &frame);
    /// @brief 按步进调整播放速度
    void adjustSpeed(float delta);
    /// @brief 循环切换播放速度
    void cycleSpeed();
    /// @brief 统一应用播放速度并更新 UI 和状态提示
    void applySpeed(float speed);
    /// @brief 根据当前播放状态刷新按钮可用性
    void updatePlaybackButtons();
    /// @brief 在状态栏左侧显示操作反馈（5秒后自动清除）
    void showOperationStatus(const QString &text);
    /// @brief 恢复分析状态（区域/校时/标签/截图融合）
    void restoreAnalysisState(const QVector<QRect> &regions,
                               const TimeCalibration &calibration,
                               const QVector<ChartLabel> &labels,
                               const QRect &pinnedRect,
                               const SnapshotFusionData &fusion,
                               const QVector<int> &regionRoiIds = {});

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    VideoWidget *m_videoWidget = nullptr;
    MagnifierWidget *m_magnifier = nullptr;
    bool m_videoListWasExpanded = true;  // 保存呼出放大镜前视频列表状态
    PinnedWidget *m_pinned = nullptr;
    ChartPanel *m_chartPanel = nullptr;
    SnapshotOverlay *m_snapshotOverlay = nullptr;
    IVideoEngine *m_videoEngine = nullptr;
    IAnalysisEngine *m_analysisEngine = nullptr;
    CalibrationService *m_calibrationService = nullptr;
    TimeCalibration m_calibration;   // 当前视频校时 SSOT（.vla v8 持久化）
    QPointer<TimeSettingsDialog> m_calibrationDialog;  // 非模态校时窗口（v1.2.1）
    QPointer<TimeSettingsDialog> m_roiDialog;          // 框选中的校时窗口
    /// 校时长任务完成的 Windows toast 通知（v1.2.2，用户最小化等待场景）
    void showTrayNotification(const QString &title, const QString &message);
    QSystemTrayIcon *m_trayIcon = nullptr;
    /// 时间戳区域持久化（按视频路径 hash，同一摄像头复用）
    QRectF savedTimestampRoi(const QString &videoPath) const;
    void saveTimestampRoi(const QString &videoPath, const QRectF &norm);
    RegionModel *m_regionModel = nullptr;
    PolygonModel *m_polygonModel = nullptr;
    GuideLineModel *m_guideLineModel = nullptr;
    TimelineModel *m_timelineModel = nullptr;

    QSplitter *m_splitter = nullptr;
    QLabel *m_timeLabel = nullptr;

    QPushButton *m_playBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_analyzeBtn = nullptr;
    QPushButton *m_audioAnalysisBtn = nullptr;  // v0.3: 音频分析按钮
    QPushButton *m_setTimeBtn = nullptr;
    QPushButton *m_captureBtn = nullptr;
    QPushButton *m_editBtn = nullptr;
    QPushButton *m_placeBtn = nullptr;
    QPushButton *m_speedBtn = nullptr;
    float m_currentSpeed = 1.0f;

    QString m_currentVideoPath;
    qint64 m_trustedDurationMs = 0;   // 由 Python 分析引擎算出的真实时长
    qint64 m_currentDurationMs = 0;   // 校准后用于 UI 的权威时长
    QTimer *m_seekThrottleTimer = nullptr;  // 拖拽 seek 节流（50ms leading+trailing）
    qint64 m_pendingSeekMs = -1;
    qint64 m_lastIssuedSeekMs = -1;
    QLabel *m_operationLabel = nullptr;   // 操作反馈标签（状态栏左侧）
    QLabel *m_statusLabel = nullptr;
    QLabel *m_hwAdapterLabel = nullptr;   // 硬解适配器名称（状态栏右侧）
    QProgressBar *m_progressBar = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    // Noise floor slider for spectrogram
    QSlider *m_noiseFloorSlider = nullptr;
    QLabel *m_noiseFloorLabel = nullptr;
    QLabel *m_noiseFloorValueLabel = nullptr;

    // Noise reduction slider for audio analysis
    QSlider *m_noiseReductionSlider = nullptr;
    QLabel *m_noiseReductionLabel = nullptr;
    QLabel *m_noiseReductionValueLabel = nullptr;
    qreal m_noiseReductionStrength = 0.0;
    QPushButton *m_nrApplyBtn = nullptr;
    QPushButton *m_toggleVideoListBtn = nullptr;
    QPushButton *m_chartCollapseBtn = nullptr;
    QPushButton *m_spectrogramCollapseBtn = nullptr;
    QPushButton *m_videoListCollapseBtn = nullptr;

    // v0.5: 多边形ROI和辅助线
    QPushButton *m_rectModeBtn = nullptr;
    QPushButton *m_polygonModeBtn = nullptr;
    QPushButton *m_guideLineBtn = nullptr;
    QPushButton *m_copyRoiBtn = nullptr;
    QPushButton *m_pasteRoiBtn = nullptr;
    QVector<QPolygon> m_polygonClipboard;
    QVector<QRect> m_roiClipboard;
    QVector<GuideLine> m_guideLineClipboard;
    QWidget *m_videoListSidebar = nullptr;
    QWidget *m_videoListContent = nullptr;
    QDockWidget *m_videoListPlaceholder = nullptr;
    /// 打开素材转码拼接窗口（v1.2 独立任务窗口）
    void openPreprocessWindow();
    QWidget *m_chartContainer = nullptr;
    QWidget *m_chartContent = nullptr;
    QWidget *m_spectrogramContainer = nullptr;
    QWidget *m_spectrogramContent = nullptr;
    QList<int> m_splitterSizes;
    QList<int> m_chartSavedSizes;
    QList<int> m_spectrogramSavedSizes;
    QRect m_pinnedRect;
    SnapshotFusionData m_snapshotFusion;

    // v0.3: Multi-video and audio visualization
    VideoListPanel *m_videoListPanel = nullptr;
    SpectrogramPanelEnhanced *m_spectrogramEnhanced = nullptr;
    VideoStateManager *m_stateManager = nullptr;
    AnalysisPhase m_analysisPhase = None;       // 当前分析阶段（进度条分离）
};
