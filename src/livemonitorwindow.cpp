/**
 * @file livemonitorwindow.cpp
 * @brief 监控直播窗口实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "livemonitorwindow.h"

#include "videowidget.h"
#include "snapshotoverlay.h"
#include "i18n.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"
#include "infrastructure/live/live_stream_engine.h"
#include "infrastructure/live/live_recorder.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QString kSettingsGroup = QStringLiteral("LumenArc");
const QString kSettingsApp = QStringLiteral("LumenArc");

QString formatDuration(qint64 ms)
{
    const qint64 total = ms / 1000;
    const qint64 h = total / 3600;
    const qint64 m = (total % 3600) / 60;
    const qint64 s = total % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QLatin1Char('0'))
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'));
}

} // namespace

LiveMonitorWindow::LiveMonitorWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(lang(QStringLiteral("监控直播 — 追光者 Lumen Arc"),
                        QStringLiteral("Live Monitor — Lumen Arc")));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Window);   // 有父窗口时仍作为独立顶层窗口（任务栏可见）
    resize(1120, 720);

    buildUi();
    loadSettings();

    // 引擎/录制器
    m_engine = new LiveStreamEngine(this);
    m_recorder = new LiveRecorder(this);
    m_video->setVideoEngine(m_engine);   // 画面接直播引擎（含分辨率→叠加层同步）

    connect(m_engine, &LiveStreamEngine::streamStarted,
            this, &LiveMonitorWindow::onStreamStarted);
    connect(m_engine, &LiveStreamEngine::streamError,
            this, &LiveMonitorWindow::onStreamError);
    connect(m_engine, &LiveStreamEngine::reconnecting,
            this, &LiveMonitorWindow::onReconnecting);
    connect(m_recorder, &LiveRecorder::finished,
            this, &LiveMonitorWindow::onRecordFinished);

    m_statTimer = new QTimer(this);
    m_statTimer->setInterval(1000);
    connect(m_statTimer, &QTimer::timeout, this, &LiveMonitorWindow::refreshStats);
    m_statTimer->start();

    setSessionUi(false);
    showStatus(lang(QStringLiteral("未连接"), QStringLiteral("Disconnected")), StatusTone::Idle);
}

LiveMonitorWindow::~LiveMonitorWindow() = default;

void LiveMonitorWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ---- 接入参数 ----
    auto *connBox = new QGroupBox(lang(QStringLiteral("录像机接入"), QStringLiteral("Recorder")), this);
    auto *connRow = new QHBoxLayout(connBox);
    connRow->setSpacing(6);

    connRow->addWidget(new QLabel(lang(QStringLiteral("地址"), QStringLiteral("Host")), connBox));
    m_hostEdit = new QLineEdit(connBox);
    m_hostEdit->setPlaceholderText(QStringLiteral("192.168.1.108"));
    m_hostEdit->setMinimumWidth(130);
    connRow->addWidget(m_hostEdit);

    connRow->addWidget(new QLabel(lang(QStringLiteral("端口"), QStringLiteral("Port")), connBox));
    m_portSpin = new QSpinBox(connBox);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(554);
    connRow->addWidget(m_portSpin);

    connRow->addWidget(new QLabel(lang(QStringLiteral("用户"), QStringLiteral("User")), connBox));
    m_userEdit = new QLineEdit(connBox);
    m_userEdit->setMinimumWidth(80);
    connRow->addWidget(m_userEdit);

    connRow->addWidget(new QLabel(lang(QStringLiteral("密码"), QStringLiteral("Password")), connBox));
    m_passEdit = new QLineEdit(connBox);
    m_passEdit->setEchoMode(QLineEdit::Password);
    m_passEdit->setMinimumWidth(80);
    connRow->addWidget(m_passEdit);

    connRow->addWidget(new QLabel(lang(QStringLiteral("通道"), QStringLiteral("Ch")), connBox));
    m_channelSpin = new QSpinBox(connBox);
    m_channelSpin->setRange(1, 256);
    m_channelSpin->setValue(1);
    connRow->addWidget(m_channelSpin);

    m_kindCombo = new QComboBox(connBox);
    m_kindCombo->addItem(lang(QStringLiteral("主码流"), QStringLiteral("Main")), 0);
    m_kindCombo->addItem(lang(QStringLiteral("子码流"), QStringLiteral("Sub")), 1);
    connRow->addWidget(m_kindCombo);

    m_transportCombo = new QComboBox(connBox);
    m_transportCombo->addItem(QStringLiteral("TCP"), 0);
    m_transportCombo->addItem(QStringLiteral("UDP"), 1);
    connRow->addWidget(m_transportCombo);

    m_connectBtn = new QPushButton(lang(QStringLiteral("连接"), QStringLiteral("Connect")), connBox);
    m_disconnectBtn = new QPushButton(lang(QStringLiteral("断开"), QStringLiteral("Disconnect")), connBox);
    connRow->addWidget(m_connectBtn);
    connRow->addWidget(m_disconnectBtn);
    connRow->addStretch(1);

    connect(m_connectBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onDisconnectClicked);

    root->addWidget(connBox);

    // ---- 完整地址（可选，覆盖以上参数）----
    auto *urlRow = new QHBoxLayout();
    urlRow->addWidget(new QLabel(
        lang(QStringLiteral("完整地址（可选）"), QStringLiteral("Full URL (optional)")), this));
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(
        QStringLiteral("rtsp://user:pass@host:554/cam/realmonitor?channel=1&subtype=0"));
    urlRow->addWidget(m_urlEdit, 1);
    root->addLayout(urlRow);

    // ---- 工具条 ----
    auto *toolBox = new QGroupBox(lang(QStringLiteral("分析工具与录制"), QStringLiteral("Tools & Recording")), this);
    auto *toolRow = new QHBoxLayout(toolBox);
    toolRow->setSpacing(6);

    m_guideBtn = new QPushButton(lang(QStringLiteral("辅助线"), QStringLiteral("Guide line")), toolBox);
    m_guideBtn->setCheckable(true);
    m_clearGuideBtn = new QPushButton(lang(QStringLiteral("清除辅助线"), QStringLiteral("Clear guides")), toolBox);
    toolRow->addWidget(m_guideBtn);
    toolRow->addWidget(m_clearGuideBtn);

    m_refBtn = new QPushButton(lang(QStringLiteral("参考图…"), QStringLiteral("Ref image...")), toolBox);
    m_captureRefBtn = new QPushButton(lang(QStringLiteral("当前帧为参考"), QStringLiteral("Use frame")), toolBox);
    m_showRefBtn = new QPushButton(lang(QStringLiteral("显示参考图"), QStringLiteral("Show ref")), toolBox);
    m_clearRefBtn = new QPushButton(lang(QStringLiteral("清除参考图"), QStringLiteral("Clear ref")), toolBox);
    toolRow->addWidget(m_refBtn);
    toolRow->addWidget(m_captureRefBtn);
    toolRow->addWidget(m_showRefBtn);
    toolRow->addWidget(m_clearRefBtn);

    toolRow->addSpacing(12);
    m_recordBtn = new QPushButton(lang(QStringLiteral("● 开始录制"), QStringLiteral("● Record")), toolBox);
    m_containerCombo = new QComboBox(toolBox);
    m_containerCombo->addItem(QStringLiteral("MP4"), QStringLiteral("mp4"));
    m_containerCombo->addItem(QStringLiteral("MKV"), QStringLiteral("mkv"));
    m_dirBtn = new QPushButton(lang(QStringLiteral("目录…"), QStringLiteral("Folder...")), toolBox);
    toolRow->addWidget(m_recordBtn);
    toolRow->addWidget(m_containerCombo);
    toolRow->addWidget(m_dirBtn);
    toolRow->addStretch(1);

    m_statusLabel = new QLabel(this);
    m_statLabel = new QLabel(this);
    toolRow->addWidget(m_statusLabel);
    toolRow->addWidget(m_statLabel);

    root->addWidget(toolBox);

    // ---- 画面 ----
    m_video = new VideoWidget(this);
    m_roiModel = new RoiModel(this);
    m_guideLineModel = new GuideLineModel(this);
    m_video->setRegionModel(m_roiModel);
    m_video->setPolygonModel(m_roiModel);
    m_video->setGuideLineModel(m_guideLineModel);
    root->addWidget(m_video, 1);

    m_snapshot = new SnapshotOverlay(m_video);
    m_snapshot->hide();

    // ---- 接线 ----
    connect(m_guideBtn, &QPushButton::toggled, this, &LiveMonitorWindow::onGuideLineToggled);
    connect(m_clearGuideBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onClearGuideLines);
    connect(m_refBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onLoadReferenceImage);
    connect(m_captureRefBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onCaptureReference);
    connect(m_showRefBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onToggleReferenceVisible);
    connect(m_clearRefBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onClearReference);
    connect(m_recordBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onToggleRecord);
    connect(m_dirBtn, &QPushButton::clicked, this, &LiveMonitorWindow::onChooseRecordDir);

    connect(m_snapshot, &SnapshotOverlay::snapshotChanged,
            this, &LiveMonitorWindow::applySnapshotToVideo);
    connect(m_snapshot, &SnapshotOverlay::placeToggled, this, [this](bool active) {
        if (active)
            applySnapshotToVideo();
        else
            m_video->clearSnapshot();
    });
    connect(m_video, &VideoWidget::frameSnapshotReady, this, [this](const QImage &img) {
        m_snapshot->setSnapshot(img);
        m_snapshot->show();
        applySnapshotToVideo();
    });
}

void LiveMonitorWindow::loadSettings()
{
    QSettings s(kSettingsGroup, kSettingsApp);
    m_hostEdit->setText(s.value(QStringLiteral("live/host"),
                               QStringLiteral("192.168.1.108")).toString());   // 大华出厂默认 IP
    m_portSpin->setValue(s.value(QStringLiteral("live/port"), 554).toInt());
    m_userEdit->setText(s.value(QStringLiteral("live/user"), QStringLiteral("admin")).toString());
    m_channelSpin->setValue(s.value(QStringLiteral("live/channel"), 1).toInt());
    m_channelSpin->setToolTip(lang(QStringLiteral("DH-NVR2208-S1 为 8 路，通道 1–8"),
                                   QStringLiteral("DH-NVR2208-S1 has 8 channels (1-8)")));
    m_hostEdit->setToolTip(lang(QStringLiteral("直连时请把本机网卡设为 192.168.1.x/24（录像机默认 192.168.1.108）"),
                                QStringLiteral("For a direct cable link, set this PC's NIC to 192.168.1.x/24 (NVR default 192.168.1.108)")));
    m_kindCombo->setCurrentIndex(qBound(0, s.value(QStringLiteral("live/kind"), 0).toInt(), 1));
    m_transportCombo->setCurrentIndex(qBound(0, s.value(QStringLiteral("live/transport"), 0).toInt(), 1));
    m_containerCombo->setCurrentIndex(qBound(0, s.value(QStringLiteral("live/container"), 0).toInt(), 1));
    // 密码不落盘（取证工具的凭证不写明文配置）
}

void LiveMonitorWindow::saveSettings() const
{
    QSettings s(kSettingsGroup, kSettingsApp);
    s.setValue(QStringLiteral("live/host"), m_hostEdit->text().trimmed());
    s.setValue(QStringLiteral("live/port"), m_portSpin->value());
    s.setValue(QStringLiteral("live/user"), m_userEdit->text());
    s.setValue(QStringLiteral("live/channel"), m_channelSpin->value());
    s.setValue(QStringLiteral("live/kind"), m_kindCombo->currentIndex());
    s.setValue(QStringLiteral("live/transport"), m_transportCombo->currentIndex());
    s.setValue(QStringLiteral("live/container"), m_containerCombo->currentIndex());
}

live::LiveSourceConfig LiveMonitorWindow::currentConfig() const
{
    live::LiveSourceConfig cfg;
    cfg.host = m_hostEdit->text().trimmed();
    cfg.port = m_portSpin->value();
    cfg.username = m_userEdit->text();
    cfg.password = m_passEdit->text();
    cfg.channel = m_channelSpin->value();
    cfg.kind = m_kindCombo->currentIndex() == 1 ? live::StreamKind::Sub
                                                : live::StreamKind::Main;
    cfg.transport = m_transportCombo->currentIndex() == 1 ? live::Transport::Udp
                                                          : live::Transport::Tcp;
    cfg.urlOverride = m_urlEdit->text().trimmed();
    return cfg;
}

void LiveMonitorWindow::onConnectClicked()
{
    if (m_active)
        return;
    const live::LiveSourceConfig cfg = currentConfig();
    if (!cfg.isComplete()) {
        QMessageBox::warning(this, lang(QStringLiteral("接入监控"), QStringLiteral("Live Monitor")),
            lang(QStringLiteral("请填写录像机地址与用户名，或填写完整地址。"),
                 QStringLiteral("Enter the recorder host and user, or a full URL.")));
        return;
    }
    const QString url = live::resolveStreamUrl(cfg);
    if (url.isEmpty()) {
        QMessageBox::warning(this, lang(QStringLiteral("接入监控"), QStringLiteral("Live Monitor")),
            lang(QStringLiteral("直播地址无效。"), QStringLiteral("Invalid stream URL.")));
        return;
    }

    saveSettings();
    m_errorShown = false;
    m_everConnected = false;   // 评审 N5：新会话重新计起
    m_active = true;
    setSessionUi(true);
    showStatus(lang(QStringLiteral("正在连接…"), QStringLiteral("Connecting...")),
               StatusTone::Idle);

    m_video->setLoading(true);
    connect(m_engine, &LiveStreamEngine::frameReady, this,
            [this](const QImage &) { m_video->setLoading(false); },
            Qt::SingleShotConnection);

    m_engine->setTransportTcp(cfg.transport == live::Transport::Tcp);
    m_engine->load(url);
    m_engine->play();
}

void LiveMonitorWindow::onDisconnectClicked()
{
    if (m_recorder->isRecording())
        m_recorder->stop();
    // 评审 N1：断开走非阻塞 stop()（仅置标志 + 唤醒），不在此同步 join 工作线程，
    // 避免 worker 卡死时阻塞 UI 数秒；线程的最终回收在窗口析构的 unload() 完成。
    // stop() 已置 m_started=false + m_abort=true，readLoop 会立即退出并释放 RTSP 连接。
    m_engine->stop();
    m_active = false;
    m_everConnected = false;
    m_video->setLoading(false);
    m_video->clearFrame();
    setSessionUi(false);
    showStatus(lang(QStringLiteral("已断开"), QStringLiteral("Disconnected")), StatusTone::Idle);
}

void LiveMonitorWindow::onStreamStarted()
{
    m_active = true;
    m_everConnected = true;   // 评审 N5：此后失败才算“断线重连”
    setSessionUi(true);
    m_errorShown = false;
    showStatus(lang(QStringLiteral("直播中"), QStringLiteral("Live")), StatusTone::Ok);
    refreshStats();
}

void LiveMonitorWindow::onStreamError(const QString &message)
{
    // 评审 N2：失败必须停掉“导入中…”旋转动画，否则用户误以为仍在连接
    if (m_video)
        m_video->setLoading(false);
    showStatus(lang(QStringLiteral("连接失败"), QStringLiteral("Connection failed")),
               StatusTone::Bad);
    if (m_errorShown)
        return;
    m_errorShown = true;

    // 现场最常见的是"网线直连但本机没配 IP"——网关不存在，任何 RTSP 都连不上。
    // 这里把排查步骤直接摆在错误框里，避免用户反复重试。
    const QString hint = lang(
        QStringLiteral("\n\n排查建议：\n"
                       "· 网线直连电脑时，把本机网卡设为 192.168.1.x/24（如 192.168.1.100）——\n"
                       "  录像机默认 192.168.1.108，直连链路没有 DHCP 服务器；\n"
                       "· 先在命令行 ping 通录像机，并确认 RTSP 服务已开启（默认端口 554）；\n"
                       "· 账号需具备远程预览权限（默认常为 admin）；\n"
                       "· 大华取流格式：rtsp://用户:密码@IP:554/cam/realmonitor?channel=通道&subtype=0"),
        QStringLiteral("\n\nTroubleshooting:\n"
                       "- For a direct cable link, set this PC's NIC to 192.168.1.x/24 (e.g. 192.168.1.100);\n"
                       "  the NVR defaults to 192.168.1.108 and a direct link has no DHCP;\n"
                       "- Ping the NVR first and make sure RTSP (port 554) is enabled;\n"
                       "- The account needs remote-preview permission (usually admin);\n"
                       "- Dahua URL form: rtsp://user:pass@IP:554/cam/realmonitor?channel=N&subtype=0"));

    QMessageBox::warning(this, lang(QStringLiteral("接入监控"), QStringLiteral("Live Monitor")),
                         message + hint);
}

void LiveMonitorWindow::onReconnecting(int attempt)
{
    // 评审 N5：首次连接失败 ≠ 断线重连，文案与色调分开
    if (m_everConnected) {
        showStatus(lang(QStringLiteral("连接中断，正在重连（第 %1 次）…"),
                        QStringLiteral("Reconnecting (attempt %1)...")).arg(attempt),
                   StatusTone::Bad);
    } else {
        showStatus(lang(QStringLiteral("正在连接（第 %1 次尝试）…"),
                        QStringLiteral("Connecting (attempt %1)...")).arg(attempt),
                   StatusTone::Idle);
    }
}

void LiveMonitorWindow::setSessionUi(bool active)
{
    m_connectBtn->setEnabled(!active);
    m_disconnectBtn->setEnabled(active);
    m_hostEdit->setEnabled(!active);
    m_portSpin->setEnabled(!active);
    m_userEdit->setEnabled(!active);
    m_passEdit->setEnabled(!active);
    m_channelSpin->setEnabled(!active);
    m_kindCombo->setEnabled(!active);
    m_transportCombo->setEnabled(!active);
    m_urlEdit->setEnabled(!active);
    if (!active && m_guideBtn->isChecked()) {
        m_guideBtn->setChecked(false);
    }
}

void LiveMonitorWindow::showStatus(const QString &text, StatusTone tone)
{
    if (!m_statusLabel)
        return;
    // 评审 N3：色调由调用方显式指定，不再由 m_active 推导
    QString color = QStringLiteral("#8a8a8a");            // Idle：中性灰
    if (tone == StatusTone::Ok)
        color = QStringLiteral("#2d8f47");                // 成功：绿
    else if (tone == StatusTone::Bad)
        color = QStringLiteral("#b03030");                // 失败/重连：红
    m_statusLabel->setText(QStringLiteral("  %1  ").arg(text));
    m_statusLabel->setStyleSheet(QStringLiteral("color:%1; font-weight:bold;").arg(color));
}

void LiveMonitorWindow::refreshStats()
{
    if (!m_statLabel || !m_engine)
        return;
    const int w = m_engine->videoWidth();
    const int h = m_engine->videoHeight();
    QString s = (w > 0 && h > 0) ? QStringLiteral("%1×%2").arg(w).arg(h)
                                 : QStringLiteral("—");
    const float fps = m_engine->fps();
    if (fps > 0.0f)
        s += QStringLiteral(" · %1 fps").arg(fps, 0, 'f', 1);
    const qint64 posMs = m_engine->position();
    if (m_active && posMs > 0)
        s += QStringLiteral(" · %1").arg(formatDuration(posMs));
    m_statLabel->setText(s);
}

// ---------------------------------------------------------------------------
// 辅助线
// ---------------------------------------------------------------------------

void LiveMonitorWindow::onGuideLineToggled(bool on)
{
    if (!m_video || !m_video->overlay())
        return;
    m_video->overlay()->setGuideLineMode(on);
    showStatus(on ? lang(QStringLiteral("辅助线：在画面上拖拽绘制"),
                         QStringLiteral("Guide line: drag on the picture"))
                  : (m_active ? lang(QStringLiteral("直播中"), QStringLiteral("Live"))
                              : lang(QStringLiteral("未连接"), QStringLiteral("Disconnected"))),
               on || m_active ? StatusTone::Ok : StatusTone::Idle);
}

void LiveMonitorWindow::onClearGuideLines()
{
    if (m_guideLineModel)
        m_guideLineModel->clearLines();
}

// ---------------------------------------------------------------------------
// 截图叠加（参考图）
// ---------------------------------------------------------------------------

void LiveMonitorWindow::onLoadReferenceImage()
{
    const QString path = QFileDialog::getOpenFileName(this,
        lang(QStringLiteral("选择参考图"), QStringLiteral("Choose reference image")), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tiff *.tif);;All Files (*)"));
    if (path.isEmpty())
        return;
    const QImage img(path);
    if (img.isNull()) {
        QMessageBox::warning(this, lang(QStringLiteral("加载失败"), QStringLiteral("Load failed")),
            lang(QStringLiteral("无法加载该图片文件。"), QStringLiteral("Failed to load the image.")));
        return;
    }
    m_snapshot->setSnapshot(img);
    m_snapshot->show();
    applySnapshotToVideo();
}

void LiveMonitorWindow::onCaptureReference()
{
    if (m_video)
        m_video->grabFrameSnapshot();   // 经 frameSnapshotReady 落入参考图
}

void LiveMonitorWindow::onToggleReferenceVisible()
{
    if (!m_snapshot->hasSnapshot()) {
        QMessageBox::information(this, lang(QStringLiteral("参考图"), QStringLiteral("Reference")),
            lang(QStringLiteral("尚未加载参考图。"), QStringLiteral("No reference image loaded.")));
        return;
    }
    m_snapshot->setVisible(!m_snapshot->isVisible());
}

void LiveMonitorWindow::onClearReference()
{
    m_snapshot->clearSnapshot();
    m_snapshot->hide();
    if (m_video)
        m_video->clearSnapshot();
}

void LiveMonitorWindow::applySnapshotToVideo()
{
    if (!m_video || !m_snapshot)
        return;
    if (m_snapshot->hasSnapshot()) {
        m_video->setSnapshot(m_snapshot->snapshotImage(),
                             m_snapshot->brightness(),
                             m_snapshot->contrastValue(),
                             m_snapshot->opacityValue());
    } else {
        m_video->clearSnapshot();
    }
}

// ---------------------------------------------------------------------------
// 录制
// ---------------------------------------------------------------------------

void LiveMonitorWindow::onToggleRecord()
{
    if (m_recorder->isRecording()) {
        m_recordBtn->setEnabled(false);
        showStatus(lang(QStringLiteral("正在收尾保存录像…"),
                        QStringLiteral("Finalizing recording...")));
        m_recorder->stop();
        return;
    }

    if (!m_active) {
        QMessageBox::information(this, lang(QStringLiteral("录制"), QStringLiteral("Record")),
            lang(QStringLiteral("请先连接直播再开始录制。"),
                 QStringLiteral("Connect to the live stream first.")));
        return;
    }

    const live::LiveSourceConfig cfg = currentConfig();
    const QString url = live::resolveStreamUrl(cfg);
    if (url.isEmpty()) {
        QMessageBox::warning(this, lang(QStringLiteral("录制"), QStringLiteral("Record")),
            lang(QStringLiteral("直播地址无效。"), QStringLiteral("Invalid stream URL.")));
        return;
    }

    QSettings s(kSettingsGroup, kSettingsApp);
    const QString dir = s.value(QStringLiteral("live/recordDir"),
                                LiveRecorder::defaultRecordDir()).toString();
    const QString container = m_containerCombo->currentData().toString();
    const QString path = QDir(dir).filePath(
        LiveRecorder::suggestFileName(cfg.host, cfg.channel, container));

    QString err;
    if (!m_recorder->start(url, path, cfg.transport == live::Transport::Tcp, &err)) {
        QMessageBox::critical(this, lang(QStringLiteral("录制失败"), QStringLiteral("Recording failed")),
                              err);
        return;
    }
    m_recordBtn->setText(lang(QStringLiteral("■ 停止录制"), QStringLiteral("■ Stop")));
    showStatus(lang(QStringLiteral("录制中：%1").arg(path),
                    QStringLiteral("Recording: %1").arg(path)));
}

void LiveMonitorWindow::onRecordFinished(const QString &path, bool ok, const QString &message)
{
    m_recordBtn->setText(lang(QStringLiteral("● 开始录制"), QStringLiteral("● Record")));
    m_recordBtn->setEnabled(true);
    if (ok) {
        showStatus(lang(QStringLiteral("录像已保存"), QStringLiteral("Recording saved")),
                   StatusTone::Ok);
        QMessageBox::information(this, lang(QStringLiteral("录制完成"), QStringLiteral("Recording done")),
                                 message);
    } else {
        showStatus(lang(QStringLiteral("录制失败"), QStringLiteral("Recording failed")),
                   StatusTone::Bad);
        QMessageBox::warning(this, lang(QStringLiteral("录制失败"), QStringLiteral("Recording failed")),
                             QStringLiteral("%1\n%2").arg(message, path));
    }
}

void LiveMonitorWindow::onChooseRecordDir()
{
    QSettings s(kSettingsGroup, kSettingsApp);
    const QString cur = s.value(QStringLiteral("live/recordDir"),
                                LiveRecorder::defaultRecordDir()).toString();
    const QString dir = QFileDialog::getExistingDirectory(
        this, lang(QStringLiteral("选择录像保存目录"), QStringLiteral("Choose recording folder")), cur);
    if (dir.isEmpty())
        return;
    s.setValue(QStringLiteral("live/recordDir"), dir);
    showStatus(lang(QStringLiteral("录像目录：%1").arg(dir),
                    QStringLiteral("Recording folder: %1").arg(dir)), StatusTone::Idle);
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

void LiveMonitorWindow::closeEvent(QCloseEvent *event)
{
    if (m_recorder && m_recorder->isRecording()) {
        const auto reply = QMessageBox::question(this,
            lang(QStringLiteral("正在录制"), QStringLiteral("Recording in progress")),
            lang(QStringLiteral("录像仍在进行，关闭窗口会停止录像。确定关闭吗？"),
                 QStringLiteral("Recording is in progress; closing will stop it. Close anyway?")),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_recorder->stop();
    }
    // 评审 N1：此处同样不做同步 join（窗口随后析构，由 ~LiveMonitorWindow 回收线程）
    if (m_engine)
        m_engine->stop();
    event->accept();
}

void LiveMonitorWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_snapshot && m_video) {
        m_snapshot->adjustSize();
        m_snapshot->move(qMax(0, m_video->width() - m_snapshot->width() - 12), 12);
    }
}
