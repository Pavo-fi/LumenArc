/**
 * @file mainwindow_case.cpp
 * @brief v1.17.0 P-79：自 mainwindow.cpp 按职责域拆出（行为冻结纯移动，定义仍属 MainWindow）
 */
#include "mainwindow.h"
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

/// 构建时间戳（2026-08-13）：标题栏常驻，杜绝“用户在跑旧构建”无法辨识

// ---------------------------------------------------------------------------
// 案件模式（v1.3.0 M2 任务10）
// ---------------------------------------------------------------------------
QString MainWindow::windowTitleWithCase(const QString &base) const
{
    QString t = base;
    if (m_caseManager && m_caseManager->isOpen())
        t += QStringLiteral(" - 《")
            + m_caseManager->meta().caseNo + QStringLiteral("-")
            + m_caseManager->meta().title + QStringLiteral("》");
    return t + buildStamp();   // 构建时间戳常驻标题栏
}

void MainWindow::enterCaseMode()
{
    // CaseDock 替代视频列表（仅案件模式，拍板§8-6）
    m_videoListPanel->setVisible(false);
    m_videoListPlaceholder->setVisible(false);
    m_caseDock->setVisible(true);
    m_caseDock->refreshTree();
    resizeDocks({m_caseDock}, {250}, Qt::Horizontal);
    m_caseStatusBtn->setText(
        QStringLiteral("📁 ") + m_caseManager->meta().caseNo);
    m_caseStatusBtn->setVisible(true);
    if (m_closeCaseAction)
        m_closeCaseAction->setEnabled(true);
    if (m_casePropsAction)
        m_casePropsAction->setEnabled(true);
        m_genReportAction->setEnabled(true);
        m_sitemapAction->setEnabled(true);
    if (m_exportCaseAction)
        m_exportCaseAction->setEnabled(true);
    if (m_batchRelocateAction)
        m_batchRelocateAction->setEnabled(true);
    setWindowTitle(windowTitleWithCase(
        lang("追光者 Lumen Arc v1.16.1", "Lumen Arc v1.16.1")));
    showOperationStatus(lang("案件已打开：%1", "Case opened: %1")
                            .arg(m_caseManager->meta().caseNo));
    // 开案批量校时徽标校验（用户实测：旧 vla time_offset=0 误亮 ⏰ 且只在
    // 打开视频时才刷新）——轻量读每个标了校准的视频的 vla 校时字段，
    // 修正 case.json 里的历史误值。同步但仅限 hasCalibration 的视频
    // （通常很少），单个 peek 只读 META/顶层字段，毫秒级。
    {
        const auto videos = m_caseManager->meta().videos;
        int fixed = 0;
        for (const auto &v : videos) {
            if (!v.hasCalibration)
                continue;
            const QString vlaPath = m_caseManager->vlaPathFor(v.originalPath);
            const TimeCalibration peek = TimelineModel::peekCalibrationFromVla(vlaPath);
            if (!peek.isEffective()) {
                m_caseManager->updateCalibrationBadge(v.originalPath, false, QString());
                ++fixed;
            }
        }
        if (fixed > 0) {
            m_caseDock->refreshTree();
            showOperationStatus(lang("已修正 %1 个视频的校时徽标（旧数据误标）",
                                     "Fixed %1 calibration badges (stale)")
                                    .arg(fixed));
        }
    }
    // 开案恢复现场（uiState.lastVideoId）：.vla 缓存探测自动加载分析数据
    if (const auto *v = m_caseManager->videoById(m_caseManager->meta().lastVideoId)) {
        if (QFile::exists(v->originalPath))
            openVideoFile(v->originalPath);
    }
}

void MainWindow::exitCaseMode()
{
    m_caseDock->setVisible(false);
    m_videoListPanel->setVisible(true);
    m_videoListContent->setVisible(true);
    resizeDocks({m_videoListPanel}, {250}, Qt::Horizontal);
    m_caseStatusBtn->setVisible(false);
    if (m_closeCaseAction)
        m_closeCaseAction->setEnabled(false);
    if (m_casePropsAction)
        m_casePropsAction->setEnabled(false);
        m_genReportAction->setEnabled(false);
        m_sitemapAction->setEnabled(false);
    if (m_exportCaseAction)
        m_exportCaseAction->setEnabled(false);
    if (m_batchRelocateAction)
        m_batchRelocateAction->setEnabled(false);
    setWindowTitle(lang("追光者 Lumen Arc v1.16.1", "Lumen Arc v1.16.1"));
    showOperationStatus(lang("案件已关闭", "Case closed"));
}

void MainWindow::closeCaseWithPrompt()
{
    if (!m_caseManager || !m_caseManager->isOpen())
        return;
    // 记录现场后关案（不中断播放，拍板§8-6）
    if (const auto *v = m_caseManager->videoByPath(m_currentVideoPath))
        m_caseManager->setLastVideoId(v->id);
    if (m_caseManager->isDirty()) {
        const auto reply = QMessageBox::question(this,
            lang("关闭案件", "Close Case"),
            lang("案件有未保存的变更。关闭前是否保存？",
                 "The case has unsaved changes. Save before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (reply == QMessageBox::Cancel)
            return;
        if (reply == QMessageBox::Save) {
            QString err;
            if (!m_caseManager->saveCase(&err)) {
                QMessageBox::critical(this, lang("错误", "Error"),
                    lang("案件保存失败：%1", "Failed to save case: %1").arg(err));
                return;
            }
        }
    }
    m_caseManager->closeCase();   // caseClosed → exitCaseMode
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 退出前案件 dirty 检查（取证：未保存变更须明示；取消则中止退出）
    if (m_caseManager && m_caseManager->isOpen() && m_caseManager->isDirty()) {
        if (const auto *v = m_caseManager->videoByPath(m_currentVideoPath))
            m_caseManager->setLastVideoId(v->id);
        const auto reply = QMessageBox::question(this,
            lang("退出", "Exit"),
            lang("案件有未保存的变更。退出前是否保存？",
                 "The case has unsaved changes. Save before exiting?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (reply == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (reply == QMessageBox::Save) {
            QString err;
            if (!m_caseManager->saveCase(&err)) {
                QMessageBox::critical(this, lang("错误", "Error"),
                    lang("案件保存失败：%1", "Failed to save case: %1").arg(err));
                event->ignore();
                return;
            }
        }
    }
    QMainWindow::closeEvent(event);
}


// ---------------------------------------------------------------------------
// 案件对话框与起始页（v1.3.0 M2 任务11）
// ---------------------------------------------------------------------------
void MainWindow::onNewCase()
{
    if (m_caseManager->isOpen()) {
        closeCaseWithPrompt();
        if (m_caseManager->isOpen())
            return;   // 用户取消，维持现案
    }
    QDir().mkpath(CaseManager::caseRootDir());
    NewCaseDialog dlg(CaseManager::caseRootDir(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString err;
    if (!m_caseManager->createCase(CaseManager::caseRootDir(), dlg.meta(), &err))
        QMessageBox::critical(this, lang("新建案件失败", "New Case Failed"), err);
    // 成功：caseOpened 信号 → enterCaseMode 自动
}

void MainWindow::onOpenCase()
{
    // 页面内居中面板（Blender 式，2026-08 人工反馈：不弹窗打开案件）
    if (!m_caseOpenPanel)
        return;
    centerCaseOpenPanel();
    m_caseOpenPanel->refresh();
    m_caseOpenPanel->show();
    m_caseOpenPanel->raise();
    m_caseOpenPanel->activateWindow();
}

void MainWindow::onOpenCaseBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
        lang("打开案件（选择案件目录）", "Open Case (choose case folder)"),
        CaseManager::caseRootDir());
    if (!dir.isEmpty()) {
        m_caseOpenPanel->hide();
        openCaseFlow(dir);
    }
}

void MainWindow::centerCaseOpenPanel()
{
    if (!m_caseOpenPanel || !centralWidget())
        return;
    // 以内容区为基准居中（dock/工具栏之外）
    const QRect r = centralWidget()->rect();
    m_caseOpenPanel->move(r.center() - m_caseOpenPanel->rect().center());
}

void MainWindow::openCaseFlow(const QString &dir)
{
    if (dir.isEmpty())
        return;
    if (m_caseManager->isOpen()) {
        closeCaseWithPrompt();
        if (m_caseManager->isOpen())
            return;   // 用户取消
    }
    QString err;
    QStringList warnings;
    bool lockConflict = false;
    bool opened = m_caseManager->openCase(dir, &err, &warnings,
                                          &lockConflict, false);
    if (!opened && lockConflict) {
        // 残留锁已自动清理；到此 = 真双开冲突（持有者进程仍在）
        const auto reply = QMessageBox::warning(this,
            lang("案件被锁定", "Case Locked"),
            lang("案件正在另一个实例中打开。\n\n%1\n\n"
                 "仍要强制打开吗？（建议先关闭另一实例）",
                 "The case is open in another instance.\n\n%1\n\n"
                 "Force open anyway? (close the other instance first)")
                .arg(err),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
        opened = m_caseManager->openCase(dir, &err, &warnings,
                                         nullptr, true);
    }
    if (!opened) {
        QMessageBox::critical(this, lang("打开案件失败", "Open Case Failed"),
                              err);
        return;
    }
    if (m_caseOpenPanel)
        m_caseOpenPanel->hide();
    if (!warnings.isEmpty())
        showOperationStatus(warnings.join(QStringLiteral("；")));
    // caseOpened 信号 → enterCaseMode 自动
}

void MainWindow::onCaseProperties()
{
    if (!m_caseManager->isOpen())
        return;
    CasePropertiesDialog dlg(m_caseManager, this);
    dlg.exec();
    // 名称可能已改：刷新标题/面板/状态栏
    if (m_caseManager->isOpen()) {
        setWindowTitle(windowTitleWithCase(
            lang("追光者 Lumen Arc v1.16.1", "Lumen Arc v1.16.1")));
        m_caseDock->refreshTree();
        m_caseStatusBtn->setText(
            QStringLiteral("📁 ") + m_caseManager->meta().caseNo);
    }
}

void MainWindow::onCaseRootDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
        lang("选择案件根目录（新建案件的默认存放位置）",
             "Choose case root folder (default location for new cases)"),
        CaseManager::caseRootDir());
    if (dir.isEmpty())
        return;
    CaseManager::setCaseRootDir(dir);
    showOperationStatus(lang("案件根目录：%1", "Case root: %1").arg(dir));
}

void MainWindow::onShowStartPage()
{
    // 页面内居中欢迎面板（Blender 式，非模态）：主界面已打开后才出现
    if (!m_caseOpenPanel)
        return;
    centerCaseOpenPanel();
    m_caseOpenPanel->refresh();
    m_caseOpenPanel->show();
    m_caseOpenPanel->raise();
    m_caseOpenPanel->activateWindow();
}

void MainWindow::onExportCase()
{
    if (!m_caseManager->isOpen())
        return;
    ExportCaseDialog dlg(m_caseManager, this);
    dlg.exec();
}

void MainWindow::onBatchRelocate()
{
    if (!m_caseManager->isOpen())
        return;
    BatchRelocateDialog dlg(m_caseManager, this);
    dlg.exec();
}

void MainWindow::onMultiCamView()
{
    if (!m_caseManager->isOpen())
        return;
    if (m_multiCamWin) {          // 单例：已开则置顶
        m_multiCamWin->raise();
        m_multiCamWin->activateWindow();
        return;
    }
    // 主窗正在播则先暂停（避免双路音频/资源竞争）
    if (m_videoEngine && m_videoEngine->state() == PlaybackState::Playing)
        m_videoEngine->pause();
    auto *win = new MultiCamPlaybackWindow(this);
    m_multiCamWin = win;
    // R4：具体引擎仅在此构造并注入工厂（与主引擎同一 QSettings 硬解口径）
    win->setEngineFactory([](QObject *parent) -> IVideoEngine * {
        QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
        auto *e = new FfmpegVideoEngine(parent);
        e->setHardwareDecode(s.value(QStringLiteral("hwDecode"), true).toBool());
        e->setHardwareAdapter(s.value(QStringLiteral("hwAdapter"), -1).toInt());
        return e;
    });
    win->onOpenVideo = [this](const QString &path) { openVideoFile(path); };
    // v1.15.3：多机窗保存校时/改案数据 → 案件树徽标即刷新；
    // 若正中主视口当前视频：同步内存+时间轴，防旧值回写覆盖 .vla
    win->onCaseDataChanged = [this](const QString &path) {
        if (m_caseDock)
            m_caseDock->refreshTree();
        if (!path.isEmpty() && !m_currentVideoPath.isEmpty()
            && QString::compare(QDir::cleanPath(path),
                                QDir::cleanPath(m_currentVideoPath),
                                Qt::CaseInsensitive) == 0) {
            const TimeCalibration cal = TimelineModel::peekCalibrationFromVla(
                m_caseManager->vlaPathFor(path));
            if (cal.isValid()) {
                m_calibration = cal;
                m_chartPanel->setCalibration(m_calibration);
                showOperationStatus(lang(
                    "主视口校时已与多机窗口同步（同事件对时）",
                    "Main view calibration synced from multicam"));
            }
        }
    };
    if (!win->openCaseLanes(*m_caseManager)) {
        delete win;
        m_multiCamWin = nullptr;
        return;
    }
    // v1.16.0：多机起播前拦主视口（开窗时的一次暂停挡不住回主窗再播）
    win->onAboutToPlay = [this]() {
        if (m_videoEngine && m_videoEngine->state() == PlaybackState::Playing)
            m_videoEngine->pause();
    };
    {
        // P-54b：多机窗继承播放降噪设置
        QSettings s("LumenArc", "LumenArc");
        win->applyPlaybackDenoise(s.value("playbackDenoise", false).toBool(),
                                  m_noiseReductionStrength);
    }
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->showMaximized();   // v1.16.0 拍板：多机窗默认最大化（多路铺屏）
}

void MainWindow::onMultiCamStandalone()
{
    if (m_multiCamWin) {
        m_multiCamWin->raise();
        m_multiCamWin->activateWindow();
        return;
    }
    if (m_videoEngine && m_videoEngine->state() == PlaybackState::Playing)
        m_videoEngine->pause();
    auto *win = new MultiCamPlaybackWindow(this);
    m_multiCamWin = win;
    win->setEngineFactory([](QObject *parent) -> IVideoEngine * {
        QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
        auto *e = new FfmpegVideoEngine(parent);
        e->setHardwareDecode(s.value(QStringLiteral("hwDecode"), true).toBool());
        e->setHardwareAdapter(s.value(QStringLiteral("hwAdapter"), -1).toInt());
        return e;
    });
    win->onOpenVideo = [this](const QString &path) { openVideoFile(path); };
    win->onCaseDataChanged = [this](const QString &path) {
        if (m_caseDock)
            m_caseDock->refreshTree();
        // 多机窗保存的校时若正中主视口当前视频：同步内存+时间轴，
        // 防主窗旧 m_calibration 后续自动存盘回写覆盖 .vla 新校时
        if (!path.isEmpty() && !m_currentVideoPath.isEmpty()
            && QString::compare(QDir::cleanPath(path),
                                QDir::cleanPath(m_currentVideoPath),
                                Qt::CaseInsensitive) == 0) {
            const TimeCalibration cal = TimelineModel::peekCalibrationFromVla(
                m_caseManager->vlaPathFor(path));
            if (cal.isValid()) {
                m_calibration = cal;
                m_chartPanel->setCalibration(m_calibration);
                showOperationStatus(lang(
                    "主视口校时已与多机窗口同步（同事件对时）",
                    "Main view calibration synced from multicam"));
            }
        }
    };
    win->openStandalone();
    win->onAboutToPlay = [this]() {   // v1.16.0：同上拦主视口
        if (m_videoEngine && m_videoEngine->state() == PlaybackState::Playing)
            m_videoEngine->pause();
    };
    {
        QSettings s("LumenArc", "LumenArc");   // P-54b 同上
        win->applyPlaybackDenoise(s.value("playbackDenoise", false).toBool(),
                                  m_noiseReductionStrength);
    }
    win->setAttribute(Qt::WA_DeleteOnClose);
    win->showMaximized();   // v1.16.0：默认最大化
}

void MainWindow::showTrayNotification(const QString &title,
                                      const QString &message)
{
    // v1.2.2：校时长任务（重建可达数分钟）用户最小化等待场景 ——
    // Windows toast 通知。用户正在操作任一窗口时不打扰。
    if (QApplication::activeWindow())
        return;
    if (!QSystemTrayIcon::isSystemTrayAvailable()
        || !QSystemTrayIcon::supportsMessages())
        return;
    if (!m_trayIcon) {
        m_trayIcon = new QSystemTrayIcon(windowIcon(), this);
        connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, [this]() {
            if (m_calibrationDialog) {
                m_calibrationDialog->showNormal();
                m_calibrationDialog->raise();
                m_calibrationDialog->activateWindow();
            }
            showNormal();
            raise();
            activateWindow();
        });
    }
    m_trayIcon->show();
    m_trayIcon->showMessage(title, message,
                            QSystemTrayIcon::Information, 10000);
    // 托盘图标不常驻：消息弹出后自动隐藏（toast 已入系统通知中心）
    QTimer::singleShot(15000, m_trayIcon, &QSystemTrayIcon::hide);
}

