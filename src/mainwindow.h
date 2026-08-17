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
class CaseManager;
class CaseDock;
class CaseOpenPanel;
class RoiModel;
class RoiModel;
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

    // v1.8.0 P1a：AnalysisPhase 硬编码两阶段枚举已删（PENDING P-32 勾销）——
    // 分析流程状态由 AnalysisTaskService 状态机持有（R7/R8），MainWindow 仅响应任务信号

private slots:
    /// @brief 打开文件对话框并加载视频
    void onOpenFile();
    /// @brief 临时打开视频（不入案，v1.3.0 M2 任务7）
    void onOpenFileTemporary();
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
    /// 证据快照（2026-08-14）：视频当前帧（所见即所得含画面调节）+
    /// 曲线分析区合成 PNG，OSD 烧录标签/时间码，入案件 snapshots/。
    void onSnapshotQuick();
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

    // Analysis task service callbacks（v1.8.0 P1a：UI 中性信号，取代引擎直连）
    void onTaskStarted(const QString &taskId);
    void onTaskProgress(const QString &taskId, qreal percent, const QString &detail);
    void onTaskFinished(const QString &taskId, const AnalysisSnapshot &snapshot);
    void onTaskFailed(const QString &taskId, const QString &code, const QString &detail);
    void onTaskCancelled(const QString &taskId);

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
    /// @brief 打开视频对话框公共流程（admitToCase=false 为临时打开不入案）
    void openVideosInteractive(bool admitToCase);
    /// @brief 视频入案登记（v1.3.0 M2 任务7）：重复跳过/源旁 .vla 询问导入
    ///        （默认是，复制）/同大小仅提示；无打开案件时直接返回
    void admitVideoToCase(const QString &path, bool interactive);
    /// @brief 进入案件模式（v1.3.0 M2 任务10）：CaseDock 替代视频列表、
    ///        状态栏📁、窗口标题带案件名、开案恢复现场（lastVideoId）
    void enterCaseMode();
    /// @brief 退出案件模式：恢复视频列表/状态栏/标题（不中断播放）
    void exitCaseMode();
    /// @brief 关闭案件流程（面板✕/Ctrl+W/状态栏📁 三出口共调）：
    ///        dirty → 保存/放弃/取消；取消则中止
    void closeCaseWithPrompt();
    /// @brief 窗口标题拼接案件名（案件打开时）
    QString windowTitleWithCase(const QString &base) const;
    /// @brief 新建案件（任务11）：对话框 → createCase → 自动进入案件模式
    void onNewCase();
    /// @brief 打开案件（浏览目录）
    void onOpenCase();
    void onOpenCaseBrowse();              // 面板内「浏览」→ 系统目录选择（2026-08）
    void centerCaseOpenPanel();           // 面板居中于内容区（2026-08）
    /// @brief 打开案件流程（锁冲突提示 force / 警告展示 / 错误报告）
    void openCaseFlow(const QString &dir);
    /// @brief 案件属性对话框
    void onCaseProperties();
    /// @brief 案件根目录设置（QSettings case/rootDir）
    void onCaseRootDir();
    /// @brief 起始页（启动自动显示 + 案件菜单 reopen）
    void onShowStartPage();
    /// @brief 导出移交包（v1.3.0 M3 任务12）
    void onExportCase();
    /// @brief 批量重新定位（v1.3.0 M3 任务13）
    void onBatchRelocate();
    /// @brief 多机时间线对齐只读视图（v1.3.0 M3 任务14）
    void onMultiCamView();
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
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;   // 打开面板居中（2026-08）
    bool eventFilter(QObject *watched, QEvent *event) override;

    VideoWidget *m_videoWidget = nullptr;
    MagnifierWidget *m_magnifier = nullptr;
    bool m_videoListWasExpanded = true;  // 保存呼出放大镜前视频列表状态
    PinnedWidget *m_pinned = nullptr;
    ChartPanel *m_chartPanel = nullptr;
    SnapshotOverlay *m_snapshotOverlay = nullptr;
    IVideoEngine *m_videoEngine = nullptr;
    IAnalysisEngine *m_analysisEngine = nullptr;
    class AnalysisTaskService *m_taskService = nullptr;   ///< v1.8.0 P1a：任务状态机（持引擎信号聚合）
    CalibrationService *m_calibrationService = nullptr;
    /// 案件管理器（v1.3.0 M2）：.vla 路径分流/框选记忆随案 SSOT；
    /// 无打开案件或未入案视频时全部回落独立模式老路径（v1.2.2 逐点一致）
    CaseManager *m_caseManager = nullptr;
    CaseDock *m_caseDock = nullptr;          ///< 案件面板（任务10，仅案件模式可见）
    CaseOpenPanel *m_caseOpenPanel = nullptr; ///< 打开案件面板（2026-08，页面内居中）
    QPushButton *m_caseStatusBtn = nullptr;  ///< 状态栏📁标识（点击=退出案件模式）
    QAction *m_closeCaseAction = nullptr;    ///< 菜单「关闭案件」(Ctrl+W)
    QAction *m_casePropsAction = nullptr;    ///< 菜单「案件属性」
    QAction *m_exportCaseAction = nullptr;   ///< 菜单「导出移交包」(M3)
    QAction *m_batchRelocateAction = nullptr; ///< 菜单「批量重新定位」(M3)
    QAction *m_multiCamAction = nullptr;      ///< 菜单「多机时间线」(M3)
    TimeCalibration m_calibration;   // 当前视频校时 SSOT（.vla v8 持久化）
    QPointer<TimeSettingsDialog> m_calibrationDialog;  // 非模态校时窗口（v1.2.1）
    QPointer<TimeSettingsDialog> m_roiDialog;          // 框选中的校时窗口
    /// 校时长任务完成的 Windows toast 通知（v1.2.2，用户最小化等待场景）
    void showTrayNotification(const QString &title, const QString &message);
    QSystemTrayIcon *m_trayIcon = nullptr;
    /// 时间戳区域持久化（按视频路径 hash，同一摄像头复用）
    QRectF savedTimestampRoi(const QString &videoPath) const;
    void saveTimestampRoi(const QString &videoPath, const QRectF &norm);
    /// v1.7.1：后台保存当前视频 .vla + 同步案件校时徽标（分析完成/校时采用共用）
    void saveCurrentVlaAsync();
    /// QSettings 注册表读取（独立模式路径；案件模式迁移时只读复制源）
    QRectF readTimestampRoiRegistry(const QString &videoPath) const;
    /// 校时徽标文案（写案件内 .vla 时同步刷新 CaseVideoRef 缓存）
    QString calibrationBadgeSummary() const;
    bool m_volumeWarnShown = false;   // v1.7.1：音量破 200% 提示（每会话一次）
    RoiModel *m_roiModel = nullptr;
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
    QPushButton *m_snapshotBtn = nullptr;    ///< 证据快照（视频+曲线合成 PNG）
    QPushButton *m_adjustBtn = nullptr;      ///< 画面调节面板开关
    class PlaybackAdjustPanel *m_adjustPanel = nullptr;
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
};
