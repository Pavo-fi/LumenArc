/**
 * @file mainwindow.h
 * @brief 主窗口：菜单/工具栏/拖放/快捷键/分析流程编排
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QMainWindow>
#include "domain/analysis_snapshot.h"
#include "domain/timeline_model.h"

class VideoWidget;
class MagnifierWidget;
class ChartPanel;
class SnapshotOverlay;
class PinnedWidget;
class IVideoEngine;
class IAnalysisEngine;
class RegionModel;
class QPushButton;
class QLabel;
class QSplitter;
class QDockWidget;
class QProgressDialog;

/** @brief 主窗口，协调所有子组件：视频播放/图表/放大镜/截图叠加/分析引擎 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

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
    /// @brief 启动亮度分析流程
    void onAnalyze();
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
    void onMagnifierWheelZoom(int delta);
    void onMagnifierCursorMoved(QPoint videoPos);
    void removeMagnifier();
    void setMagnifierFollowing(bool follow);

    // Analysis engine callbacks
    void onAnalysisProgress(int analyzed, int total, qreal percent);
    void onAnalysisFinished(const AnalysisSnapshot &snapshot);
    void onAnalysisFailed(const QString &error);

private:
    void createMenus();
    void createToolBar();
    void setupConnections();
    void updateTimeDisplay();
    QString formatTime(qint64 ms) const;
    void openVideoFile(const QString &path);
    /// @brief 自动检测系统中的 Python 解释器路径
    QString detectPythonPath() const;
    void createMagnifier();
    void showVideoContextMenu(const QPoint &pos);
    void updatePinnedImage(const QImage &frame);
    /// @brief 按步进调整播放速度
    void adjustSpeed(float delta);
    /// @brief 根据当前播放状态刷新按钮可用性
    void updatePlaybackButtons();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    VideoWidget *m_videoWidget = nullptr;
    MagnifierWidget *m_magnifier = nullptr;
    bool m_magnifierFollowing = false;
    PinnedWidget *m_pinned = nullptr;
    ChartPanel *m_chartPanel = nullptr;
    SnapshotOverlay *m_snapshotOverlay = nullptr;
    IVideoEngine *m_videoEngine = nullptr;
    IAnalysisEngine *m_analysisEngine = nullptr;
    RegionModel *m_regionModel = nullptr;
    TimelineModel *m_timelineModel = nullptr;

    QSplitter *m_splitter = nullptr;
    QLabel *m_timeLabel = nullptr;

    QPushButton *m_playBtn = nullptr;
    QPushButton *m_pauseBtn = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_analyzeBtn = nullptr;
    QPushButton *m_setTimeBtn = nullptr;
    QPushButton *m_captureBtn = nullptr;
    QPushButton *m_editBtn = nullptr;
    QPushButton *m_placeBtn = nullptr;
    QPushButton *m_speedBtn = nullptr;
    float m_currentSpeed = 1.0f;
    bool m_snapshotOverlayActive = false;

    QString m_currentVideoPath;
    QProgressDialog *m_progressDlg = nullptr;
    QRect m_pinnedRect;
    SnapshotFusionData m_snapshotFusion;
};
