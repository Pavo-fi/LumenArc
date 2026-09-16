/**
 * @file mainwindow_magnifier.cpp
 * @brief v1.17.0 P-79：自 mainwindow.cpp 按职责域拆出（行为冻结纯移动，定义仍属 MainWindow）
 */
#include "mainwindow.h"
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

/// 构建时间戳（2026-08-13）：标题栏常驻，杜绝“用户在跑旧构建”无法辨识

/// @brief 创建放大镜：设置视频尺寸/区域模型/同步截图叠加
void MainWindow::createMagnifier()
{
    if (m_magnifier)
        return;

    m_magnifier = new MagnifierWidget(this);

    int vw = m_videoEngine ? m_videoEngine->videoWidth() : 1920;
    int vh = m_videoEngine ? m_videoEngine->videoHeight() : 1080;
    m_magnifier->setVideoSize(vw, vh);
    m_magnifier->setRegionModel(m_roiModel);
    m_magnifier->setPolygonModel(m_roiModel);
    m_magnifier->setGuideLineModel(m_guideLineModel);

    // 旋转档位 + 画面调节同步（Q1 方案 A：放大视图随主画面一起转/调）
    m_magnifier->setDisplayRotation(m_videoWidget->displayRotation());
    if (m_adjustPanel)
        m_magnifier->setDisplayAdjust(m_adjustPanel->adjust());

    // 放大镜来源标识框（§14 Q1）：源区域变化实时同步到主画面 overlay；
    // 初始值立即下发（setVideoSize 的信号早于 connect）
    connect(m_magnifier, &MagnifierWidget::sourceRectChanged,
            this, [this](const QRect &storedRect, qreal zoom) {
                m_videoWidget->overlay()->setMagnifierRect(storedRect, zoom);
            });
    m_videoWidget->overlay()->setMagnifierRect(m_magnifier->currentSourceRect(),
                                               m_magnifier->zoomLevel());

    // Sync current ROI mode to magnifier overlay
    if (m_magnifier->overlay()) {
        m_magnifier->overlay()->setPolygonMode(m_videoWidget->overlay()->isPolygonMode());
        m_magnifier->overlay()->setGuideLineMode(m_videoWidget->overlay()->isGuideLineMode());
    }

    m_topRow->addWidget(m_magnifier);   // 内嵌顶行右半（v1.13.1）
    m_topRow->setStretchFactor(1, 1);
    m_magnifier->show();
    // 左右等大并列：首次均分；用户拖过分割条则以关放大镜时保存的比例恢复
    if (m_topRowSavedSizes.size() == 2 && m_topRowSavedSizes[0] > 0
        && m_topRowSavedSizes[1] > 0) {
        m_topRow->setSizes(m_topRowSavedSizes);
    } else {
        const int w = m_topRow->width();
        if (w > 0)
            m_topRow->setSizes({w / 2, w - w / 2});
    }

    // Auto-layout: 收起左侧列表（案件模式收案件面板，否则收视频列表，
    // v1.13.1 用户反馈①：案件列表此前不折叠）并显示占位细条
    m_videoListWasExpanded = m_videoListContent->isVisible();
    const bool caseMode = m_caseDock && m_caseDock->isVisible();
    if (caseMode) {
        // v1.13.1：记录真实手动状态（用户本已收起的，关闭放大镜后仍保持收起）
        m_caseDockWasExpanded = m_caseListContent
            ? m_caseListContent->isVisible() : true;
        m_caseDock->setVisible(false);
        m_casePlaceholder->setVisible(true);
        resizeDocks({m_casePlaceholder}, {24}, Qt::Horizontal);
    } else if (m_videoListWasExpanded || m_videoListPanel->isVisible()) {
        m_videoListPanel->setVisible(false);
        m_videoListPlaceholder->setVisible(true);
        resizeDocks({m_videoListPlaceholder}, {24}, Qt::Horizontal);
    }

    // Sync snapshot overlay if currently active
    if (m_snapshotOverlay && m_snapshotOverlay->hasSnapshot() && m_snapshotOverlay->isOverlayActive()) {
        m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                         m_snapshotOverlay->brightness(),
                                         m_snapshotOverlay->contrastValue(),
                                         m_snapshotOverlay->opacityValue());
    }

    // Connect magnifier overlay's regionAdjustmentFinished for data invalidation warning
    connect(m_magnifier->overlay(), &OverlayWidget::regionAdjustmentFinished,
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

    // Connect magnifier overlay's polygonAdjustmentFinished
    connect(m_magnifier->overlay(), &OverlayWidget::polygonAdjustmentFinished,
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

    // Forward the current frame so the magnifier shows content immediately
    // even if the video is paused or has ended.
    // 注意必须给【原始帧】：放大镜内部按原视频系坐标裁剪（旋转由 ContentWidget
    // 在显示前应用）；currentFrame() 是已旋转+LUT 的显示帧，裁剪几何会错。
    if (!m_videoWidget->rawFrame().isNull()) {
        m_magnifier->onFrameReady(m_videoWidget->applyMicroDiffTo(m_videoWidget->rawFrame()));
    }
}

void MainWindow::removeMagnifier()
{
    if (!m_magnifier)
        return;

    // 恢复案件面板（v1.13.1：与视频列表同语义——占位条还亮着说明用户
    // 没主动展开过，按进入前状态恢复）
    if (m_casePlaceholder && m_casePlaceholder->isVisible()) {
        m_casePlaceholder->setVisible(false);
        if (m_caseDock) {
            m_caseDock->setVisible(true);
            if (m_caseDockWasExpanded) {
                if (m_caseListContent)
                    m_caseListContent->setVisible(true);
                if (m_caseCollapseBtn)
                    m_caseCollapseBtn->setText(QString::fromUtf8("◀"));
                resizeDocks({m_caseDock}, {250}, Qt::Horizontal);
            } else {
                // 进入放大镜前用户本就手动收起——恢复为 24px 细条
                resizeDocks({m_caseDock}, {24}, Qt::Horizontal);
            }
        }
    }
    // Restore video list panel state
    if (m_videoListPlaceholder->isVisible()) {
        // User didn't expand video list while magnifier was open
        m_videoListPlaceholder->setVisible(false);
        if (m_videoListWasExpanded) {
            m_videoListPanel->setVisible(true);
            m_videoListContent->setVisible(true);
            m_videoListCollapseBtn->setText(QString::fromUtf8("\xe2\x97\x80")); // ◀
            m_videoListCollapseBtn->setToolTip(lang("收起视频列表", "Collapse video list"));
            resizeDocks({m_videoListPanel}, {250}, Qt::Horizontal);
        }
    }
    // else: user expanded video list while magnifier was open, keep it as is

    m_videoWidget->overlay()->setMagnifierRect(QRect(), 0.0);   // 标识框随放大镜关闭消失
    m_topRowSavedSizes = m_topRow->sizes();   // 保存用户调过的左右比例
    m_magnifier->clearSnapshotOverlay();
    // 立即摘出分割条（deleteLater 要等事件循环才重排，右侧会留半行空白）
    m_magnifier->setParent(nullptr);
    m_magnifier->close();
    m_magnifier->deleteLater();
    m_magnifier = nullptr;
    // 单孩 QSplitter 不自动拉伸剩余子项（offscreen 实测），显式吃满整行
    m_topRow->setSizes({m_topRow->width()});
}

void MainWindow::onMagnifierWheelZoom(int delta, QPoint videoPos)
{
    if (!m_magnifier)
        createMagnifier();

    if (m_magnifier) {
        m_magnifier->zoomAtPoint(delta, videoPos);
    }
}

void MainWindow::showVideoContextMenu(const QPoint &pos)
{
    QMenu menu;

    if (m_chartPanel->isABRegionSet()) {
        QAction *loopAction = menu.addAction(m_chartPanel->isABLoop()
            ? lang("关闭循环", "Disable Loop")
            : lang("开启循环", "Enable Loop"));
        QAction *clearABAction = menu.addAction(lang("清除 A/B 区域", "Clear A/B Region"));
        QAction *chosen = menu.exec(m_videoWidget->overlay()->mapToGlobal(pos));
        if (chosen == loopAction) {
            m_chartPanel->setABLoop(!m_chartPanel->isABLoop());
        } else if (chosen == clearABAction) {
            m_chartPanel->clearAB();
        }
    } else {
        if (m_magnifier) {
            menu.addAction(lang("关闭放大镜", "Close Magnifier"));
            QAction *invertAction = menu.addAction(lang("反向平移", "Invert Pan"));
            invertAction->setCheckable(true);
            invertAction->setChecked(m_magnifier->isinvertPan());
            QAction *chosen = menu.exec(m_videoWidget->overlay()->mapToGlobal(pos));
            if (m_magnifier && chosen && chosen->text() == lang("关闭放大镜", "Close Magnifier")) {
                removeMagnifier();
            } else if (chosen == invertAction) {
                m_magnifier->setInvertPan(invertAction->isChecked());
            }
        } else {
            menu.exec(m_videoWidget->overlay()->mapToGlobal(pos));
        }
    }
}

void MainWindow::updatePinnedImage(const QImage &frame)
{
    if (!m_pinned || m_pinnedRect.isEmpty() || frame.isNull())
        return;
    // 同步源视频原生分辨率（帧坐标换算基准；videoSizeChanged 仅在 load 时发出一次，
    // 而 PinnedWidget 跨视频存活，故每帧同步，赋值零开销）
    if (m_videoEngine)
        m_pinned->setVideoSize(m_videoEngine->videoWidth(), m_videoEngine->videoHeight());
    m_pinned->setPinnedImage(frame, m_pinnedRect);
}


