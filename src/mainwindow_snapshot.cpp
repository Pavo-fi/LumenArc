/**
 * @file mainwindow_snapshot.cpp
 * @brief v1.17.0 P-79：自 mainwindow.cpp 按职责域拆出（行为冻结纯移动，定义仍属 MainWindow）
 */
#include "mainwindow.h"
#include "videowidget.h"
#include "frame_annotation.h"
#include "chartpanel.h"
#include "domain/roi_model.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/ivideo_engine.h"
#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/ffmpeg_video_engine.h"
#include "app/calibration_service.h"
#include "app/snapshot_composer.h"
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

void MainWindow::onSnapshotQuick()
{
    const QImage frame = m_videoWidget->currentFrame();   // 已含画面调节+旋转（所见即所得）
    if (frame.isNull()) {
        showOperationStatus(lang("当前无画面，无法快照", "No frame to snapshot"));
        return;
    }
    const int rotation = m_videoWidget->displayRotation();
    const qint64 posMs = m_videoEngine ? m_videoEngine->position() : 0;

    // ---- 当前帧标签（±1s 内最近者；ChartLabel 存流内时间）----
    QString labelText;
    {
        qint64 best = LLONG_MAX;
        for (const auto &l : m_chartPanel->labels()) {
            const qint64 d = qAbs(l.timeMs - posMs);
            if (d < best) { best = d; labelText = l.text; }
        }
        if (best > 1000)
            labelText.clear();
    }

    // ---- v1.17.0 P-80：渲染输入装配（取数/离屏渲染留 UI 层，合成走纯模块）----
    SnapshotInputs in;
    in.frame = frame;
    in.rotation = rotation;
    in.videoSize = m_videoWidget->overlay()->videoSize();   // 原视频原生尺寸
    in.regions = m_roiModel;
    in.guideLines = m_guideLineModel;
    in.hasMagnifier = m_magnifier && m_magnifier->currentSourceRect().isValid();
    if (in.hasMagnifier) {
        in.magnifierSourceRect = m_magnifier->currentSourceRect();
        in.magnifierZoom = m_magnifier->zoomLevel();
        in.magnifiedImage = m_magnifier->currentMagnifiedImage();
    }
    in.posMs = posMs;
    in.calibration = m_calibration;
    in.labelText = labelText;
    in.fileName = QFileInfo(m_sessionMgr->currentVideoPath()).fileName();
    // 分析数据区（§14 v2：离屏重渲染，widget 渲染留在 UI 层）
    const AnalysisSnapshot snap = m_timelineModel->snapshot();
    if (!snap.isEmpty() || snap.hasAudio())
        in.chartImg = m_chartPanel->renderToImage(QSize(frame.width(), 420));
    if (snap.hasAudio() && m_spectrogramEnhanced)
        in.specImg = m_spectrogramEnhanced->renderHeatmapImage(
            QSize(frame.width(), 300));

    const QImage out = SnapshotComposer::compose(in);
    if (out.isNull()) {
        showOperationStatus(lang("当前无画面，无法快照", "No frame to snapshot"));
        return;
    }

    // ---- 保存：案件 snapshots/ 优先；无案件则视频同目录 snapshots/ ----
    const QString fileTimeTag =
        SnapshotComposer::timeCode(m_calibration, posMs).second;
    const QString base = QFileInfo(m_sessionMgr->currentVideoPath()).completeBaseName();
    QString dir;
    const bool inCase = m_caseManager && m_caseManager->isOpen();
    if (inCase)
        dir = m_caseManager->caseDir() + QStringLiteral("/snapshots");
    else if (!m_sessionMgr->currentVideoPath().isEmpty())
        dir = QFileInfo(m_sessionMgr->currentVideoPath()).absolutePath()
              + QStringLiteral("/snapshots");
    else
        dir = QDir::homePath() + QStringLiteral("/LumenArc_Snapshots");
    if (!QDir().mkpath(dir)) {
        showOperationStatus(lang("快照目录创建失败：%1", "Cannot create snapshot dir: %1")
                                .arg(dir));
        return;
    }
    QString path;
    for (int i = 0; ; ++i) {
        const QString name = i == 0
            ? base + QStringLiteral("_") + fileTimeTag + QStringLiteral(".png")
            : base + QStringLiteral("_") + fileTimeTag
              + QStringLiteral("_%1.png").arg(i + 1);
        path = dir + QLatin1Char('/') + name;
        if (!QFile::exists(path))
            break;
    }
    if (!out.save(path, "PNG")) {
        showOperationStatus(lang("快照保存失败：%1", "Snapshot save failed: %1")
                                .arg(path));
        return;
    }
    if (inCase && m_caseDock)
        m_caseDock->refreshTree();   // 案件快照组即时可见（dock 扫描目录驱动）
    showOperationStatus(lang("快照已保存：%1", "Snapshot saved: %1").arg(path)
        + (rotation != 0
            ? lang("（显示旋转 %1°）", " (rotated %1°)").arg(rotation)
            : QString()));
}
