/**
 * @file mainwindow_export.cpp
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

// ---------------------------------------------------------------------------
// P-68 导出选段视频（分段变速 + 图表/语谱同框复合导出）
// ---------------------------------------------------------------------------
void MainWindow::onExportSegmentClip()
{
    // 合成导出工作台（2026-09-03 拍板 v2：独立窗口，素材树+可播放预览+片段块时间线）
    const double fps = (m_videoEngine && m_videoEngine->fps() > 0.0f)
                           ? double(m_videoEngine->fps()) : 25.0;
    // 主窗正在播则先暂停（与多机窗同款资源互斥）
    if (m_videoEngine && m_videoEngine->state() == PlaybackState::Playing)
        m_videoEngine->pause();

    if (!m_workbench) {
        m_workbench = new ComposeWorkbenchWindow(m_caseManager, m_currentVideoPath,
                                                 fps, this);
        connect(m_workbench, &ComposeWorkbenchWindow::exportRequested, this,
                [this](const SegmentExportEngine::Params &pp) {
                    startComposeExport(pp);
                });
        connect(m_workbench, &ComposeWorkbenchWindow::cancelRequested, this, [this]() {
            if (m_segmentExporter)
                m_segmentExporter->cancel();
        });
    } else {
        m_workbench->refreshContext(m_currentVideoPath, fps);
    }
    m_workbench->show();
    m_workbench->raise();
    m_workbench->activateWindow();
}


/// 合成导出入口（v1.16.2）：双模式分发——证据直拷 / 多段合成走新管线；
/// 单段+当前视频+原速+图表面板勾选 → 旧复合导出路径（曲线/语谱/放大镜全保真）
void MainWindow::startComposeExport(const SegmentExportEngine::Params &ppIn)
{
    if (!m_workbench)
        return;
    if (m_segmentExporter && m_segmentExporter->isRunning()) {
        m_workbench->setResult(false, lang("已有导出进行中", "Export already running"));
        return;
    }
    if (ppIn.segments.size() == 1 && !ppIn.evidenceCopy
        && ppIn.segments.first().sourcePath == m_currentVideoPath
        && qAbs(ppIn.segments.first().rate - 1.0) < 0.01
        && ppIn.segments.first().annos.isEmpty()   // 有标注 → 新管线烧录
        && m_workbench->wantPanels()) {
        // 旧复合路径（图表/语谱/放大镜/标签 OSD 全套，v1.15.3 行为冻结）
        QVector<qint64> labelTimes;
        for (const ChartLabel &lb : m_chartPanel->labels())
            labelTimes.append(lb.timeMs);
        const speedplan::SpeedPlan plan = speedplan::planFromLabels(
            ppIn.segments.first().inMs, ppIn.segments.first().outMs, labelTimes);
        startSegmentExport(plan, ppIn.burnOsd, ppIn.outputPath);
        return;
    }
    if (!m_segmentExporter) {
        m_segmentExporter = new SegmentExportEngine(this);
        // 连接只在创建时建一次（v1.15.3 卡死修同款教训）
        connect(m_segmentExporter, &SegmentExportEngine::progress, m_workbench,
                &ComposeWorkbenchWindow::setProgress);
        connect(m_segmentExporter, &SegmentExportEngine::finished, this,
                [this](bool ok, const QString &msg) {
                    if (m_workbench)
                        m_workbench->setResult(ok, msg);
                    if (ok)
                        showOperationStatus(lang("合成导出完成", "Compose export done"));
                });
    }
    const bool evidence = ppIn.evidenceCopy;
    qint64 total = 0;
    if (!evidence)
        for (const auto &s : ppIn.segments)
            total += SegmentExportEngine::composeSegOutFrames(s, ppIn.outFps);
    m_workbench->setExportRunning(true, evidence ? 100 : int(qMax<qint64>(1, total)));
    m_segmentExporter->start(ppIn);
}


/// P-68：面板「开始导出」→ 采集底图 + 引擎启动（进度回报回面板）
void MainWindow::startSegmentExport(const speedplan::SpeedPlan &planIn,
                                    bool burnOsd, const QString &outPath)
{
    if (m_segmentExporter && m_segmentExporter->isRunning()) {
        m_workbench->setResult(false, lang("已有导出进行中", "Export already running"));
        return;
    }
    speedplan::SpeedPlan plan = planIn;
    plan.normalize();
    m_speedPlan = plan;          // 拍板 Q5：方案随 .vla 持久化
    saveCurrentVlaAsync();

    // 底图光栅化：X 轴定区间 [A,B]（语谱经 xAxisRangeChanged 联动跟随），
    // 2x 超采样保清晰度；渲染后恢复视口。无数据面板 = 空图（拍板 Q3 隐藏）。
    QImage chartBase, specBase;
    const AnalysisSnapshot snap = m_timelineModel->snapshot();
    QValueAxis *ax = m_chartPanel->axisX();
    const qreal oldMin = ax ? ax->min() : 0, oldMax = ax ? ax->max() : 0;
    m_chartPanel->setXAxisRange(plan.aMs, plan.bMs);
    if (!snap.timestamps.isEmpty())
        chartBase = m_chartPanel->renderToImage(QSize(3840, 420));
    if (snap.audioData().hasSpectrogram())
        specBase = m_spectrogramEnhanced->renderHeatmapImage(QSize(3840, 400));
    if (ax)
        m_chartPanel->setXAxisRange(oldMin, oldMax);

    SegmentExportEngine::Params pp;
    pp.sourcePath = m_currentVideoPath;
    pp.outputPath = outPath;
    pp.plan = plan;
    pp.outFps = (m_videoEngine && m_videoEngine->fps() > 0.0f)
                    ? double(m_videoEngine->fps()) : 25.0;
    pp.chartBase = chartBase;
    pp.specBase = specBase;
    pp.burnOsd = burnOsd;
    pp.caseLabel = (m_caseManager && m_caseManager->isOpen())
                       ? m_caseManager->meta().caseNo : QString();
    pp.calibration = m_calibration;
    // 图表标签入导出（曲线条竖标 + OSD 5 秒烧录，真机反馈拍板）
    if (m_chartPanel)
        pp.labels = m_chartPanel->labels();
    // 放大镜开着且有取景 → 导出画面右下角 PIP 嵌入放大视图（真机反馈）
    if (m_magnifier && !m_magnifier->currentSourceRect().isEmpty()) {
        pp.magnifierPip = true;
        pp.magnifierSrcRect = m_magnifier->currentSourceRect();
        pp.magnifierRotation = m_magnifier->displayRotation();
        pp.magnifierZoom = m_magnifier->zoomLevel();
    }

    if (!m_segmentExporter) {
        m_segmentExporter = new SegmentExportEngine(this);
        // v1.15.3 卡死修：连接只在创建时建一次——旧代码每次导出都 connect，
        // Qt::UniqueConnection 对 lambda 无效（每次新地址），finished 槽逐次
        // 堆叠：第一次完成弹 1 窗、第二次 2 窗……模态窗堵死界面=“不能再导出”。
        connect(m_segmentExporter, &SegmentExportEngine::progress, m_workbench,
                &ComposeWorkbenchWindow::setProgress);
        connect(m_segmentExporter, &SegmentExportEngine::finished, this,
                [this](bool ok, const QString &msg) {
                    if (m_workbench)
                        m_workbench->setResult(ok, msg);
                    if (ok)
                        showOperationStatus(lang("选段视频导出完成", "Clip exported"));
                    // v1.15.3 用户拍板：不要完成弹窗（QMessageBox 在该机器上
                    // 无法关闭、拦截全应用输入，见档案 docs/INVESTIGATION_EXPORT_
                    // FROZEN_20260825.md）——完成提示走面板结果条 + 📂 打开所在
                    // 文件夹按钮 + 状态栏提示，已足够。
                });
    }
    SegmentExportEngine *eng = m_segmentExporter;
    m_workbench->setExportRunning(true, int(plan.outputFrameCount(pp.outFps)));
    eng->start(pp);
}


