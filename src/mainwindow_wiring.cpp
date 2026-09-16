/**
 * @file mainwindow_wiring.cpp
 * @brief v1.17.0 P-79：自 mainwindow.cpp 按职责域拆出（行为冻结纯移动，定义仍属 MainWindow）
 */
#include "mainwindow.h"
#include "keyguardfilter.h"
#include "videowidget.h"
#include "chartpanel.h"
#include "domain/roi_model.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/ivideo_engine.h"
#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/ffmpeg_video_engine.h"
#include "app/calibration_service.h"
#include "app/report_service.h"
#include "app/report_docx_builder.h"
#include "reportpreflightdialog.h"
#include "sitemapeditordialog.h"
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include "app/case_manager.h"
#include "app/case_open_panel.h"
#include "app/analysis_task_service.h"
#include "microdiffdialog.h"   // 清基准时同步面板按钮态（曲线按钮可用性）
#include "app/analysis_controller.h"
#include "app/uistate.h"
#include "app/project_io.h"
#include "app/video_session_manager.h"
#include "domain/task_registry.h"
#include "casedock.h"
#include "casedialogs.h"
#include "multicamplaybackwindow.h"
#include "timesettingsdialog.h"
#include "magnifierwidget.h"
#include "fullscreenvideowindow.h"
#include "snapshotoverlay.h"
#include "playbackadjustpanel.h"
#include "displayadjust.h"
#include "pinnedwidget.h"
#include "videolistpanel.h"
#include "preprocesswindow.h"
#include "composeworkbench.h"
#include "infrastructure/segment_export_engine.h"
#include <QProgressDialog>
#include "spectrogrampanel_enhanced.h"
#include "i18n.h"
#include "aboutdialog.h"
#include "accountdialog.h"
#include "feedbackdialog.h"
#include "logindialog.h"
#include "infrastructure/credential_store.h"
#include "theme.h"

#include <QListWidget>
#include <QSplitter>
#include <QMenuBar>
#include <QToolBar>
#include <QPushButton>
#include <QtConcurrent>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QInputDialog>
#include <QTime>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <QSettings>
#include <QCryptographicHash>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QKeyEvent>
#include <QDesktopServices>
#include <QLineEdit>
#include <QTextEdit>
#include <QSlider>
#include <QTreeWidget>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QActionGroup>
#include <QDialog>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>

/// @brief 连接所有信号槽：引擎→UI更新/按钮→槽/截图同步
/// v1.17.0 P-79：原 487 行单函数按职责域拆为 8 个接线函数（行为冻结：
/// connect 集合与内容零改动；各 (信号, 槽) 对唯一，跨域注册顺序行为中性）
void MainWindow::setupConnections()
{
    setupTransportConnections();
    setupAudioSpectrogramConnections();
    setupAnalysisConnections();
    setupMagnifierConnections();
    setupRoiConnections();
    setupSnapshotFusionConnections();
    setupVideoListConnections();
    setupCaseConnections();
}

/// @brief 传输域：播放按钮/引擎时间信号/图表与语谱游标 seek
void MainWindow::setupTransportConnections()
{
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlay);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_speedBtn, &QPushButton::clicked, this, &MainWindow::cycleSpeed);
    connect(m_analyzeBtn, &QPushButton::clicked, this, &MainWindow::onAnalyze);
    connect(m_audioAnalysisBtn, &QPushButton::clicked, this, &MainWindow::onAudioAnalysis);
    connect(m_microDiffBtn, &QPushButton::clicked, this, &MainWindow::onMicroDiff);
    connect(m_setTimeBtn, &QPushButton::clicked, this, &MainWindow::onSetStartTime);

    connect(m_videoEngine, &IVideoEngine::positionChanged,
            this, &MainWindow::onPositionChanged);
    connect(m_videoEngine, &IVideoEngine::durationChanged,
            this, &MainWindow::onDurationChanged);

    connect(m_chartPanel, &ChartPanel::seekRequested,
            this, &MainWindow::onSeekFromChart);
    // 拖拽松手：退出 scrub 模式 + 最终精确 seek
    connect(m_chartPanel, &ChartPanel::scrubEnded, this, [this]() {
        m_videoEngine->setScrubMode(false);
        qint64 pos = m_chartPanel->cursorTime();
        // 审查 F-2：seek 落点后重置微变时域环（防跨跳变平均出假变化云）
        if (m_videoWidget)
            m_videoWidget->resetMicroDiffTemporal();
        m_videoEngine->seek(pos);
    });

    // Spectrogram cursor drag -> seek
    connect(m_spectrogramEnhanced, &SpectrogramPanelEnhanced::seekRequested,
            this, &MainWindow::onSeekFromChart);
    // 语谱拖拽松手：退出 scrub 模式 + 最终精确 seek（光标在拖拽中已两面板同步）
    connect(m_spectrogramEnhanced, &SpectrogramPanelEnhanced::scrubEnded, this, [this]() {
        m_videoEngine->setScrubMode(false);
        if (m_videoWidget)
            m_videoWidget->resetMicroDiffTemporal();   // 审查 F-2
        m_videoEngine->seek(m_chartPanel->cursorTime());
    });
}

/// @brief 音频/语谱域：降噪滑杆与「应用」/两面板 X 轴双向联动
void MainWindow::setupAudioSpectrogramConnections()
{
    // Noise floor slider: update spectrogram min value
    connect(m_noiseFloorSlider, &QSlider::valueChanged, this, [this](int value) {
        qreal db = value / 10.0;
        m_noiseFloorValueLabel->setText(QString::number(db, 'f', 1));
        m_noiseFloorSlider->setToolTip(QString("底噪阈值: %1 dB").arg(db, 0, 'f', 1));
        if (m_spectrogramEnhanced) {
            m_spectrogramEnhanced->setNoiseFloor(db);
        }
    });

    // Noise reduction slider
    connect(m_noiseReductionSlider, &QSlider::valueChanged, this, [this](int value) {
        m_playbackSettings->setNoiseReductionStrength(value / 10.0);
        m_noiseReductionValueLabel->setText(QString::number(m_playbackSettings->noiseReductionStrength(), 'f', 1));
        // P-54b：播放降噪强度实时跟随（原子热更新，下一帧生效，不用点应用）；
        // 分析显示链路仍需「应用」重跑（图是离线数据渲染的）
        applyPlaybackDenoiseSetting();
    });
    // Apply button: directly connected via member variable
    connect(m_nrApplyBtn, &QPushButton::clicked, this, [this]() {
        // P-54（v1.16.1 落地）：libav 引擎原生谱门控降噪——
        // 强度随任务下发引擎（onAudioAnalysis 内统一读取滑杆值）；
        // 调回 0 再应用 = 重跑干净分析复原。
        if (!m_sessionMgr->currentVideoPath().isEmpty())
            onAudioAnalysis();
    });

    // v0.35: Spectrogram X-axis bidirectional sync with ChartPanel
    // Chart zoom/pan → spectrogram follows
    connect(m_chartPanel, &ChartPanel::xAxisRangeChanged,
            m_spectrogramEnhanced, &SpectrogramPanelEnhanced::onXAxisRangeChanged);

    // Spectrogram zoom → chart follows
    connect(m_spectrogramEnhanced, &SpectrogramPanelEnhanced::xAxisRangeChanged,
            this, [this](qreal xMin, qreal xMax) {
        m_chartPanel->setXAxisRange(xMin, xMax);   // P-31 T5：R3 穿透收口
    });

    // Spectrogram axis alignment with chart plot area
    connect(m_chartPanel, &ChartPanel::plotAreaUpdated,
            m_spectrogramEnhanced, &SpectrogramPanelEnhanced::onChartPlotAreaChanged);
}

/// @brief 分析域：AnalysisTaskService 任务信号/取消
void MainWindow::setupAnalysisConnections()
{
    // v1.8.0 P1a：引擎信号由 AnalysisTaskService 聚合（状态机 gating），
    // MainWindow 只接任务级中性信号（R1：UI 与引擎解耦）
    connect(m_taskService, &AnalysisTaskService::taskStarted,
            this, &MainWindow::onTaskStarted);
    connect(m_taskService, &AnalysisTaskService::taskProgress,
            this, &MainWindow::onTaskProgress);
    connect(m_taskService, &AnalysisTaskService::taskFinished,
            this, &MainWindow::onTaskFinished);
    connect(m_taskService, &AnalysisTaskService::taskFailed,
            this, &MainWindow::onTaskFailed);
    connect(m_taskService, &AnalysisTaskService::taskCancelled,
            this, &MainWindow::onTaskCancelled);

    // v0.3: Cancel button
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_taskService->cancel();   // v1.8.0 P1a：经状态机取消（迟到信号 gating）
    });
}

/// @brief 放大镜域：overlay 缩放/平移/右键菜单/事件过滤/帧转发/钉图
void MainWindow::setupMagnifierConnections()
{
    // Magnifier signals from video overlay
    auto *overlay = m_videoWidget->overlay();
    connect(overlay, &OverlayWidget::magnifierWheelZoom,
            this, &MainWindow::onMagnifierWheelZoom);
    connect(overlay, &OverlayWidget::magnifierPanRequested, this, [this](QPoint delta) {
        if (m_magnifier) {
            if (m_magnifier->isinvertPan())
                delta = -delta;
            QPoint newPos = m_magnifier->cursorPosition() + delta;
            m_magnifier->updateCursorPosition(newPos);
        }
    });

    // Right-click context menu on video overlay
    overlay->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(overlay, &QWidget::customContextMenuRequested,
            this, &MainWindow::showVideoContextMenu);

    // Install event filter for global shortcut handling
    overlay->installEventFilter(m_keyGuard);
    menuBar()->installEventFilter(m_keyGuard);
    this->installEventFilter(m_keyGuard);  // Global shortcut handling
    m_videoListPanel->listWidget()->installEventFilter(m_keyGuard);  // 视频列表快捷键

    // Forward video frames to magnifier and pinned
    // 审查 F-5：这里必须转发【已叠加微变】的原彩帧（而非引擎原始帧），否则
    // 放大镜/钉图/副屏全屏看不到微变彩色——即"微变局部放大"失效。
    // VideoWidget 已在 setVideoEngine 时先接到 frameReady（连接顺序在前），
    // 因此此处取到的 m_microDiffFrame 与本帧同步；applyMicroDiffTo 是纯渲染，
    // 不推进时域环缓冲，可被多个显示面共享。
    connect(m_videoEngine, &IVideoEngine::frameReady,
            this, [this](const QImage &img) {
                const QImage md = m_videoWidget ? m_videoWidget->applyMicroDiffTo(img) : img;
                if (m_magnifier)
                    m_magnifier->onFrameReady(md);
                updatePinnedImage(md);
                if (m_fsWindow)
                    m_fsWindow->setFrame(md);   // 副屏全屏：同源帧（隐式共享）
            });

    // Pinned timestamp
    connect(overlay, &OverlayWidget::pinnedRequested,
            this, [this](const QRect &videoRect) {
                m_pinnedRect = videoRect;
                if (!m_pinned) {
                    m_pinned = new PinnedWidget(this);
                    m_pinned->setAttribute(Qt::WA_DeleteOnClose);
                    connect(m_pinned, &QObject::destroyed, this, [this]() {
                        m_pinned = nullptr;
                        m_pinnedRect = QRect();
                    });
                    m_pinned->show();
                }
                // 钉图内容随主画面旋转（Q1 方案 A）+ 同一调节 LUT
                m_pinned->setDisplayRotation(m_videoWidget->displayRotation());
                if (m_adjustPanel)
                    m_pinned->setDisplayLut(m_adjustPanel->adjust().buildLut());
            });
}

/// @brief ROI 域：区域调整数据失效警告/删除清理/模式切换/复制粘贴
void MainWindow::setupRoiConnections()
{
    auto *overlay = m_videoWidget->overlay();

    // ROI adjustment warning: check for existing analysis data
    connect(overlay, &OverlayWidget::regionAdjustmentFinished,
            this, [this](int regionIndex, const QRect &originalRect, const QRect &newRect) {
                Q_UNUSED(newRect);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    int roiId = m_roiModel->roiIdAt(regionIndex);
                    if (roiId > 0 && snapshot.dataIndexOfRoiId(roiId, DataEntry::Rect) >= 0) {
                        auto reply = QMessageBox::question(this,
                            lang("数据失效警告", "Data Invalidation Warning"),
                            lang("调整该区域将导致亮度量化数据失效。\n确定要继续吗？",
                                 "Adjusting this region will invalidate the luminance analysis data.\nContinue?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Rect);
                        } else {
                            m_roiModel->updateRegion(regionIndex, originalRect);
                        }
                    }
                }
            });

    // Bug fix: When an ROI region is deleted, remove its corresponding analysis data
    // to prevent stale curves from appearing when a new ROI is drawn at the same index.
    connect(m_roiModel, &RoiModel::regionRemoved,
            this, [this](int index, int roiId) {
                Q_UNUSED(index);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Rect);
                }
            });

    // When a polygon ROI is deleted, remove its data by ROI ID
    connect(m_roiModel, &RoiModel::polygonRemoved,
            this, [this](int index, int roiId) {
                Q_UNUSED(index);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Polygon);
                }
            });

    // Polygon adjustment: show data invalidation warning
    connect(overlay, &OverlayWidget::polygonAdjustmentFinished,
            this, [this](int polygonIndex, const QPolygon &originalPolygon, const QPolygon &newPolygon) {
                Q_UNUSED(newPolygon);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    int roiId = m_roiModel->polygonRoiIdAt(polygonIndex);
                    int dataIdx = snapshot.dataIndexOfRoiId(roiId, DataEntry::Polygon);
                    if (dataIdx >= 0) {
                        auto reply = QMessageBox::question(this,
                            lang("数据失效警告", "Data Invalidation Warning"),
                            lang("调整该多边形将导致亮度量化数据失效。\n确定要继续吗？",
                                 "Adjusting this polygon will invalidate the luminance analysis data.\nContinue?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Polygon);
                        } else {
                            m_roiModel->updatePolygon(polygonIndex, originalPolygon);
                        }
                    }
                }
            });

    // v0.5: 多边形ROI和辅助线模式切换
    connect(m_rectModeBtn, &QPushButton::clicked, this, &MainWindow::onRectMode);
    connect(m_polygonModeBtn, &QPushButton::clicked, this, &MainWindow::onPolygonMode);
    connect(m_guideLineBtn, &QPushButton::clicked, this, &MainWindow::onGuideLineMode);

    // v0.5: 复制粘贴ROI
    connect(m_copyRoiBtn, &QPushButton::clicked, this, &MainWindow::onCopyRoi);
    connect(m_pasteRoiBtn, &QPushButton::clicked, this, &MainWindow::onPasteRoi);

    // v0.5: 模式变化信号
    connect(m_videoWidget->overlay(), &OverlayWidget::modeChanged, this, [this](const QString &mode) {
        showOperationStatus(mode);
    });

    // v0.5: 更新粘贴按钮状态
    connect(m_roiModel, &RoiModel::regionsChanged, this, [this]() {
        m_pasteRoiBtn->setEnabled(!m_roiClipboard.isEmpty() || !m_polygonClipboard.isEmpty());
    });
    connect(m_roiModel, &RoiModel::polygonsChanged, this, [this]() {
        m_pasteRoiBtn->setEnabled(!m_roiClipboard.isEmpty() || !m_polygonClipboard.isEmpty());
    });
}

/// @brief 截图融合域：截图叠加层/融合按钮/画面调节面板/放置同步
void MainWindow::setupSnapshotFusionConnections()
{
    // Snapshot overlay connections
    connect(m_snapshotOverlay, &SnapshotOverlay::captureRequested,
            this, [this]() {
                m_videoWidget->grabFrameSnapshot();
            });
    connect(m_snapshotOverlay, &SnapshotOverlay::clearRequested, this, [this]() {
        m_videoWidget->clearSnapshot();
        m_snapshotFusion = SnapshotFusionData();
        if (m_magnifier) {
            m_magnifier->clearSnapshotOverlay();
        }
    });

    // Fusion buttons
    connect(m_captureBtn, &QPushButton::clicked, this, [this]() {
        m_videoWidget->grabFrameSnapshot();
    });
    // 证据快照 + 画面调节面板（2026-08-14）
    connect(m_snapshotBtn, &QPushButton::clicked,
            this, &MainWindow::onSnapshotQuick);
    connect(m_adjustBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_adjustPanel) {
            // v1.13.1（用户反馈③）：独立浮窗呼出（与多机同步页同款），
            // 不挤占 dock 布局；关闭浮窗回写钮态由 visibilityChanged 负责
            if (on)
                m_adjustPanel->setFloating(true);
            m_adjustPanel->setVisible(on);
            if (on) {
                m_adjustPanel->resize(320, 460);
                const QPoint anchor = mapToGlobal(
                    QPoint(width() - 340, 90));
                m_adjustPanel->move(anchor);
            }
        }
    });
    if (m_adjustPanel) {
        connect(m_adjustPanel, &PlaybackAdjustPanel::adjustChanged, this,
                [this](const DisplayAdjust &adj) {
                    // VideoWidget 内部会从保留的原始帧重建显示帧，
                    // 暂停态拖滑杆同样实时预览；放大镜/钉图同一张 LUT。
                    const QByteArray lut = adj.buildLut();
                    m_videoWidget->setDisplayAdjust(adj);
                    if (m_magnifier)
                        m_magnifier->setDisplayAdjust(adj);
                    if (m_pinned)
                        m_pinned->setDisplayLut(lut);
                    if (m_fsWindow)
                        m_fsWindow->setDisplayAdjust(adj);   // 副屏共享调节
                });
        // 旋转档位（Q1 方案 A）：主画面 + 放大镜 + 钉图同步随转
        connect(m_adjustPanel, &PlaybackAdjustPanel::rotationChanged, this,
                [this](int degrees) {
                    m_videoWidget->setDisplayRotation(degrees);
                    if (m_magnifier)
                        m_magnifier->setDisplayRotation(degrees);
                    if (m_pinned)
                        m_pinned->setDisplayRotation(degrees);
                    showOperationStatus(degrees == 0
                        ? lang("显示旋转已复位", "Display rotation reset")
                        : lang("显示旋转 %1°（覆盖物随转，分析坐标不变）",
                               "Display rotation %1° (overlays follow; analysis coords unchanged)")
                                   .arg(degrees));
                });
        connect(m_adjustPanel, &QDockWidget::visibilityChanged, this,
                [this](bool vis) {
                    if (m_adjustBtn) {
                        QSignalBlocker blk(m_adjustBtn);
                        m_adjustBtn->setChecked(vis);
                    }
                });
    }
    connect(m_videoWidget, &VideoWidget::frameSnapshotReady, this, [this](const QImage &img) {
        m_snapshotOverlay->setSnapshot(img);
        m_editBtn->setEnabled(true);
        m_placeBtn->setEnabled(true);
        m_snapshotFusion.imageData = img;
    });
    connect(m_editBtn, &QPushButton::clicked, this, [this]() {
        if (m_snapshotOverlay->hasSnapshot()) {
            m_snapshotOverlay->setVisible(!m_snapshotOverlay->isVisible());
        }
    });
    connect(m_placeBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (!m_snapshotOverlay->hasSnapshot()) return;
        // Sync overlay's place state
        if (checked) {
            m_videoWidget->setSnapshot(m_snapshotOverlay->snapshotImage(),
                                       m_snapshotOverlay->brightness(),
                                       m_snapshotOverlay->contrastValue(),
                                       m_snapshotOverlay->opacityValue());
            if (m_magnifier) {
                m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                                m_snapshotOverlay->brightness(),
                                                m_snapshotOverlay->contrastValue(),
                                                m_snapshotOverlay->opacityValue());
            }
        } else {
            m_videoWidget->clearSnapshot();
            if (m_magnifier) {
                m_magnifier->clearSnapshotOverlay();
                m_magnifier->update();
            }
        }
    });
    connect(m_snapshotOverlay, &SnapshotOverlay::placeToggled, this, [this](bool active) {
        m_placeBtn->setChecked(active);
        // Sync snapshot fusion data
        m_snapshotFusion.brightness = m_snapshotOverlay->brightness();
        m_snapshotFusion.contrast = m_snapshotOverlay->contrastValue();
        m_snapshotFusion.opacity = m_snapshotOverlay->opacityValue();
        if (m_snapshotOverlay->hasSnapshot()) {
            m_snapshotFusion.imageData = m_snapshotOverlay->snapshotImage();
        }
        if (active) {
            m_videoWidget->setSnapshot(m_snapshotOverlay->snapshotImage(),
                                       m_snapshotOverlay->brightness(),
                                       m_snapshotOverlay->contrastValue(),
                                       m_snapshotOverlay->opacityValue());
            if (m_magnifier) {
                m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                                m_snapshotOverlay->brightness(),
                                                m_snapshotOverlay->contrastValue(),
                                                m_snapshotOverlay->opacityValue());
            }
        } else {
            m_videoWidget->clearSnapshot();
            if (m_magnifier) {
                m_magnifier->clearSnapshotOverlay();
            }
        }
    });
    connect(m_snapshotOverlay, &SnapshotOverlay::snapshotChanged, this, [this]() {
        // Sync snapshot fusion data (always, for VLA persistence)
        m_snapshotFusion.brightness = m_snapshotOverlay->brightness();
        m_snapshotFusion.contrast = m_snapshotOverlay->contrastValue();
        m_snapshotFusion.opacity = m_snapshotOverlay->opacityValue();
        if (m_snapshotOverlay->hasSnapshot()) {
            m_snapshotFusion.imageData = m_snapshotOverlay->snapshotImage();
        }

        // Only apply overlay when Place is active
        if (m_snapshotOverlay->isOverlayActive()) {
            m_videoWidget->setSnapshot(m_snapshotOverlay->snapshotImage(),
                                       m_snapshotOverlay->brightness(),
                                       m_snapshotOverlay->contrastValue(),
                                       m_snapshotOverlay->opacityValue());
            if (m_magnifier) {
                m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                                m_snapshotOverlay->brightness(),
                                                m_snapshotOverlay->contrastValue(),
                                                m_snapshotOverlay->opacityValue());
            }
        } else {
            m_videoWidget->clearSnapshot();
            if (m_magnifier) {
                m_magnifier->clearSnapshotOverlay();
            }
        }
    });
}

/// @brief 视频列表域：选中/清空（全量清场）/拖拽重排
void MainWindow::setupVideoListConnections()
{
    // v0.3: Video list panel connections
    connect(m_videoListPanel, &VideoListPanel::videoSelected,
            this, &MainWindow::onVideoSelected);
    // When the video list is cleared, unload everything: stop the engine,
    // clear the displayed frame and all analysis state, disable controls.
    connect(m_videoListPanel, &VideoListPanel::videoCountChanged,
            this, [this](int count) {
                if (count > 0)
                    return;
                m_videoEngine->unload();   // 彻底卸载：停线程+释放文件+duration 归零
                                          // （仅 stop() 时空格快捷键仍可继续播放——现场反馈）
                removeMagnifier();
                m_sessionMgr->setCurrentVideoPath(QString());
                m_uiState->beginVideo(0);

                m_roiModel->clearRegions();
                m_roiModel->clearPolygons();
                m_guideLineModel->clearLines();
                m_timelineModel->clearData();
                if (m_spectrogramEnhanced)
                    m_spectrogramEnhanced->clear();

                m_chartPanel->setLabels({});
                m_calibration = TimeCalibration();
                m_chartPanel->setCalibration(m_calibration);
                m_chartPanel->clearAB();
                m_pinnedRect = QRect();
                m_snapshotFusion = SnapshotFusionData();
                if (m_snapshotOverlay)
                    m_snapshotOverlay->clearSnapshot();
                if (m_videoWidget) {
                    m_videoWidget->clearSnapshot();
                    m_videoWidget->clearFrame();   // 清空 m_frameImage → 回到初始空状态
                }
                if (m_fsWindow) {
                    m_fsWindow->close();           // 清数据/关文件：副屏全屏一并关
                    m_fsWindow = nullptr;
                }
                // 竞态：worker 已 emit、UI 尚未处理的在途 frameReady 会在
                // clearFrame 之后到达又把帧画回去（现场反馈：第二次清空画面留存）。
                // unload 已停线程不会再产生新帧，事件循环尾部再清一次即可兜住。
                QTimer::singleShot(0, this, [this]() {
                    if (m_videoWidget && m_sessionMgr->currentVideoPath().isEmpty())
                        m_videoWidget->clearFrame();
                });

                m_playBtn->setEnabled(false);
                m_pauseBtn->setEnabled(false);
                m_stopBtn->setEnabled(false);
                m_speedBtn->setEnabled(false);
                m_analyzeBtn->setEnabled(false);
                m_audioAnalysisBtn->setEnabled(false);
                m_microDiffBtn->setEnabled(false);
                // 审查 F-1：清空列表同时清微变基准，防下一个视频与旧基准求差
                if (m_videoWidget)
                    m_videoWidget->clearMicroDiffBaseline();
                m_microDiffBaselineReady = false;
                m_microDiffBaseStartSec = 0.0;
                m_microDiffBaseDurSec = 0.0;
                if (m_microDiffDialog)
                    m_microDiffDialog->setBaselineReady(false);
                m_setTimeBtn->setEnabled(false);
                m_captureBtn->setEnabled(false);
                if (m_snapshotBtn)
                    m_snapshotBtn->setEnabled(false);
                if (m_adjustPanel) {   // 清空列表：调节回默认，面板状态保留
                    m_adjustPanel->setValues(DisplayAdjust(), 0);
                    m_videoWidget->setDisplayAdjust(DisplayAdjust());
                    m_videoWidget->setDisplayRotation(0);
                    if (m_magnifier)
                        m_magnifier->setDisplayAdjust(DisplayAdjust());
                    if (m_pinned) {
                        m_pinned->setDisplayLut(QByteArray());
                        m_pinned->setDisplayRotation(0);
                    }
                }

                setWindowTitle(windowTitleWithCase(
                    lang(QStringLiteral("追光者 Lumen Arc v") + QString(APP_VERSION), QStringLiteral("Lumen Arc v") + QString(APP_VERSION))));
                updateTimeDisplay();
                showOperationStatus(lang("已清空视频列表", "Video list cleared"));
            });
    // B5: Respond to drag-reorder changes (recalculate playback offsets, etc.)
    connect(m_videoListPanel, &VideoListPanel::videoReordered,
            this, [this]() {
                // Invalidate cached timeline offsets; next playback/analysis
                // will pick up the new order from allVideos().
            });
}

/// @brief 案件域：开案/关案时合成导出入口使能（v1.16.2 P1.5 解绑 A/B 后）
void MainWindow::setupCaseConnections()
{
    // v1.16.2 P1.5：合成导出工作台不再要求 A/B（素材树+I/O 打点自含）——
    // 使能改由视频加载/案件打开驱动（见两处加载收口与 caseOpened）
    connect(m_caseManager, &CaseManager::caseOpened, this, [this]() {
        if (m_exportClipBtn)
            m_exportClipBtn->setEnabled(true);
    });
    connect(m_caseManager, &CaseManager::caseClosed, this, [this]() {
        if (m_exportClipBtn && m_sessionMgr->currentVideoPath().isEmpty())
            m_exportClipBtn->setEnabled(false);
    });
}