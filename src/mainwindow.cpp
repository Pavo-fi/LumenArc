/**
 * @file mainwindow.cpp
 * @brief 主窗口实现：菜单/工具栏/快捷键/分析流程/截图叠加/放大镜协调
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "mainwindow.h"
#include "keyguardfilter.h"
#include "build_stamp.h"
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

void MainWindow::openPreprocessWindow()
{
    auto *w = new PreprocessWindow(m_analysisEngine, this);
    // v1.3.0 M2 任务8：案件模式注入（有打开案件时成果默认导入案件）
    w->setCaseManager(m_caseManager);
    connect(w, &PreprocessWindow::openOutputRequested,
            this, &MainWindow::openVideoFile);
    w->show();
    w->raise();
    w->activateWindow();
}

/**
 * @brief 5级Python检测：内嵌→环境变量→注册表→常见路径→py.exe
 */
qint64 MainWindow::trustedDurationFor(const QString &path) const
{
    // R2: engine-neutral interface, no downcast (preprocess design §3.4)
    return m_analysisEngine ? m_analysisEngine->trustedDurationMs(path) : 0;
}

void MainWindow::onOpenFile()
{
    // Ctrl+O：案件打开时自动入案（拍板§8-5）
    openVideosInteractive(true);
}

void MainWindow::onOpenFileTemporary()
{
    // 「临时打开(不入案)」：跳过入案登记，其余流程一致
    openVideosInteractive(false);
}

void MainWindow::openVideosInteractive(bool admitToCase)
{
    QStringList filePaths = QFileDialog::getOpenFileNames(this,
        lang("打开视频", "Open Video Files"),
        QString(),
        lang("视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;所有文件 (*)",
             "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All Files (*)"));

    if (filePaths.isEmpty())
        return;

    // v0.3: Add all files to video list, open first one for playback
    for (int i = 0; i < filePaths.size(); ++i) {
        const QString &path = filePaths[i];
        // Get video info (fps + total frames -> duration) via interface (R2)
        IAnalysisEngine::VideoTiming timing;
        if (m_analysisEngine)
            timing = m_analysisEngine->videoTiming(path);
        float fps = timing.fps;
        qint64 durationMs = timing.durationMs;
        if (fps <= 0) fps = 30.0f;
        m_videoListPanel->addVideo(path, durationMs, fps);

        // v1.3.0 M2 任务7：先于 openVideoFile 入案，使源旁 .vla 导入后
        // 缓存探测直接命中案件 .vla（入案视频不弹询问）
        if (admitToCase)
            admitVideoToCase(path, true);

        if (i == 0) {
            openVideoFile(path);
            // 引擎回退：getVideoInfo 失败时改用引擎时长
            if (durationMs <= 0) {
                qint64 engineDur = m_videoEngine->duration();
                if (engineDur > 0) {
                    m_videoListPanel->updateDuration(path, engineDur);
                }
            }
        }
    }
}

/// @brief 打开视频文件：加载/缓存检测/按钮启用
/// @brief 视频入案登记（v1.3.0 M2 任务7）
void MainWindow::admitVideoToCase(const QString &path, bool interactive)
{
    if (!m_caseManager || !m_caseManager->isOpen() || path.isEmpty())
        return;
    if (path.endsWith(".vla", Qt::CaseInsensitive))
        return;   // .vla 分析文件不是视频，不入案
    if (m_caseManager->isCaseVideo(path))
        return;   // 已在案：照常打开即可，不重复登记

    QString err;
    const QString id = m_caseManager->addVideo(path, &err);
    if (id.isEmpty()) {
        // 重复路径拒绝（拍板§8-5）：非模态提示，不打断打开流程
        if (!err.isEmpty())
            showOperationStatus(err);
        return;
    }
    showOperationStatus(lang("已入案登记：%1", "Registered into case: %1").arg(id));

    // 同内容仅提示（拍板§8-5）：与案内他路大小完全一致 → 可能同一来源
    if (const auto *self = m_caseManager->videoById(id)) {
        for (const auto &v : m_caseManager->meta().videos) {
            if (v.id != id && v.sizeBytes > 0 && v.sizeBytes == self->sizeBytes) {
                showOperationStatus(
                    lang("提示：%1 与 %2 文件大小相同，请确认是否同一来源",
                         "Note: %1 and %2 have identical size; same source?")
                        .arg(id, v.id));
                break;
            }
        }
    }

    // 源旁已有 .vla 询问导入（默认是，复制；拍板§8-5）
    const QString sideVla = path + QStringLiteral(".vla");
    const QString caseVla = m_caseManager->vlaPathFor(path);
    if (QFile::exists(sideVla) && !QFile::exists(caseVla)) {
        bool importIt = true;
        if (interactive) {
            const auto reply = QMessageBox::question(this,
                lang("导入已有分析结果", "Import Existing Analysis"),
                lang("源视频旁存在已保存的分析结果：\n%1\n\n是否复制导入案件（%2）？",
                     "A saved analysis exists beside the source:\n%1\n\n"
                     "Copy it into the case (%2)?").arg(sideVla, id),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            importIt = (reply == QMessageBox::Yes);
        }
        if (importIt) {
            QDir().mkpath(QFileInfo(caseVla).absolutePath());
            if (QFile::copy(sideVla, caseVla))
                showOperationStatus(
                    lang("已导入分析结果到 %1", "Imported analysis into %1").arg(id));
            else
                showOperationStatus(
                    lang("分析结果复制失败", "Failed to copy the analysis file"));
        }
    }

    // 登记即落盘（取证：崩溃不丢登记；case.json 体量小，保存开销可忽略）
    QString saveErr;
    if (!m_caseManager->saveCase(&saveErr))
        showOperationStatus(
            lang("案件保存失败：%1", "Failed to save case: %1").arg(saveErr));
}









void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    // v1.13.1：首次有效尺寸落地纵向初始比例（视频55/图表21/语谱15，
    // 对齐 v1.7.0 观感拍板）；只执行一次，之后用户拖分割条/窗口缩放自理
    if (!m_initialSplitApplied && m_splitter && m_splitter->height() > 200) {
        m_initialSplitApplied = true;
        const int h = m_splitter->height();
        m_splitter->setSizes({int(h * 0.55), int(h * 0.21),
                              int(h * 0.15)});
    }
    if (m_caseOpenPanel && m_caseOpenPanel->isVisible())
        centerCaseOpenPanel();
}









qint64 MainWindow::abPointA() const
{
    return m_chartPanel ? m_chartPanel->abPointA() : -1;
}

qint64 MainWindow::abPointB() const
{
    return m_chartPanel ? m_chartPanel->abPointB() : -1;
}

void MainWindow::applyRestoredState(const VideoState &st)
{
    // v1.17.0 D2：自 openVideoFile hasMemoryState 分支逐字抽取（行为冻结）
        // 带 roiId 恢复：保持与分析数据 dataEntries 的 roi_id 对齐
    if (st.regionRoiIds.size() == st.regions.size())
        m_roiModel->restoreRegions(st.regions, st.regionRoiIds);
    else {
        m_roiModel->clearRegions();
        for (const QRect &rc : st.regions)
            m_roiModel->addRegion(rc);
    }

    if (st.polygonRoiIds.size() == st.polygons.size())
        m_roiModel->restorePolygons(st.polygons, st.polygonRoiIds);
    else {
        m_roiModel->clearPolygons();
        for (const QPolygon &poly : st.polygons)
            m_roiModel->addPolygon(poly);
    }

    m_guideLineModel->clearLines();
    for (const GuideLine &line : st.guideLines)
        m_guideLineModel->addLine(line);

    m_timelineModel->setData(
        QVector<qint64>(st.snapshot.timestamps),
        QVector<QVector<qreal>>(st.snapshot.lumRows()),
        QVector<DataEntry>(st.snapshot.lumEntries()),
        st.snapshot.audioData()
    );

    m_calibration = st.calibration;
    m_chartPanel->setCalibration(m_calibration);
    // 校时徽标以 .vla 为 SSOT：空校时模型（旧 v7 迁移 offset=0）同步
    // 熄灭案件里误亮的 ⏰（用户实测反馈）
    m_caseManager->updateCalibrationBadge(
        m_sessionMgr->currentVideoPath(), m_calibration.isEffective(),
        ProjectIO::calibrationBadgeSummary(m_calibration));
    m_chartPanel->setLabels(st.labels);
    m_chartPanel->setChartGuideLinesData(st.chartGuideLines);

    // Restore A/B region
    if (st.abPointA >= 0) m_chartPanel->setPointA(st.abPointA);
    if (st.abPointB >= 0) m_chartPanel->setPointB(st.abPointB);
    if (st.abLoop) m_chartPanel->setABLoop(true);

    if (!st.pinnedRect.isEmpty())
        m_pinnedRect = st.pinnedRect;

    m_snapshotFusion = st.snapshotFusion;
    if (st.snapshotFusion.isValid() && !st.snapshotFusion.imageData.isNull()) {
        m_snapshotOverlay->setSnapshot(st.snapshotFusion.imageData);
        m_snapshotOverlay->setParameters(
            st.snapshotFusion.brightness,
            st.snapshotFusion.contrast,
            st.snapshotFusion.opacity);
        m_editBtn->setEnabled(true);
        m_placeBtn->setEnabled(true);
    }

    // 恢复播放画面调节（逐视频记忆：亮度/对比度/伽马/色阶/反色/旋转）
    if (m_adjustPanel) {
        m_adjustPanel->setValues(st.display,
                                 st.displayRotation);
        const QByteArray lut = st.display.buildLut();
        m_videoWidget->setDisplayAdjust(st.display);
        m_videoWidget->setDisplayRotation(st.displayRotation);
        if (m_magnifier) {
            m_magnifier->setDisplayAdjust(st.display);
            m_magnifier->setDisplayRotation(st.displayRotation);
        }
        if (m_pinned) {
            m_pinned->setDisplayLut(lut);
            m_pinned->setDisplayRotation(st.displayRotation);
        }
    }

    if (m_spectrogramEnhanced && st.snapshot.hasAudio())
        m_spectrogramEnhanced->setSpectrogramData(st.snapshot.audioData());


}

void MainWindow::resetForNewVideo()
{
    // v1.17.0 D2：自 openVideoFile 无状态分支逐字抽取（行为冻结）
m_roiModel->clearRegions();
m_roiModel->clearPolygons();
m_guideLineModel->clearLines();
m_timelineModel->clearData();
if (m_spectrogramEnhanced)
    m_spectrogramEnhanced->clear();

// Also reset chart-level state so labels/time axis/A-B/pinned/fusion
// from the previous video do not leak into this one.
m_chartPanel->setLabels({});
m_chartPanel->clearChartGuideLines();
m_calibration = TimeCalibration();
m_chartPanel->setCalibration(m_calibration);
m_chartPanel->clearAB();
m_pinnedRect = QRect();
m_snapshotFusion = SnapshotFusionData();
// 无状态视频：画面调节回默认（防跨视频泄漏）
if (m_adjustPanel) {
    m_adjustPanel->setValues(DisplayAdjust(), 0);
    m_videoWidget->setDisplayAdjust(DisplayAdjust());
    m_videoWidget->setDisplayRotation(0);
    if (m_magnifier)
        m_magnifier->setDisplayAdjust(DisplayAdjust());
    if (m_pinned)
        m_pinned->setDisplayLut(QByteArray());
}
if (m_snapshotOverlay)
    m_snapshotOverlay->clearSnapshot();
if (m_videoWidget)
    m_videoWidget->clearSnapshot();


}

void MainWindow::enableVideoActions()
{
    // v1.17.0 D2：两分支共用的按钮启用块（逐字抽取，消除重复）
    m_playBtn->setEnabled(true);
    m_pauseBtn->setEnabled(true);
    m_stopBtn->setEnabled(true);
    m_speedBtn->setEnabled(true);
    m_analyzeBtn->setEnabled(true);
    m_audioAnalysisBtn->setEnabled(true);
    m_setTimeBtn->setEnabled(true);
    m_captureBtn->setEnabled(true);
    if (m_snapshotBtn)
        m_snapshotBtn->setEnabled(true);
    if (m_exportClipBtn)
        m_exportClipBtn->setEnabled(true);
}

void MainWindow::openVideoFile(const QString &filePath)
{



    if (filePath.isEmpty())
        return;

    if (filePath.endsWith(".dav", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, lang("不支持的格式", "Unsupported Format"),
            lang("DAV 格式不受本工具支持。\n"
                 "请将视频转换为 MP4、AVI 或 MKV 格式。",
                 "DAV format is not supported by this tool.\n"
                 "Please convert the video to MP4, AVI, or MKV format."));
        return;
    }

    // If it's a .vla analysis result file, load it directly（P-31 T2-A/T1：装载归 ProjectIO，应用块去重）
    if (filePath.endsWith(".vla", Qt::CaseInsensitive)) {
        ProjectIO::LoadedVla loaded;
        if (m_projectIo->loadVla(filePath, &loaded)) {
        applyAnalysisArtifacts(loaded);

        // Do NOT overwrite m_sessionMgr->currentVideoPath() with the .vla path: it is an
        // analysis file, not a playable video, and it keys VideoStateManager.
        setWindowTitle(windowTitleWithCase(QStringLiteral("Lumen Arc v") + QString(APP_VERSION) + QStringLiteral(" - [Loaded: ") +
                           QFileInfo(filePath).fileName() + "]"));
        } else {
            QMessageBox::critical(this, lang("错误", "Error"),
                lang("加载分析结果文件失败：\n",
                     "Failed to load analysis result file:\n") + filePath);
        }
        return;
    }

    // Save current video state before switching（P-31 T2-A：装配归会话管理器）
    if (!m_sessionMgr->currentVideoPath().isEmpty()) {
        VideoState cur;
        cur.snapshot = m_timelineModel->snapshot();
        cur.regions = m_roiModel->regions();
        cur.regionRoiIds = m_roiModel->roiIds();
        cur.polygons = m_roiModel->polygons();
        cur.polygonRoiIds = m_roiModel->polygonRoiIds();
        cur.guideLines = m_guideLineModel->lines();
        cur.chartGuideLines = m_chartPanel->chartGuideLinesData();
        cur.calibration = m_calibration;
        cur.magnifierRect = m_magnifier ? m_magnifier->currentSourceRect() : QRect();
        cur.labels = m_chartPanel->labels();
        cur.pinnedRect = m_pinnedRect;
        cur.snapshotFusion = m_snapshotFusion;
        cur.abPointA = m_chartPanel->abPointA();
        cur.abPointB = m_chartPanel->abPointB();
        cur.abLoop = m_chartPanel->isABLoop();
        cur.display = m_adjustPanel ? m_adjustPanel->adjust() : DisplayAdjust();
        cur.displayRotation = m_adjustPanel ? m_adjustPanel->rotation() : 0;
        m_sessionMgr->saveCurrentState(m_sessionMgr->currentVideoPath(), cur);
    }

    removeMagnifier();
    m_sessionMgr->setCurrentVideoPath(filePath);
    m_uiState->beginVideo(trustedDurationFor(filePath));   // 等待 durationChanged 校准
    // 案件现场跟踪（v1.3.0 M2：开案恢复 lastVideoId 的数据源）
    if (const auto *cv = m_caseManager->videoByPath(filePath))
        m_caseManager->setLastVideoId(cv->id);

    if (m_videoEngine->load(filePath)) {
        // P-31 T2-A：打开决策数据面（内存现场 + 缓存路径/入案判定）
        const VideoSessionManager::OpenPlan openPlan =
            m_sessionMgr->planOpen(filePath, m_caseManager);
        // 同步源视频原生分辨率（时间戳框选归一化基准，v1.2.1）
        m_videoWidget->overlay()->setVideoSize(
            m_videoEngine->videoWidth(), m_videoEngine->videoHeight());
        // 大视频加载需要数秒：主窗口显示“导入中…”（首帧到达自动清除），
        // 避免用户误以为程序无响应
        m_videoWidget->setLoading(true);
        QObject::connect(m_videoEngine, &IVideoEngine::frameReady, this,
                         [this](const QImage &) { m_videoWidget->setLoading(false); },
                         Qt::SingleShotConnection);
        // 加入视频列表（“在主窗口播放输出”路径此前漏加；hasVideo 去重避免
        // 每次切换视频重复跑 videoTiming）
        if (m_videoListPanel && !m_videoListPanel->hasVideo(filePath)) {
            IAnalysisEngine::VideoTiming timing;
            if (m_analysisEngine)
                timing = m_analysisEngine->videoTiming(filePath);
            float fps = timing.fps > 0 ? timing.fps : m_videoEngine->fps();
            qint64 dur = timing.durationMs;
            if (dur <= 0)
                dur = m_videoEngine->duration();
            m_videoListPanel->addVideo(filePath, dur, fps);
        }
        // Check if we have a saved state for this video (memory state takes priority)
        if (openPlan.hasMemoryState) {
            // v1.17.0 D2：恢复扇出抽为 applyRestoredState（逐字搬移，顺序冻结）
            applyRestoredState(openPlan.memoryState);
            enableVideoActions();
            // v1.7.1：案件树高亮正在播放的文件
            if (m_caseDock)
                m_caseDock->setCurrentVideoPath(m_sessionMgr->currentVideoPath());
            onPlay();
            return;
        }

        // No saved state, clear data and check for .vla cache
        // v1.17.0 D2：清空扇出抽为 resetForNewVideo（逐字搬移，顺序冻结）
        resetForNewVideo();
        enableVideoActions();


        // Check for cached .vla file alongside the video（P-31 T2-A：路径/入案判定来自 planOpen）
        if (!openPlan.cacheVlaPath.isEmpty()) {
            // 入案视频：案件内 .vla 即权威缓存，直接加载不弹询问（拍板§3-6）
            bool loadCache = openPlan.cacheIsCaseVideo;
            if (!loadCache) {
            auto reply = QMessageBox::question(this,
                lang("找到缓存的分析结果", "Cached Analysis Found"),
                lang("找到此视频已保存的分析结果。\n"
                     "是否直接加载而无需重新分析？\n\n",
                     "A saved analysis result was found for this video.\n"
                     "Would you like to load it instead of re-analyzing?\n\n") + openPlan.cacheVlaPath,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            loadCache = (reply == QMessageBox::Yes);
            }
            if (loadCache) {
                // P-31 T1：装载归 ProjectIO，应用块去重（applyAnalysisArtifacts）
                ProjectIO::LoadedVla loaded;
                if (m_projectIo->loadVla(openPlan.cacheVlaPath, &loaded))
                    applyAnalysisArtifacts(loaded);
                // v1.7.1：案件内 .vla 自动加载不弹框（拍板§3-6），但补一个
                // 非打扰状态栏提示——用户反馈“默认加载分析结果，不再弹出”
                showOperationStatus(lang("已自动加载分析结果",
                                         "Analysis result auto-loaded"));
            }
        }

        // v1.2.0 sidecar 继承：拼接输出的校时自动带入（仅无现有校时时，Q-4）
        // v1.12.0：分段校时——各段按画面时间锚定，缺口处墙钟精确跳变
        if (!m_calibration.isValid()) {
            TimeCalibration inherited;
            QString sidecarWarning;
            if (CalibrationService::loadSidecar(filePath, &inherited,
                                                &sidecarWarning)
                && inherited.isValid()) {
                m_calibration = inherited;
                m_chartPanel->setCalibration(m_calibration);
                // v1.7.1 修复：继承的校时必须落盘 .vla 并刷新案件 ⏰ 徽标
                // （用户实测：前处理产物继承校时后切换/重开丢失、无徽标——
                // 旧流程只在内存，产物无分析数据时从未写 vla）
                saveCurrentVlaAsync();
                if (sidecarWarning.isEmpty()) {
                    showOperationStatus(
                        lang("已继承前处理校时",
                             "Inherited calibration from preprocessing"));
                } else {
                    // sidecarWarning 类型化前缀（C1）："gaps:<数量>:<最大|缺口|ms>"
                    QString gapCount;
                    const QStringList wp = sidecarWarning.split(QLatin1Char(':'));
                    if (wp.size() >= 3 && wp[0] == QLatin1String("gaps"))
                        gapCount = wp[1];
                    QMessageBox::information(this,
                        lang("拼接时间缺口提示", "Time Gap Notice"),
                        lang("已继承前处理校时（分段模式：每段按画面时间锚定）。\n"
                             "此拼接文件含 %1 处时间缺口（监控常态），缺口处墙钟跳变（将写入报告）。",
                             "Calibration inherited (piecewise: each segment anchored\n"
                             "by its on-screen time). This merged file has %1 time gap(s);\n"
                             "wall clock jumps at gaps (will be noted in reports).")
                            .arg(gapCount));
                }
            }
        }

        // v1.7.1：案件树高亮正在播放的文件（无缓存/继承路径同待遇）
        if (m_caseDock)
            m_caseDock->setCurrentVideoPath(m_sessionMgr->currentVideoPath());
        onPlay();
    } else {
        QMessageBox::critical(this, lang("错误", "Error"),
            lang("打开视频文件失败：\n", "Failed to open video file:\n") + filePath);
    }
}

void MainWindow::onSaveAnalysis()
{
    AnalysisSnapshot snapshot = m_timelineModel->snapshot();
    if (snapshot.isEmpty()) {
        QMessageBox::information(this, lang("保存", "Save"),
            lang("没有分析数据可保存。", "No analysis data to save."));
        return;
    }

    // P-31 T1：路径分流与写出归 ProjectIO（行为冻结）
    QString defaultPath = m_projectIo->suggestSavePath(m_sessionMgr->currentVideoPath());
    QString filePath = QFileDialog::getSaveFileName(this,
        lang("保存分析结果", "Save Analysis Result"), defaultPath,
        lang("VLA 文件 (*.vla)", "VLA Files (*.vla)"));
    if (filePath.isEmpty())
        return;

    if (m_projectIo->saveVlaNow(filePath, collectVlaSaveRequest())) {
        // VLA2：频谱已内嵌于文件中，无需 .spec 伴随文件
        // 存入案件管理路径时同步刷新校时徽标缓存（.vla 为 SSOT）
        if (!m_sessionMgr->currentVideoPath().isEmpty()
            && QFileInfo(filePath).absoluteFilePath()
                   == QFileInfo(m_caseManager->vlaPathFor(m_sessionMgr->currentVideoPath())).absoluteFilePath()) {
            m_caseManager->updateCalibrationBadge(
                m_sessionMgr->currentVideoPath(), m_calibration.isEffective(),
                ProjectIO::calibrationBadgeSummary(m_calibration));
        }
        QMessageBox::information(this, lang("保存", "Save"),
            lang("分析结果保存成功。", "Analysis result saved successfully."));
        showOperationStatus(lang("保存成功", "Saved"));
    } else {
        QMessageBox::critical(this, lang("错误", "Error"),
            lang("保存分析结果失败。", "Failed to save analysis result."));
    }
}

void MainWindow::onLoadAnalysis()
{
    // v1.3.0 路径分流：入案视频从案件 videos/ 目录起始浏览
    const QString startDir = m_sessionMgr->currentVideoPath().isEmpty()
        ? QString()
        : QFileInfo(m_caseManager->vlaPathFor(m_sessionMgr->currentVideoPath())).absolutePath();
    QString filePath = QFileDialog::getOpenFileName(this,
        "Load Analysis Result", startDir,
        "VLA Files (*.vla);;All Files (*)");
    if (filePath.isEmpty())
        return;

        QVector<QRect> regions;
        QVector<QPolygon> loadedPolygons;
        QVector<GuideLine> loadedGuideLines;
        QVector<int> loadedRegionRoiIds;
        QVector<int> loadedPolygonRoiIds;
        TimeCalibration calibration;
        QRect magnifierRect;
        QVector<ChartLabel> labels;
        QRect pinnedRect;
        SnapshotFusionData snapshotFusion;
        if (m_timelineModel->loadFromFile(filePath, &regions, &calibration,
                                            &magnifierRect, &labels, &pinnedRect,
                                            &snapshotFusion, &loadedPolygons, &loadedGuideLines,
                                            &loadedRegionRoiIds, &loadedPolygonRoiIds)) {
            restoreAnalysisState(regions, calibration, labels, pinnedRect, snapshotFusion, loadedRegionRoiIds);
            if (loadedPolygonRoiIds.size() == loadedPolygons.size())
                m_roiModel->restorePolygons(loadedPolygons, loadedPolygonRoiIds);
            else {
                m_roiModel->clearPolygons();
                for (const QPolygon &poly : loadedPolygons)
                    m_roiModel->addPolygon(poly);
            }
            m_guideLineModel->clearLines();
            for (const GuideLine &line : loadedGuideLines)
                m_guideLineModel->addLine(line);

            // Do NOT overwrite m_sessionMgr->currentVideoPath() with the .vla path (see openVideoFile).
        setWindowTitle(windowTitleWithCase(QStringLiteral("Lumen Arc v") + QString(APP_VERSION) + QStringLiteral(" - [Loaded: ") +
                       QFileInfo(filePath).fileName() + "]"));
        QMessageBox::information(this, lang("已加载", "Loaded"),
            lang("分析结果加载成功。", "Analysis result loaded successfully."));
        showOperationStatus(lang("加载成功", "Loaded"));
    } else {
        QMessageBox::critical(this, lang("错误", "Error"),
            lang("加载分析结果文件失败：\n",
                 "Failed to load analysis result file:\n") + filePath);
    }
}

/// @brief 从外部文件加载图片作为叠加层
void MainWindow::onLoadOverlayImage()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        lang("加载图片为叠加", "Load Image as Overlay"), QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.tiff *.tif);;All Files (*)");
    if (filePath.isEmpty())
        return;

    QImage img(filePath);
    if (img.isNull()) {
        QMessageBox::warning(this,
            lang("加载失败", "Load Failed"),
            lang("无法加载该图片文件。\n请确认文件格式是否支持。",
                 "Failed to load the image file.\nPlease check if the format is supported."));
        return;
    }

    m_snapshotOverlay->setSnapshot(img);
    m_editBtn->setEnabled(true);
    m_placeBtn->setEnabled(true);
    m_snapshotFusion.imageData = img;
}

// ---------------------------------------------------------------------------
// 时间戳区域持久化（v1.2.1：按视频路径 hash，同一摄像头复用）
// ---------------------------------------------------------------------------




/// v1.7.1：后台保存当前视频 .vla + 同步案件校时徽标。
/// 分析完成自动保存与校时采用后共用（用户实测：校时后徽标不出现——
/// 旧流程只在保存 .vla 时刷新徽标，校时采用未触发保存）。
/// v1.7.1：后台保存当前视频 .vla + 同步案件校时徽标。
/// v1.9.0 P-31 T1：写出与参数组装归 ProjectIO（行为冻结）。
void MainWindow::saveCurrentVlaAsync()
{
    if (m_sessionMgr->currentVideoPath().isEmpty()
        || m_sessionMgr->currentVideoPath().endsWith(".vla", Qt::CaseInsensitive))
        return;
    m_projectIo->saveVlaAsync(m_caseManager->vlaPathFor(m_sessionMgr->currentVideoPath()),
                              collectVlaSaveRequest());
    // 同步刷新案件校时徽标缓存（.vla 为 SSOT；案件模式空指针安全）
    m_caseManager->updateCalibrationBadge(
        m_sessionMgr->currentVideoPath(), m_calibration.isEffective(),
        ProjectIO::calibrationBadgeSummary(m_calibration));
}

/// UI 侧收集 .vla 保存参数（值拷贝，后台线程安全）
ProjectIO::VlaSaveRequest MainWindow::collectVlaSaveRequest()
{
    ProjectIO::VlaSaveRequest req;
    req.regions = m_roiModel->regions();
    req.calibration = m_calibration;
    req.magnifierRect = m_magnifier ? m_magnifier->currentSourceRect() : QRect();
    req.labels = m_chartPanel->labels();
    req.pinnedRect = m_pinnedRect;
    req.fusion = m_snapshotFusion;
    req.polygons = m_roiModel->polygons();
    req.guideLines = m_guideLineModel->lines();
    req.regionRoiIds = m_roiModel->roiIds();
    req.polygonRoiIds = m_roiModel->polygonRoiIds();
    req.abRegion.a = m_chartPanel->abPointA();      // P-68 拍板 Q5：入 .vla
    req.abRegion.b = m_chartPanel->abPointB();
    req.abRegion.loop = m_chartPanel->isABLoop();
    req.speedPlan = m_speedPlan;
    return req;
}






void MainWindow::onSetStartTime()
{
    // v1.2.1 非模态校时窗口：重建/识别在后台进行，主窗口可继续操作；
    // 关闭窗口不取消任务，重开可见进度与结果（Q-3 候选语义不变）。
    if (m_sessionMgr->currentVideoPath().isEmpty())
        return;
    // 已开窗口：置顶返回（此前无 return 照样新建 → 双窗共存，框选确认只
    // 推进最后打开的窗口，先开的永远卡在「框选完了不抵达下一步」）
    if (m_calibrationDialog) {
        m_calibrationDialog->raise();
        m_calibrationDialog->activateWindow();
        return;
    }
    const qint64 curPos = m_videoEngine ? m_videoEngine->position() : 0;
    QString sidecarWarning;
    if (m_calibration.source == TimeCalibration::Source::Inherited)
        CalibrationService::loadSidecar(m_sessionMgr->currentVideoPath(), nullptr,
                                        &sidecarWarning);
    auto *dlg = new TimeSettingsDialog(
        m_sessionMgr->currentVideoPath(), curPos,
        m_uiState->effectiveDurationMs(),
        m_calibration, sidecarWarning, m_calibrationService, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_calibrationDialog = dlg;
    // 恢复已保存的时间戳区域（同一摄像头自动复用）
    const QRectF savedRoi = m_projectIo->savedTimestampRoi(m_sessionMgr->currentVideoPath());
    if (savedRoi.isValid())
        dlg->setTimestampRoi(savedRoi);
    // 应用校时（与旧模态路径等价：应用后更新图表与状态栏）
    // v1.2.1 时间戳框选：dialog 请求 → 主窗口视频框选 → 回传 + 持久化
    connect(dlg, &TimeSettingsDialog::requestTimestampRoi,
            this, [this, dlg]() {
                // 优先用已保存的区域（同一摄像头复用）；无则给右上角默认框
                QRectF saved = m_projectIo->savedTimestampRoi(m_sessionMgr->currentVideoPath());
                m_videoWidget->beginTimestampRoiSelection(saved);
                m_roiDialog = dlg;
            });
    connect(dlg, &TimeSettingsDialog::cancelTimestampRoiRequest,
            this, [this]() {
                m_videoWidget->endTimestampRoiSelection();
                m_roiDialog = nullptr;
    });
    // 窗口关闭时退出框选模式
    connect(dlg, &QDialog::destroyed, this, [this]() {
        if (m_roiDialog)
            m_roiDialog = nullptr;
        m_videoWidget->endTimestampRoiSelection();
        m_calibrationDialog = nullptr;
    });
    connect(dlg, &TimeSettingsDialog::goTaskFinished,
            this, &MainWindow::showTrayNotification);
    connect(dlg, &TimeSettingsDialog::calibrationApplied,
            this, [this](const TimeCalibration &cal) {
                m_calibration = cal;
                m_chartPanel->setCalibration(m_calibration);
                QString msg;
                if (!cal.isValid()) {
                    msg = lang("校时已清除", "Calibration cleared");
                } else if (cal.dateKnown) {
                    msg = lang("校时已应用：流内 0 点 = %1",
                               "Calibration applied: stream 0 = %1")
                              .arg(QDateTime::fromMSecsSinceEpoch(cal.offsetMs)
                                       .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
                    if (cal.rateApplied)
                        msg += lang("；漂移 %1 秒/天", "; drift %1 s/day")
                                   .arg(cal.driftSecondsPerDay(), 0, 'f', 1);
                    if (cal.truthSet)
                        msg += lang("；北京时间偏移 %1s", "; Beijing offset %1s")
                                   .arg(cal.truthOffsetMs / 1000.0, 0, 'f', 1);
                    if (cal.piecewiseMode())
                        msg += lang("；分段重建 %1 段（变速）", "; piecewise %1 segs")
                                   .arg(cal.piecewise.size());
                } else {
                    msg = lang("时间偏移已应用：%1s", "Time offset applied: %1s")
                              .arg(cal.offsetMs / 1000.0, 0, 'f', 1);
                }
                showOperationStatus(msg);
                // v1.13.1 对时图片留档（拍板：存档）：校时图片复制入案件
                // calibration/ 目录（best effort；未入案件则保留原路径引用）
                if (cal.truthSet
                    && cal.truthSource == QLatin1String("photo")
                    && !cal.truthImagePath.isEmpty()
                    && m_caseManager && !m_caseManager->caseDir().isEmpty()) {
                    const QString dstDir = m_caseManager->caseDir()
                        + QStringLiteral("/calibration");
                    QDir().mkpath(dstDir);
                    const QString dst = dstDir + QStringLiteral("/")
                        + QFileInfo(cal.truthImagePath).fileName();
                    if (!QFile::exists(dst)
                        && !QFile::copy(cal.truthImagePath, dst))
                        showOperationStatus(lang(
                            "校时图片复制入案件失败（原图仍在：%1）",
                            "Photo archive copy failed (original: %1)")
                            .arg(cal.truthImagePath));
                }
                // v1.7.1：校时采用后即落盘 .vla 并刷新案件徽标（用户实测：
                // 校时完成后 ⏰ 不出现——旧流程只更新内存，徽标要等保存）
                saveCurrentVlaAsync();
                if (m_caseDock)
                    m_caseDock->refreshTree();
            });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls())
        return;

    QList<QUrl> urls = mimeData->urls();
    if (urls.isEmpty())
        return;

    // v0.3: Support multi-file drop - add all to video list
    bool first = true;
    for (const QUrl &url : urls) {
        QString filePath = url.toLocalFile();
        if (filePath.isEmpty()) continue;

        // Get video metadata (fps + total frames -> duration)
        float fps = 30.0f;
        qint64 durationMs = 0;
        const auto timing = m_analysisEngine->videoTiming(filePath);   // R2：接口调用
        fps = (timing.fps > 0) ? timing.fps : 30.0f;
        durationMs = timing.durationMs;
        if (fps <= 0) fps = 30.0f;

        m_videoListPanel->addVideo(filePath, durationMs, fps);

        // v1.3.0 M2 任务7：拖入即入案（有打开案件时；拍板§8-5）
        admitVideoToCase(filePath, true);

        if (first) {
            openVideoFile(filePath);
            // 引擎回退：getVideoInfo 失败时改用引擎时长
            if (durationMs <= 0) {
                qint64 engineDur = m_videoEngine->duration();
                if (engineDur > 0) {
                    m_videoListPanel->updateDuration(filePath, engineDur);
                }
            }
            first = false;
        }
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::onPlay()
{
    m_videoEngine->play();
    showOperationStatus(lang("播放", "Playing"));
}

void MainWindow::onPause()
{
    m_videoEngine->pause();
    showOperationStatus(lang("暂停", "Paused"));
}

void MainWindow::onStop()
{
    m_videoEngine->stop();
    m_playbackSettings->setSpeed(1.0f);
    m_speedBtn->setText("1x");
    m_videoEngine->setRate(1.0f);
    updatePlaybackButtons();
    showOperationStatus(lang("停止", "Stopped"));
}

/// @brief v0.3: 启动多视频连续播放
/// @brief 按步进调整播放速度：0.25x/0.5x/1x/2x/4x/8x
void MainWindow::adjustSpeed(float delta)
{
    static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    static const int count = 6;

    // Find current speed index
    int idx = 0;
    for (int i = 0; i < count; ++i) {
        if (qAbs(m_playbackSettings->speed() - speeds[i]) < 0.01f) {
            idx = i;
            break;
        }
    }

    // Adjust index
    if (delta > 0)
        idx = qMin(idx + 1, count - 1);
    else
        idx = qMax(idx - 1, 0);

    applySpeed(speeds[idx]);
}

/// @brief 循环切换播放倍速：0.25x/0.5x/1x/2x/4x/8x，到达最高速后回到最低速
void MainWindow::cycleSpeed()
{
    static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    static const int count = 6;

    // Find current speed index
    int idx = 0;
    for (int i = 0; i < count; ++i) {
        if (qAbs(m_playbackSettings->speed() - speeds[i]) < 0.01f) {
            idx = i;
            break;
        }
    }

    // Cycle to next speed, wrapping around to the start
    idx = (idx + 1) % count;
    applySpeed(speeds[idx]);
}

/// @brief 统一应用播放速度并更新 UI 和状态提示
void MainWindow::applySpeed(float speed)
{
    m_playbackSettings->setSpeed(speed);

    // Format display text: integers show "2x", decimals show "0.5x"
    QString speedText;
    if (m_playbackSettings->speed() == static_cast<int>(m_playbackSettings->speed()))
        speedText = QString("%1x").arg(static_cast<int>(m_playbackSettings->speed()));
    else
        speedText = QString("%1x").arg(m_playbackSettings->speed(), 0, 'f', 2).replace(".00", "");

    m_speedBtn->setText(speedText);
    m_videoEngine->setRate(m_playbackSettings->speed());
    QString speedStatus = QString(lang("倍速 %1", "Speed %1")).arg(speedText);
    if (qAbs(m_playbackSettings->speed() - 1.0f) > 0.01f && !m_videoEngine->supportsRateAudio())
        speedStatus += lang("（音频已静音）", " (audio muted)");
    showOperationStatus(speedStatus);
}

void MainWindow::updatePlaybackButtons()
{
    bool playing = (m_videoEngine->state() == PlaybackState::Playing);
    bool hasMedia = (m_uiState->effectiveDurationMs() > 0) || m_videoEngine->duration() > 0;
    m_playBtn->setEnabled(!playing && hasMedia);
    m_pauseBtn->setEnabled(playing);
    m_stopBtn->setEnabled(playing || m_videoEngine->state() == PlaybackState::Paused);
}





/// @brief 启动离线分析：前置检查→状态栏进度→Python进程
void MainWindow::onAnalyze()
{
    if (m_sessionMgr->currentVideoPath().isEmpty()) {
        QMessageBox::information(this, lang("亮度分析", "Luminance Analysis"),
            lang("请先打开一个视频文件。", "Please open a video file first."));
        return;
    }


    QVector<QRect> regions = m_roiModel->regions();
    QVector<QPolygon> polygons = m_roiModel->polygons();
    if (regions.isEmpty() && polygons.isEmpty()) {
        QMessageBox::information(this, lang("亮度分析", "Luminance Analysis"),
            lang("请先在视频上绘制至少一个 ROI 区域。",
                 "Please draw at least one ROI on the video."));
        return;
    }

    // Collect ROI IDs for data tracking
    QVector<int> rectRoiIds, polygonRoiIds;
    for (int i = 0; i < m_roiModel->regionCount(); ++i)
        rectRoiIds.append(m_roiModel->roiIdAt(i));
    for (int i = 0; i < m_roiModel->polygonCount(); ++i)
        polygonRoiIds.append(m_roiModel->polygonRoiIdAt(i));

    // v1.8.0 P1a：状态机接管（前置校验/忙检查在服务内，失败经 taskFailed 弹窗）
    m_taskService->start(AnalysisChannels::luminance(), m_sessionMgr->currentVideoPath(),
                         regions, polygons, rectRoiIds, polygonRoiIds);
}


/// @brief 启动音频分析（独立于亮度分析，无需 ROI）
void MainWindow::applyPlaybackDenoiseSetting()
{
    QSettings s("LumenArc", "LumenArc");
    const bool on = s.value("playbackDenoise", false).toBool();
    if (m_videoEngine)
        m_videoEngine->setPlaybackDenoise(on, m_playbackSettings->noiseReductionStrength());
    if (m_multiCamWin)
        m_multiCamWin->applyPlaybackDenoise(on, m_playbackSettings->noiseReductionStrength());
}

void MainWindow::onAudioAnalysis()
{
    if (m_sessionMgr->currentVideoPath().isEmpty()) {
        QMessageBox::information(this, lang("音频分析", "Audio Analysis"),
            lang("请先打开一个视频文件。", "Please open a video file first."));
        return;
    }


    // v1.8.0 P1a：音频无前置条件（无 ROI 要求），状态机接管
    // P-54：降噪强度随任务下发（0=干净分析；滑杆调回 0 再应用即复原）
    if (m_analysisEngine)
        m_analysisEngine->setAudioDenoiseStrength(m_playbackSettings->noiseReductionStrength());
    applyPlaybackDenoiseSetting();   // 滑杆可能变了：播放 DSP 同步强度
    m_taskService->start(AnalysisChannels::audio(), m_sessionMgr->currentVideoPath(),
                         {}, {}, {}, {});
}


void MainWindow::onTaskStarted(const QString &taskId)
{
    // 按钮态复刻旧行为：亮度任务只置灰分析按钮（音频按钮点击会得到
    // "分析正在运行中"提示）；音频任务双按钮都置灰
    if (taskId == AnalysisChannels::audio()) {
        m_analyzeBtn->setEnabled(false);
        m_audioAnalysisBtn->setEnabled(false);
        m_audioAnalysisBtn->setText(lang("分析中...", "Analyzing..."));
        m_statusLabel->setText(lang("正在分析音频...", "Analyzing audio..."));
    } else {
        m_analyzeBtn->setEnabled(false);
        m_analyzeBtn->setText(lang("亮度分析中...", "Analyzing luminance..."));
        m_statusLabel->setText(lang("正在准备分析...", "Preparing analysis..."));
    }
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setVisible(true);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
}

void MainWindow::onTaskProgress(const QString &taskId, qreal percent, const QString &detail)
{
    Q_UNUSED(taskId);
    Q_UNUSED(percent);
    // 统一 0~100（Q6 拍板：音频不再映射 70~100）；detail 已按任务语义格式化
    m_statusLabel->setText(detail);
    int newPercent = qBound(0, static_cast<int>(percent), 100);
    if (newPercent > m_progressBar->value())
        m_progressBar->setValue(newPercent);
}


void MainWindow::onTaskFinished(const QString &taskId, const AnalysisSnapshot &snapshot)
{
    const AnalysisTaskDesc *desc = TaskRegistry::instance().find(taskId);

    m_analyzeBtn->setText(lang("亮度分析", "Luminance"));
    m_analyzeBtn->setEnabled(true);
    m_audioAnalysisBtn->setText(lang("音频分析", "Audio"));
    m_audioAnalysisBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(lang("分析完成", "Analysis complete"));

    // 展示面板路由（注册表驱动，R8）：任务产出音频通道 → 刷新语谱
    if (desc && desc->producedChannels.contains(AnalysisChannels::audio())
        && snapshot.hasAudio() && m_spectrogramEnhanced) {
        m_spectrogramEnhanced->setSpectrogramData(snapshot.audioData());
    }

    // Auto-save .vla cache alongside the video file（后台线程；含校时徽标刷新）
    saveCurrentVlaAsync();

    QString msg;
    if (desc && desc->producedChannels.contains(AnalysisChannels::luminance())
        && !snapshot.timestamps.isEmpty()) {
        msg = lang("亮度分析完成！\n数据点数：%1\n区域数：%2",
                   "Luminance analysis complete!\nTotal points: %1\nRegions: %2")
                  .arg(snapshot.pointCount()).arg(snapshot.regionCount());
    } else {
        msg = lang("音频分析完成，音量图、语谱图已生成。",
                   "Audio analysis complete. Volume and spectrogram generated.");
    }

    QTimer::singleShot(0, this, [this, msg]() {
        auto *box = new QMessageBox(QMessageBox::Information,
                                    lang("分析完成", "Analysis Complete"), msg,
                                    QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        QTimer::singleShot(5000, box, &QWidget::close);
        box->show();
    });
}



void MainWindow::onTaskFailed(const QString &taskId, const QString &code, const QString &detail)
{
    const AnalysisTaskDesc *desc = TaskRegistry::instance().find(taskId);
    const QString name = desc ? lang(desc->displayNameZh, desc->displayNameEn) : taskId;

    m_analyzeBtn->setText(lang("亮度分析", "Luminance"));
    m_analyzeBtn->setEnabled(true);
    m_audioAnalysisBtn->setText(lang("音频分析", "Audio"));
    m_audioAnalysisBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(lang("分析失败", "Analysis failed"));

    // C1：按错误码分流提示形态（不再比较错误文案）
    if (code == AnalysisTaskService::kErrEngine) {
        // Defer dialog to avoid processEvents() race condition
        QTimer::singleShot(0, this, [this, detail]() {
            QMessageBox::critical(this, lang("分析失败", "Analysis Failed"),
                lang("离线分析失败。\n\n", "Offline analysis failed.\n\n") + detail);
        });
    } else {
        // 前置/忙/未知任务：信息提示（行为冻结：与旧版 QMessageBox::information 一致）
        QMessageBox::information(this, name, detail);
    }
}

void MainWindow::onTaskCancelled(const QString &taskId)
{
    Q_UNUSED(taskId);
    // 取消：仅复位 UI（行为冻结：旧版对取消不弹任何窗）
    m_analyzeBtn->setText(lang("亮度分析", "Luminance"));
    m_analyzeBtn->setEnabled(true);
    m_audioAnalysisBtn->setText(lang("音频分析", "Audio"));
    m_audioAnalysisBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(lang("分析已取消", "Analysis cancelled"));
}


void MainWindow::toggleFullscreenOnScreen(QScreen *screen)
{
    if (!screen)
        return;
    if (m_fsWindow) {
        const bool same = (m_fsWindow->screen() == screen);
        m_fsWindow->close();          // WA_DeleteOnClose
        m_fsWindow = nullptr;
        if (same)
            return;                   // 再点同屏 = 关闭；换屏 = 关后重开
    }
    m_fsWindow = new FullscreenVideoWindow(nullptr);   // 顶层独立窗
    // 暂停态即刻有画面：推主视口当前原帧 + 当前调节值（播放中由帧流刷新）
    if (m_videoWidget && !m_videoWidget->rawFrame().isNull())
        m_fsWindow->setFrame(m_videoWidget->rawFrame());
    if (m_adjustPanel)
        m_fsWindow->setDisplayAdjust(m_adjustPanel->adjust());
    connect(m_fsWindow, &FullscreenVideoWindow::destroyed, this, [this]() {
        m_fsWindow = nullptr;
    });
    QSettings().setValue(QStringLiteral("fullscreen/screen"), screen->name());
    m_fsWindow->showOnScreen(screen);
    statusBar()->showMessage(lang("副屏全屏已开启：%1（ESC / 双击退出）",
                                  "Fullscreen on: %1 (ESC / double-click to exit)")
                                 .arg(screen->name()), 5000);
}

void MainWindow::toggleFullscreenLastScreen()
{
    if (m_fsWindow) {
        m_fsWindow->close();
        m_fsWindow = nullptr;
        return;
    }
    const auto screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        statusBar()->showMessage(lang("仅检测到一块屏幕，无法副屏全屏",
                                      "Only one screen detected"), 4000);
        return;
    }
    const QString last = QSettings().value(QStringLiteral("fullscreen/screen")).toString();
    QScreen *target = nullptr;
    for (QScreen *s : screens) {
        if (s->name() == last) { target = s; break; }
    }
    if (!target) {   // 无记忆/记忆失效：选主窗不在的那块
        QScreen *winScreen = window() && window()->windowHandle()
                                 ? window()->windowHandle()->screen() : nullptr;
        for (QScreen *s : screens) {
            if (s != winScreen) { target = s; break; }
        }
        if (!target)
            target = screens.first();
    }
    toggleFullscreenOnScreen(target);
}

void MainWindow::onClearRegions()
{
    m_roiModel->clearRegions();
    m_roiModel->clearPolygons();
    // Bug fix: Clear luminance data when all regions are removed.
    // The data is ROI-dependent and meaningless without regions.
    // Audio data is preserved.
    AnalysisSnapshot snapshot = m_timelineModel->snapshot();
    if (!snapshot.isEmpty()) {
        m_timelineModel->clearLuminanceData();
    }
    showOperationStatus(lang("选区已清除", "Regions cleared"));
}

void MainWindow::onClearData()
{
    m_timelineModel->clearData();
    showOperationStatus(lang("数据已清除", "Data cleared"));
}

void MainWindow::onExportCsv()
{
    AnalysisSnapshot snapshot = m_timelineModel->snapshot();
    if (snapshot.isEmpty()) {
        QMessageBox::information(this, lang("导出", "Export"),
            lang("没有数据可导出。", "No data to export."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        lang("导出 CSV", "Export CSV"),
        "luminance_data.csv",
        lang("CSV 文件 (*.csv)", "CSV Files (*.csv)"));

    if (filePath.isEmpty())
        return;

    QVector<QRect> regions = m_roiModel->regions();
    if (snapshot.exportToCsv(filePath, regions, m_calibration)) {
        // Export labels to separate file（P-31 T1：写出归 ProjectIO）
        QVector<ChartLabel> labels = m_chartPanel->labels();
        if (!labels.isEmpty()) {
            QString labelsPath = filePath;
            if (labelsPath.endsWith(".csv", Qt::CaseInsensitive))
                labelsPath.chop(4);
            labelsPath += "_labels.csv";
            if (m_projectIo->exportLabelsCsv(labelsPath, labels, m_calibration)) {
                QMessageBox::information(this, lang("导出", "Export"),
                    lang("数据导出成功。\n标签文件：",
                         "Data exported successfully.\nLabels: ") + labelsPath);
                showOperationStatus(lang("导出成功", "Exported"));
            } else {
                QMessageBox::information(this, lang("导出", "Export"),
                    lang("数据导出成功。\n标签导出失败。",
                         "Data exported successfully.\nFailed to export labels."));
            }
        } else {
            QMessageBox::information(this, lang("导出", "Export"),
                lang("数据导出成功。", "Data exported successfully."));
        }
    } else {
        QMessageBox::critical(this, lang("错误", "Error"),
            lang("数据导出失败。", "Failed to export data."));
    }
}

void MainWindow::onDurationChanged(qint64 durationMs)
{
    // P-31 T4：时长校准单点（UiState：可信值为上限钳制，消五副本 R5/P-37）
    m_uiState->ingestEngineDuration(durationMs);
    const qint64 effectiveDur = m_uiState->effectiveDurationMs();

    m_chartPanel->setDuration(effectiveDur);

    // 拖拽匀速化（第一层）：向图表传入帧时长，启用速度自适应帧网格量化
    const float fpsNow = m_videoEngine->fps();
    m_chartPanel->setFrameDuration(fpsNow > 0.0f ? qint64(1000.0 / fpsNow + 0.5) : 0);

    // v0.3: Sync duration to VideoListPanel
    if (!m_sessionMgr->currentVideoPath().isEmpty() && m_videoListPanel) {
        m_videoListPanel->updateDuration(m_sessionMgr->currentVideoPath(), effectiveDur);
    }

    updateTimeDisplay();

    // 更新硬解适配器显示（引擎 openFile 后 adapterName 已确定）
    QString adapter = m_videoEngine->hardwareAdapterName();
    if (adapter.isEmpty())
        m_hwAdapterLabel->setText(lang("软解", "SW decode"));
    else
        m_hwAdapterLabel->setText(adapter);
}

void MainWindow::onPositionChanged(qint64 timeMs)
{
    // Bug fix: During chart cursor drag, VLC may fire positionChanged with
    // stale intermediate positions (play→pause cycle during seek). Skip
    // overwriting the cursor position so the drag stays smooth.
    if (!m_chartPanel->isDraggingCursor() && !m_spectrogramEnhanced->isDraggingCursor()) {
        m_chartPanel->setCursorTime(timeMs);
        m_spectrogramEnhanced->setCursorTime(timeMs);
    }

    // A/B region loop playback
    if (m_chartPanel->isABRegionSet() && m_videoEngine->state() == PlaybackState::Playing) {
        qint64 bPoint = m_chartPanel->abPointB();
        if (timeMs >= bPoint) {
            if (m_chartPanel->isABLoop()) {
                m_videoEngine->seek(m_chartPanel->abPointA());
            } else {
                onPause();
            }
        }
    }

    updateTimeDisplay();
}

void MainWindow::onSeekFromChart(qint64 timeMs)
{
    // 光标立即跟随（UI 响应）
    m_chartPanel->setCursorTime(timeMs);
    m_spectrogramEnhanced->setCursorTime(timeMs);

    // 拖拽中：scrub 追逐模式——只写原子目标，引擎 worker 连续解码追赶，
    // 免节流免命令队列（mouseMove 不再产生 seek 命令，拖拽期间解码管线不断流）。
    // 图表与语谱两个面板的光标拖拽都走此路径（此前语谱拖拽漏判，
    // 退化成 50ms 节流一次性 seek：每拍全量 flush 重定，无追赶无缓存——卡顿主因）
    const bool dragging = m_chartPanel->isDraggingCursor()
                          || (m_spectrogramEnhanced && m_spectrogramEnhanced->isDraggingCursor());
    if (dragging) {
        m_videoEngine->setScrubMode(true);
        m_videoEngine->setScrubTarget(timeMs);
    } else {
        // 点击/标签跳转：一次性 seek
        m_videoEngine->setScrubMode(false);
        m_pendingSeekMs = timeMs;
        if (!m_seekThrottleTimer) {
            m_seekThrottleTimer = new QTimer(this);
            m_seekThrottleTimer->setSingleShot(true);
            m_seekThrottleTimer->setInterval(50);
            connect(m_seekThrottleTimer, &QTimer::timeout, this, [this]() {
                if (m_pendingSeekMs != m_lastIssuedSeekMs && m_pendingSeekMs >= 0) {
                    m_lastIssuedSeekMs = m_pendingSeekMs;
                    m_videoEngine->seek(m_pendingSeekMs);
                }
            });
        }
        if (!m_seekThrottleTimer->isActive()) {
            m_lastIssuedSeekMs = timeMs;
            m_videoEngine->seek(timeMs);
            m_seekThrottleTimer->start();
        }
    }
}

void MainWindow::updateTimeDisplay()
{
    qint64 pos = m_videoEngine ? m_videoEngine->position() : 0;
    qint64 dur = m_uiState->effectiveDurationMs();
    if (dur <= 0 && m_videoEngine)
        dur = m_videoEngine->duration();
    m_timeLabel->setText(QString("%1 / %2").arg(formatTime(pos)).arg(formatTime(dur)));
}

QString MainWindow::formatTime(qint64 ms) const
{
    if (ms < 0)
        ms = 0;
    int totalSeconds = static_cast<int>(ms / 1000);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    if (hours > 0)
        return QString("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}

/// @brief 在状态栏左侧显示操作反馈（2秒后自动清除）
void MainWindow::showOperationStatus(const QString &text)
{
    m_operationLabel->setText(text);
    QTimer::singleShot(5000, this, [this]() {
        m_operationLabel->clear();
    });
}

/// @brief 恢复分析状态：区域/校时/标签/截图融合/音频
/// .vla 装载结果的统一应用（R9 去重：.vla 直载/缓存两路共用；
/// 内存现场路字段结构不同，保持独立）。行为冻结：三段代码逐字同源。
void MainWindow::applyAnalysisArtifacts(const ProjectIO::LoadedVla &lv)
{
    restoreAnalysisState(lv.regions, lv.calibration, lv.labels, lv.pinnedRect,
                         lv.fusion, lv.regionRoiIds);
    if (lv.polygonRoiIds.size() == lv.polygons.size())
        m_roiModel->restorePolygons(lv.polygons, lv.polygonRoiIds);
    else {
        m_roiModel->clearPolygons();
        for (const QPolygon &poly : lv.polygons)
            m_roiModel->addPolygon(poly);
    }
    m_guideLineModel->clearLines();
    for (const GuideLine &line : lv.guideLines)
        m_guideLineModel->addLine(line);
    // P-68：A/B 选段与分段变速方案随 .vla 恢复（拍板 Q5）
    if (lv.abRegion.isValid()) {
        m_chartPanel->setPointA(lv.abRegion.a);
        m_chartPanel->setPointB(lv.abRegion.b);
        m_chartPanel->setABLoop(lv.abRegion.loop);
    }
    m_speedPlan = lv.speedPlan;
}

void MainWindow::restoreAnalysisState(const QVector<QRect> &regions,
                                       const TimeCalibration &calibration,
                                       const QVector<ChartLabel> &labels,
                                       const QRect &pinnedRect,
                                       const SnapshotFusionData &fusion,
                                       const QVector<int> &regionRoiIds)
{
    // 带 roiId 恢复：保持与分析数据 dataEntries 的 roi_id 对齐
    if (regionRoiIds.size() == regions.size())
        m_roiModel->restoreRegions(regions, regionRoiIds);
    else {
        m_roiModel->clearRegions();
        for (const QRect &rc : regions)
            m_roiModel->addRegion(rc);
    }
    m_calibration = calibration;
    m_chartPanel->setCalibration(m_calibration);
    m_chartPanel->setLabels(labels);
    if (!pinnedRect.isEmpty())
        m_pinnedRect = pinnedRect;
    m_snapshotFusion = fusion;
    if (fusion.isValid() && !fusion.imageData.isNull()) {
        m_snapshotOverlay->setSnapshot(fusion.imageData);
        m_snapshotOverlay->setParameters(fusion.brightness, fusion.contrast, fusion.opacity);
        m_editBtn->setEnabled(true);
        m_placeBtn->setEnabled(true);
    }
    AnalysisSnapshot loaded = m_timelineModel->snapshot();
    qDebug() << "[restoreAnalysisState] hasAudio:" << loaded.hasAudio()
             << "spectrogram.size:" << loaded.audioData().spectrogram.size()
             << "volume.size:" << loaded.audioData().volume.size();
    if (m_spectrogramEnhanced && loaded.hasAudio())
        m_spectrogramEnhanced->setSpectrogramData(loaded.audioData());
}

bool MainWindow::handleGlobalShortcut(QKeyEvent *e)
{
    int key = e->key();
    QWidget *fw = focusWidget();
    // Protection: Don't intercept text input widgets
    if (qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw)) {
        return false;
    }

    // Check if video engine is available
    if (!m_videoEngine || !m_videoEngine->duration())
        return false;

    // Handle all shortcuts globally
    switch (key) {
    case Qt::Key_Space:
        if (m_videoEngine->state() == PlaybackState::Playing) {
            m_videoEngine->pause();
            showOperationStatus(lang("暂停", "Paused"));
        } else {
            m_videoEngine->play();
            showOperationStatus(lang("播放", "Playing"));
        }
        updatePlaybackButtons();
        return true;

    case Qt::Key_Left: {
        float f = m_videoEngine->fps();
        qint64 frameStep = static_cast<qint64>(1000.0f / f);
        if (frameStep < 1) frameStep = 33;
        m_videoEngine->seek(m_videoEngine->position() - frameStep);
        showOperationStatus(lang("帧 -1", "Frame -1"));
        return true;
    }
    case Qt::Key_Right: {
        float f = m_videoEngine->fps();
        qint64 frameStep = static_cast<qint64>(1000.0f / f);
        if (frameStep < 1) frameStep = 33;
        m_videoEngine->seek(m_videoEngine->position() + frameStep);
        showOperationStatus(lang("帧 +1", "Frame +1"));
        return true;
    }

    case Qt::Key_Up: {
        const int cur = m_videoEngine->volume();
        // v1.7.1：突破 200% 时给一次提示（每次会话仅一次，防重复打扰）
        if (cur <= 200 && cur + 5 > 200 && !m_volumeWarnShown) {
            m_volumeWarnShown = true;
            QMessageBox::information(this, lang("音量", "Volume"),
                lang("音量即将超过 200%。继续增大可能造成声音失真（削波），\n"
                     "建议仅在原始素材音量过低时使用。",
                     "Volume will exceed 200%. Further increase may cause "
                     "clipping distortion.\nRecommended only for very quiet "
                     "source material."));
        }
        m_videoEngine->setVolume(cur + 5);   // 上限 500%
        showOperationStatus(QString(lang("音量 +5，现音量：%1%", "Volume +5, Current: %1%"))
                                .arg(m_videoEngine->volume()));
        return true;
    }
    case Qt::Key_Down:
        m_videoEngine->setVolume(m_videoEngine->volume() - 5);
        showOperationStatus(QString(lang("音量 -5，现音量：%1%", "Volume -5, Current: %1%"))
                                .arg(m_videoEngine->volume()));
        return true;

    case Qt::Key_C:
        adjustSpeed(1.0f);
        return true;
    case Qt::Key_X:
        adjustSpeed(-1.0f);
        return true;
    case Qt::Key_Z:
        m_playbackSettings->setSpeed(1.0f);
        m_speedBtn->setText("1x");
        m_videoEngine->setRate(1.0f);
        showOperationStatus(lang("倍速 1x", "Speed 1x"));
        return true;

    case Qt::Key_N: {
        if (m_videoEngine->duration() > 0) {
            // Pause while the modal label dialog is open; otherwise the
            // video keeps playing (and short clips reach the end).
            bool wasPlaying = (m_videoEngine->state() == PlaybackState::Playing);
            if (wasPlaying)
                m_videoEngine->pause();
            qint64 pos = m_videoEngine->position();
            // 标签一律流内时间存储（显示时走校时换算）。修复：旧代码把
            // 显示偏移加进存储值，设置过时间后标签错位一个 offset 且
            // 悬停/导出时间加了两次 offset。
            m_chartPanel->addLabelAtTime(pos);
            if (wasPlaying)
                m_videoEngine->play();
            showOperationStatus(lang("标签已添加", "Label added"));
        }
        return true;
    }

    case Qt::Key_A: {
        if (m_videoEngine->duration() > 0) {
            qint64 pos = m_videoEngine->position();
            m_chartPanel->setPointA(pos);
            showOperationStatus(lang("A 点已设置", "Point A set"));
        }
        return true;
    }
    case Qt::Key_B: {
        if (m_videoEngine->duration() > 0) {
            qint64 pos = m_videoEngine->position();
            m_chartPanel->setPointB(pos);
            showOperationStatus(lang("B 点已设置", "Point B set"));
        }
        return true;
    }
    case Qt::Key_S:
        onSnapshotQuick();   // 证据快照（2026-08-14）
        return true;
    case Qt::Key_J:
        adjustSpeed(-1.0f);
        return true;
    case Qt::Key_K:
        if (m_videoEngine->state() == PlaybackState::Playing) {
            m_videoEngine->pause();
            showOperationStatus(lang("暂停", "Paused"));
        } else {
            m_videoEngine->play();
            showOperationStatus(lang("播放", "Playing"));
        }
        updatePlaybackButtons();
        return true;
    case Qt::Key_L:
        adjustSpeed(1.0f);
        return true;

    case Qt::Key_Escape:
        // 如果OverlayWidget在辅助线模式或多边形模式，不处理，让OverlayWidget处理
        if (m_videoWidget->overlay()->isGuideLineMode() ||
            m_videoWidget->overlay()->isPolygonMode()) {
            return false;  // 让事件继续传递给OverlayWidget
        }
        // 否则关闭放大镜
        removeMagnifier();
        showOperationStatus(lang("放大镜已关闭", "Magnifier closed"));
        return true;

    case Qt::Key_P:
        // 切换矩形/多边形模式
        if (m_videoWidget->overlay()->isPolygonMode()) {
            onRectMode();
        } else {
            onPolygonMode();
        }
        return true;

    case Qt::Key_G:
        // 切换辅助线模式
        if (m_videoWidget->overlay()->isGuideLineMode()) {
            onRectMode();
        } else {
            onGuideLineMode();
        }
        return true;
    }

    return false;
}



void MainWindow::onVideoSelected(int index)
{
    if (index >= 0 && index < m_videoListPanel->videoCount()) {
        // B6: Cancel any running analysis before switching video to prevent
        // stale results being saved under the wrong filename.
        if (m_taskService->isRunning()) {
            m_taskService->cancel();   // B6：切换视频前取消运行中分析（迟到结果被 gating 拦截）
        }
        VideoEntry entry = m_videoListPanel->videoAt(index);
        if (!entry.filePath.isEmpty()) {
            openVideoFile(entry.filePath);
        }
    }
}

// v0.5: 矩形模式
void MainWindow::onRectMode()
{
    m_videoWidget->overlay()->setPolygonMode(false);
    m_videoWidget->overlay()->setGuideLineMode(false);
    if (m_magnifier && m_magnifier->overlay()) {
        m_magnifier->overlay()->setPolygonMode(false);
        m_magnifier->overlay()->setGuideLineMode(false);
    }
    m_rectModeBtn->setChecked(true);
    m_polygonModeBtn->setChecked(false);
    m_guideLineBtn->setChecked(false);
}

// v0.5: 多边形模式
void MainWindow::onPolygonMode()
{
    m_videoWidget->overlay()->setPolygonMode(true);
    if (m_magnifier && m_magnifier->overlay()) {
        m_magnifier->overlay()->setPolygonMode(true);
    }
    m_rectModeBtn->setChecked(false);
    m_polygonModeBtn->setChecked(true);
    m_guideLineBtn->setChecked(false);
}

// v0.5: 辅助线模式
void MainWindow::onGuideLineMode()
{
    m_videoWidget->overlay()->setGuideLineMode(true);
    if (m_magnifier && m_magnifier->overlay()) {
        m_magnifier->overlay()->setGuideLineMode(true);
    }
    m_rectModeBtn->setChecked(false);
    m_polygonModeBtn->setChecked(false);
    m_guideLineBtn->setChecked(true);
}

// v0.5: 复制ROI
void MainWindow::onCopyRoi()
{
    m_roiClipboard = m_roiModel->regions();
    m_polygonClipboard = m_roiModel->polygons();
    m_guideLineClipboard = m_guideLineModel->lines();
    int total = m_roiClipboard.size() + m_polygonClipboard.size();
    if (total > 0) {
        showOperationStatus(lang(QString("已复制 %1 个ROI区域").arg(total),
                                 QString("Copied %1 ROI regions").arg(total)));
        m_pasteRoiBtn->setEnabled(true);
    } else {
        showOperationStatus(lang("没有ROI可复制", "No ROI to copy"));
    }
}

// v0.5: 粘贴ROI
void MainWindow::onPasteRoi()
{
    if (m_roiClipboard.isEmpty() && m_polygonClipboard.isEmpty()) {
        showOperationStatus(lang("剪贴板为空", "Clipboard is empty"));
        return;
    }

    // Check if there's existing analysis data
    AnalysisSnapshot snap = m_timelineModel->snapshot();
    if (!snap.isEmpty()) {
        QMessageBox msgWarn(this);
        msgWarn.setIcon(QMessageBox::Warning);
        msgWarn.setWindowTitle(lang("粘贴ROI", "Paste ROI"));
        msgWarn.setText(lang("当前视频已有亮度分析数据。\n粘贴新的ROI将导致已有数据失效。",
                             "The current video has luminance analysis data.\nPasting new ROI will invalidate existing data."));
        auto continueBtn = msgWarn.addButton(lang("继续", "Continue"), QMessageBox::AcceptRole);
        auto cancelBtn = msgWarn.addButton(QMessageBox::Cancel);
        msgWarn.exec();
        if (msgWarn.clickedButton() == cancelBtn)
            return;
        m_timelineModel->clearLuminanceData();
    }

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setWindowTitle(lang("粘贴ROI", "Paste ROI"));
    msgBox.setText(lang("替换当前ROI还是追加？", "Replace or append current ROI?"));
    auto replaceBtn = msgBox.addButton(lang("替换", "Replace"), QMessageBox::AcceptRole);
    auto appendBtn = msgBox.addButton(lang("追加", "Append"), QMessageBox::AcceptRole);
    auto cancelBtn = msgBox.addButton(QMessageBox::Cancel);
    msgBox.exec();

    if (msgBox.clickedButton() == cancelBtn)
        return;

    if (msgBox.clickedButton() == replaceBtn) {
        m_roiModel->clearRegions();
        m_roiModel->clearPolygons();
        m_guideLineModel->clearLines();
    }

    for (const QRect &rc : m_roiClipboard) {
        m_roiModel->addRegion(rc);
    }
    for (const QPolygon &poly : m_polygonClipboard) {
        m_roiModel->addPolygon(poly);
    }
    for (const GuideLine &line : m_guideLineClipboard) {
        m_guideLineModel->addLine(line);
    }

    m_videoWidget->overlay()->update();

    int total = m_roiClipboard.size() + m_polygonClipboard.size();
    showOperationStatus(lang(QString("已粘贴 %1 个ROI区域").arg(total),
                             QString("Pasted %1 ROI regions").arg(total)));
}

// v0.5: 粘贴ROI到所有视频
void MainWindow::onPasteRoiToAll()
{
    if (m_roiClipboard.isEmpty() && m_polygonClipboard.isEmpty()) {
        showOperationStatus(lang("剪贴板为空", "Clipboard is empty"));
        return;
    }

    // 这里可以扩展为将ROI保存到VideoStateManager
    // 目前先提示用户
    QMessageBox::information(this,
        lang("粘贴到所有视频", "Paste to All Videos"),
        lang("此功能将在后续版本中实现。",
             "This feature will be implemented in a future version."));
}
