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
#include <QPainter>
#include <QProgressDialog>
#include <QStackedWidget>
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

/// v3.5：图表区域离屏重渲染 + 把悬浮浮块（徽标/读数/图例）一并画进去。
/// 为什么需要：renderToImage() 只渲染 QChart 本体，而图例、游标读数、
/// 「● 亮度·音量」徽标都是叠在图表容器上的 **独立 widget**（不是图表的一部分），
/// 所以旧快照的图表区域一直是「缺胳膊少腿」的旧样子。
QImage MainWindow::renderSnapshotChart(const QSize &target, int *leftInset,
                                       int *rightInset)
{
    if (!m_chartPanel)
        return QImage();
    QRectF plotArea;
    QImage img = m_chartPanel->renderToImage(target, &plotArea);
    // v3.7：把绘图区的左右内缩量交出去——语谱图靠它对齐，
    // 否则两图各按各的轴宽排布，水平长度永远差几个像素（用户：逼死强迫症）
    if (leftInset)
        *leftInset = plotArea.isValid() ? qRound(plotArea.left()) : -1;
    if (rightInset)
        *rightInset = plotArea.isValid()
            ? qRound(qreal(target.width()) - plotArea.right()) : -1;
    if (img.isNull() || !m_chartContainer)
        return img;
    QWidget *host = m_chartContainer;
    if (host->width() <= 0 || host->height() <= 0)
        return img;
    // 画面重渲染后字号不变，浮块也按原尺寸画；只有位置按比例映射。
    const qreal sx = qreal(target.width()) / qreal(host->width());
    const qreal sy = qreal(target.height()) / qreal(host->height());
    const QStringList chipNames{QStringLiteral("trackBadge"),
                                QStringLiteral("chartReadoutChip"),
                                QStringLiteral("chartLegendChip")};
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const QString &name : chipNames) {
        auto *chip = host->findChild<QWidget *>(name);
        if (!chip || !chip->isVisible() || chip->width() <= 0)
            continue;
        const QPoint inHost = chip->mapTo(host, QPoint(0, 0));
        p.save();
        p.translate(qreal(inHost.x()) * sx, qreal(inHost.y()) * sy);
        chip->render(&p, QPoint(0, 0), QRegion(), QWidget::DrawChildren);
        p.restore();
    }
    p.end();
    return img;
}

namespace {
/// 快照落盘目录与文件名主干（案件优先，其次源旁 snapshots/）
} // namespace

void MainWindow::onSnapshotQuick()
{
    // v3.5（用户反馈）：宫格在屏时，S 必须拍**宫格**，不能还去拍被暂停的
    // 主视口那一帧（用户看到的是「快照还是原来页面的」）。
    if (m_workspaceStack && m_multiCamWin
        && m_workspaceStack->currentWidget() == m_multiCamWin.data()) {
        QWidget *grid = m_multiCamWin->findChild<QWidget *>(
            QStringLiteral("multicamGridHost"));
        QWidget *src = grid && grid->width() > 40 ? grid : m_multiCamWin.data();
        const QPixmap pm = src->grab();
        if (pm.isNull()) {
            showOperationStatus(lang("宫格无画面，无法快照",
                                     "Grid has no picture to snapshot"));
            return;
        }
        const QString base = QFileInfo(m_sessionMgr->currentVideoPath())
                                 .completeBaseName();
        QString dir;
        const bool inCase = m_caseManager && m_caseManager->isOpen();
        if (inCase)
            dir = m_caseManager->caseDir() + QStringLiteral("/snapshots");
        else
            dir = QDir::homePath() + QStringLiteral("/LumenArc_Snapshots");
        if (!QDir().mkpath(dir)) {
            showOperationStatus(lang("快照目录创建失败：%1",
                                     "Cannot create snapshot dir: %1").arg(dir));
            return;
        }
        const QString stamp = QDateTime::currentDateTime()
                                  .toString(QStringLiteral("yyyyMMdd_HHmmss"));
        const QString stem = (base.isEmpty() ? QStringLiteral("grid") : base)
                             + QStringLiteral("_") + stamp
                             + QStringLiteral("_grid");
        QString path = dir + QLatin1Char('/') + stem + QStringLiteral(".png");
        for (int i = 1; QFile::exists(path) && i < 1000; ++i)
            path = dir + QLatin1Char('/') + stem
                   + QStringLiteral("_%1.png").arg(i);
        if (!pm.save(path, "PNG")) {
            showOperationStatus(lang("快照保存失败：%1",
                                     "Snapshot save failed: %1").arg(path));
            return;
        }
        if (inCase && m_caseDock)
            m_caseDock->refreshTree();
        showOperationStatus(lang("宫格快照已保存：%1",
                                 "Grid snapshot saved: %1").arg(path));
        return;
    }

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
    // v3.6：分析区宽度必须跟"最终画布"一致。放大镜分屏时画布 = 左视频 + 间隔
    // + 右放大图，比视频宽得多；旧代码按视频宽度渲染，导致图表只占左半、
    // 右半全黑（用户实测截图）。
    const int contentW = qMax(frame.width(),
                              SnapshotComposer::contentWidth(in));
    int chartLeft = -1, chartRight = -1;
    if (!snap.isEmpty() || snap.hasAudio())
        in.chartImg = renderSnapshotChart(QSize(contentW, 420),
                                          &chartLeft, &chartRight);
    // 语谱图用图表绘图区的同一对左右内缩量 → 热力图起止像素与曲线严格对齐
    if (snap.hasAudio() && m_spectrogramEnhanced)
        in.specImg = m_spectrogramEnhanced->renderHeatmapImage(
            QSize(contentW, 300), chartLeft, chartRight);

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
