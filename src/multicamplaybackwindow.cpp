/**
 * @file multicamplaybackwindow.cpp
 * @brief 多机同步播放独立窗口实现（P-57）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-18
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "multicamplaybackwindow.h"

#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QCloseEvent>

#include "app/cam_timeline.h"
#include "app/case_manager.h"
#include "camtilewidget.h"
#include "multicamview.h"
#include "i18n.h"
#include "theme.h"
#include "infrastructure/ivideo_engine.h"

namespace {
constexpr float kRates[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
constexpr int kRateCount = 6;
} // namespace

MultiCamPlaybackWindow::MultiCamPlaybackWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lang("多机同步播放", "Multi-camera Synced Playback"));
    setMinimumSize(900, 560);
    m_svc = new MultiCamSyncService(this);
    connect(m_svc, &MultiCamSyncService::stateChanged,
            this, &MultiCamPlaybackWindow::onServiceState);
    connect(m_svc, &MultiCamSyncService::clockChanged,
            this, &MultiCamPlaybackWindow::onClock);
    connect(m_svc, &MultiCamSyncService::laneInfoChanged,
            this, &MultiCamPlaybackWindow::onLaneInfo);
    connect(m_svc, &MultiCamSyncService::laneLoadFailed,
            this, &MultiCamPlaybackWindow::onLaneFailed);
    connect(m_svc, &MultiCamSyncService::performanceNotice,
            this, &MultiCamPlaybackWindow::onPerfNotice);
    buildUi();
}

MultiCamPlaybackWindow::~MultiCamPlaybackWindow()
{
    m_svc->closeAll();
}

void MultiCamPlaybackWindow::setEngineFactory(
    MultiCamSyncService::EngineFactory f)
{
    m_factory = f;
    m_svc->setEngineFactory(std::move(f));
}

void MultiCamPlaybackWindow::closeEvent(QCloseEvent *event)
{
    m_svc->closeAll();   // 关窗即释放全部引擎（线程/句柄有界，C5）
    QDialog::closeEvent(event);
}

// ---------------------------------------------------------------------------
// UI 骨架
// ---------------------------------------------------------------------------
void MultiCamPlaybackWindow::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // 工具行：播放/暂停 · 回起点 · 倍速 · OSD 开关 · 状态
    auto *bar = new QHBoxLayout();
    m_playBtn = new QPushButton(lang("▶ 播放", "▶ Play"), this);
    m_playBtn->setEnabled(false);
    connect(m_playBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onTogglePlay);
    bar->addWidget(m_playBtn);

    auto *backBtn = new QPushButton(lang("⏮ 回起点", "⏮ Start"), this);
    connect(backBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onBackToStart);
    bar->addWidget(backBtn);

    m_rateBtn = new QPushButton(QStringLiteral("1x"), this);
    m_rateBtn->setToolTip(lang("倍速播放 (0.25x/0.5x/1x/2x/4x/8x)",
                               "Playback speed (0.25x/0.5x/1x/2x/4x/8x)"));
    connect(m_rateBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onCycleRate);
    bar->addWidget(m_rateBtn);

    m_osdBtn = new QPushButton(lang("OSD 开", "OSD On"), this);
    m_osdBtn->setCheckable(true);
    m_osdBtn->setChecked(true);
    connect(m_osdBtn, &QPushButton::toggled, this, [this](bool on) {
        m_osdOn = on;
        m_osdBtn->setText(on ? lang("OSD 开", "OSD On")
                             : lang("OSD 关", "OSD Off"));
        for (auto *t : m_tiles)
            if (t)
                t->setOsdVisible(on);
    });
    bar->addWidget(m_osdBtn);

    m_alignBtn = new QPushButton(lang("对齐…", "Align…"), this);
    m_alignBtn->setVisible(false);
    connect(m_alignBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEnterAlign);
    bar->addWidget(m_alignBtn);
    m_alignOkBtn = new QPushButton(lang("确认对齐", "Apply"), this);
    m_alignOkBtn->setVisible(false);
    connect(m_alignOkBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onConfirmAlign);
    bar->addWidget(m_alignOkBtn);
    m_alignCancelBtn = new QPushButton(lang("取消", "Cancel"), this);
    m_alignCancelBtn->setVisible(false);
    connect(m_alignCancelBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onCancelAlign);
    bar->addWidget(m_alignCancelBtn);

    bar->addStretch(1);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(
        QStringLiteral("color:%1;").arg(Theme::TextSecond));
    bar->addWidget(m_statusLabel);
    root->addLayout(bar);

    // 瓦片网格
    auto *gridHost = new QWidget(this);
    m_grid = new QGridLayout(gridHost);
    m_grid->setSpacing(6);
    root->addWidget(gridHost, 1);

    // 底部时间线宿主（模式A 合并条 / 模式B 每路进度条）
    m_timelineHost = new QWidget(this);
    root->addWidget(m_timelineHost, 0);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(
        QStringLiteral("color:%1;").arg(Theme::TextMuted));
    root->addWidget(m_hintLabel);
}

// ---------------------------------------------------------------------------
// 入口装配
// ---------------------------------------------------------------------------
bool MultiCamPlaybackWindow::openCaseLanes(const CaseManager &cm)
{
    QVector<SyncLaneData> lanes = buildSyncLanesFromCase(cm);
    if (lanes.isEmpty()) {
        m_hintLabel->setText(lang("案内无已校时视频。请先对至少一路校时。",
                                  "No calibrated video in case."));
        return false;
    }
    if (lanes.size() >= 2) {
        m_mode = SyncTimelineMode::Merged;   // 全部已校时 → 合并时间线
        if (lanes.size() > 4) {
            lanes.resize(4);   // 拍板 2-4 路：超出截断并提示（C2 不静默）
            m_hintLabel->setText(
                lang("已校时路超过 4 路，本窗口仅同步前 4 路。",
                     "More than 4 calibrated cameras; only first 4 synced."));
        }
        if (!m_svc->loadLanes(lanes))
            return false;
        rebuildTiles();
        rebuildTimelineArea();
        refreshModeControls();
        return true;
    }
    // 恰 1 路已校时 → 模式B：该校时路 + 一个临时进槽位
    m_mode = SyncTimelineMode::Separate;
    m_tempSlot = 1;
    SyncLaneData temp;
    temp.id = QStringLiteral("T1");
    temp.temporary = true;
    lanes.append(temp);
    if (!m_svc->loadLanes(lanes))   // 临时路 path 空 → 引擎加载失败面，
        return false;               // 由槽位选择后重载（见 onPickVideo）
    rebuildTiles();
    rebuildTimelineArea();
    refreshModeControls();
    return true;
}

void MultiCamPlaybackWindow::openStandalone()
{
    m_mode = SyncTimelineMode::Separate;
    m_tempSlot = 0;   // 两路皆临时（先选先为参考路）
    rebuildTiles();
    rebuildTimelineArea();
    refreshModeControls();
    m_hintLabel->setText(
        lang("独立模式：分别为两路选择视频后开始。",
             "Standalone: pick a video for each lane to start."));
}

// ---------------------------------------------------------------------------
// 瓦片 / 时间线区域
// ---------------------------------------------------------------------------
void MultiCamPlaybackWindow::rebuildTiles()
{
    // 清空旧瓦片
    QLayoutItem *item;
    while ((item = m_grid->takeAt(0)) != nullptr) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_tiles.clear();

    const int n = qMax(2, m_svc->laneCount());
    const int cols = (n <= 2) ? n : 2;
    for (int i = 0; i < n; ++i) {
        auto *tile = new CamTileWidget(this);
        tile->setOsdVisible(m_osdOn);
        tile->setAudible(i == m_svc->audibleLane());
        const int idx = i;
        connect(tile, &CamTileWidget::clicked, this, [this, idx]() {
            // 空槽位点击 = 选视频；已载路点击 = 切听（U-2）
            if (idx >= m_svc->laneCount()
                || m_svc->lanes()[idx].path.isEmpty()) {
                onPickVideo(idx);
                return;
            }
            m_svc->setAudibleLane(idx);
            for (int k = 0; k < m_tiles.size(); ++k)
                m_tiles[k]->setAudible(k == idx);
        });
        connect(tile, &CamTileWidget::openRequested, this, [this, idx]() {
            if (onOpenVideo && idx < m_svc->laneCount()
                && !m_svc->lanes()[idx].path.isEmpty())
                onOpenVideo(m_svc->lanes()[idx].path);
        });
        if (i < m_svc->laneCount() && !m_svc->lanes()[i].path.isEmpty())
            tile->setLaneName(m_svc->lanes()[i].displayName);
        // 帧通道：引擎在 loadLanes 时已创建（加载中亦可接，frameReady 异步到）
        tile->setEngine(m_svc->engineAt(i));
        m_tiles.append(tile);
        m_grid->addWidget(tile, i / cols, i % cols);
    }
    updateTilesOsd();
}

QVector<CamLane> MultiCamPlaybackWindow::currentCamLanes() const
{
    QVector<CamLane> out;
    for (const auto &l : m_svc->lanes()) {
        if (!l.calibrated || l.durationMs <= 0)
            continue;
        CamLane c;
        c.videoId = l.id;
        c.fileName = l.displayName;
        c.wallStartMs = syncLaneWallStart(l);
        c.wallEndMs = syncLaneWallEnd(l);
        c.streamDurationMs = l.durationMs;
        out.append(c);
    }
    return out;
}

void MultiCamPlaybackWindow::rebuildTimelineArea()
{
    delete m_timelineHost->layout();
    m_bars.clear();
    m_barLabels.clear();
    m_mergedBar = nullptr;

    auto *lay = new QVBoxLayout(m_timelineHost);
    lay->setContentsMargins(0, 0, 0, 0);

    if (m_mode == SyncTimelineMode::Merged) {
        m_mergedBar = new MultiCamViewWidget(m_timelineHost);
        m_mergedBar->setLanes(currentCamLanes());
        connect(m_mergedBar, &MultiCamViewWidget::scrubPreview, this,
                [this](qint64 wallMs) {
                    if (m_svc->state() == MultiCamSyncService::State::Playing
                        || m_svc->state() == MultiCamSyncService::State::Paused
                        || m_svc->state() == MultiCamSyncService::State::Ready) {
                        m_svc->beginScrub();
                        m_svc->scrubTo(wallMs);
                    }
                });
        connect(m_mergedBar, &MultiCamViewWidget::seekCommit, this,
                [this](qint64 wallMs) {
                    Q_UNUSED(wallMs);
                    m_svc->endScrub();
                });
        connect(m_mergedBar, &MultiCamViewWidget::laneActivated, this,
                [this](const QString &id) {
                    if (!onOpenVideo)
                        return;
                    for (const auto &l : m_svc->lanes())
                        if (l.id == id) {
                            onOpenVideo(l.path);
                            return;
                        }
                });
        lay->addWidget(m_mergedBar);
        m_hintLabel->setText(
            lang("块 = 该机位墙钟覆盖 · 红 = ≥2 机位重叠 · 灰纹 = 缺口 · "
                 "拖游标同步走带 · 双击块/瓦片回单路分析",
                 "Block = wall-clock coverage · red = overlap · hatch = gap · "
                 "drag cursor to scrub · double-click to open lane"));
    } else {
        // 模式B：每路一条带时间进度条（无分析面板）
        const int n = qMax(2, m_svc->laneCount());
        for (int i = 0; i < n; ++i) {
            auto *row = new QHBoxLayout();
            auto *name = new QLabel(m_timelineHost);
            name->setMinimumWidth(140);
            if (i < m_svc->laneCount()
                && !m_svc->lanes()[i].path.isEmpty())
                name->setText(m_svc->lanes()[i].displayName);
            else
                name->setText(lang("路 %1（未选）", "Lane %1 (empty)")
                                  .arg(i + 1));
            auto *slider = new QSlider(Qt::Horizontal, m_timelineHost);
            slider->setEnabled(false);
            auto *time = new QLabel(QStringLiteral("--:-- / --:--"),
                                    m_timelineHost);
            time->setStyleSheet(
                QStringLiteral("color:%1;").arg(Theme::TextSecond));
            row->addWidget(name);
            row->addWidget(slider, 1);
            row->addWidget(time);
            lay->addLayout(row);
            m_bars.append(slider);
            m_barLabels.append(time);

            const int idx = i;
            // 拖动 = 同步语义 seek（经该路映射回墙钟轴）；对齐模式下例外
            connect(slider, &QSlider::sliderPressed, this, [this, idx]() {
                if (m_aligning || idx >= m_svc->laneCount())
                    return;
                m_svc->beginScrub();
                m_bars[idx]->setProperty("scrub", 1);
            });
            connect(slider, &QSlider::sliderMoved, this,
                    [this, idx](int v) {
                        if (m_aligning)
                            return;   // 对齐模式：barMovedTo 单独处理
                        if (idx >= m_svc->laneCount())
                            return;
                        m_svc->scrubTo(syncWallOf(m_svc->lanes()[idx], v));
                    });
            connect(slider, &QSlider::sliderReleased, this, [this, idx]() {
                if (m_aligning) {
                    if (idx < m_svc->laneCount()) {
                        if (auto *e = m_svc->engineAt(idx))
                            e->seek(m_bars[idx]->value());
                    }
                    return;
                }
                if (m_bars[idx]->property("scrub").toInt() == 1) {
                    m_bars[idx]->setProperty("scrub", 0);
                    m_svc->endScrub();
                }
            });
            // 对齐模式下单路独立拖动（sliderMoved 在拖动时连续发）
            connect(slider, &QSlider::valueChanged, this,
                    [this, idx](int v) {
                        if (!m_aligning || idx >= m_svc->laneCount())
                            return;
                        if (auto *e = m_svc->engineAt(idx))
                            e->seek(v);
                    });
        }
    }
}

void MultiCamPlaybackWindow::refreshModeControls()
{
    const bool sep = (m_mode == SyncTimelineMode::Separate);
    // 对齐按钮：模式B 且两路都已加载
    const bool canAlign = sep && lanesLoaded();
    m_alignBtn->setVisible(sep && !m_aligning && canAlign);
    m_alignOkBtn->setVisible(m_aligning);
    m_alignCancelBtn->setVisible(m_aligning);
    if (sep) {
        m_hintLabel->setText(
            m_aligning
                ? lang("对齐模式：分别拖动两路进度条到同一时刻（可用声音"
                       "比对，点瓦片切听），然后点「确认对齐」。",
                       "Aligning: drag both lanes to the same moment "
                       "(use audio cues, click a tile to listen), then Apply.")
                : lang("模式B：各路口径为流内时间；临时路对齐后联动。",
                       "Separate bars: per-lane stream time; temp lane "
                       "syncs after alignment."));
    }
}

bool MultiCamPlaybackWindow::lanesLoaded() const
{
    if (m_svc->laneCount() == 0)
        return false;
    for (const auto &l : m_svc->lanes())
        if (l.path.isEmpty() || l.durationMs <= 0)
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// 槽位选视频（临时进 / 独立模式）
// ---------------------------------------------------------------------------
void MultiCamPlaybackWindow::onPickVideo(int slot)
{
    const QString path = QFileDialog::getOpenFileName(
        this, lang("选择视频", "Pick video"), QString(),
        lang("视频文件 (*.mp4 *.avi *.mkv *.mov);;所有文件 (*)",
             "Video files (*.mp4 *.avi *.mkv *.mov);;All files (*)"));
    if (path.isEmpty())
        return;
    pickVideoForSlot(slot, path);
}

void MultiCamPlaybackWindow::pickVideoForSlot(int slot, const QString &path)
{
    QVector<SyncLaneData> lanes;
    if (m_svc->laneCount() > 0)
        lanes = m_svc->lanes();
    while (lanes.size() <= slot)
        lanes.append(SyncLaneData{});
    SyncLaneData &l = lanes[slot];
    l.id = l.calibrated ? l.id
                        : QStringLiteral("T%1").arg(slot + 1);
    l.path = path;
    l.displayName = QFileInfo(path).fileName();
    if (!l.calibrated)
        l.temporary = true;
    l.durationMs = 0;
    m_svc->loadLanes(lanes);   // 全量重载（v1：换路=重开，语义简单可靠）
    rebuildTiles();
    rebuildTimelineArea();
    refreshModeControls();
}

// ---------------------------------------------------------------------------
// 对齐会话（模式B）
// ---------------------------------------------------------------------------
void MultiCamPlaybackWindow::onEnterAlign()
{
    if (m_svc->laneCount() != 2)
        return;
    m_aligning = true;
    m_savedOffsetBeforeAlign = m_svc->lanes()[1].temporary
        ? m_svc->lanes()[1].tempOffsetMs : m_svc->lanes()[0].tempOffsetMs;
    m_svc->pause();
    // 对齐期进度条独立：直接 seek 各引擎（不经同步层）
    for (int i = 0; i < m_bars.size(); ++i)
        m_bars[i]->setEnabled(true);
    refreshModeControls();
}

void MultiCamPlaybackWindow::onConfirmAlign()
{
    if (!m_aligning || m_svc->laneCount() != 2)
        return;
    auto *e0 = m_svc->engineAt(0);
    auto *e1 = m_svc->engineAt(1);
    if (!e0 || !e1)
        return;
    const auto &lanes = m_svc->lanes();
    const int tempIdx = lanes[1].temporary ? 1 : 0;
    const int refIdx = 1 - tempIdx;
    const qint64 refPos = (refIdx == 0 ? e0 : e1)->position();
    const qint64 tempPos = (tempIdx == 0 ? e0 : e1)->position();
    // 参考路当前位置的墙钟 = 临时路当前位置应对齐到的墙钟
    const qint64 offset = syncWallOf(lanes[refIdx], refPos) - tempPos;
    m_svc->setLaneOffsetMs(tempIdx, offset);
    m_aligning = false;
    refreshModeControls();
}

void MultiCamPlaybackWindow::onCancelAlign()
{
    if (!m_aligning)
        return;
    const auto &lanes = m_svc->lanes();
    if (lanes.size() != 2 || (!lanes[0].temporary && !lanes[1].temporary)) {
        m_aligning = false;   // 无临时路（不应到达）：安全退出
        refreshModeControls();
        return;
    }
    const int tempIdx = lanes[1].temporary ? 1 : 0;
    m_svc->setLaneOffsetMs(tempIdx, m_savedOffsetBeforeAlign);
    m_aligning = false;
    refreshModeControls();
}

// ---------------------------------------------------------------------------
// 播放控制
// ---------------------------------------------------------------------------
void MultiCamPlaybackWindow::onTogglePlay()
{
    m_svc->togglePlay();
}

void MultiCamPlaybackWindow::onCycleRate()
{
    int k = 0;
    float best = 1e9f;
    for (int i = 0; i < kRateCount; ++i) {
        const float d = qAbs(kRates[i] - m_rate);
        if (d < best) { best = d; k = i; }
    }
    k = (k + 1) % kRateCount;
    m_rate = kRates[k];
    m_svc->setRate(m_rate);
    m_rateBtn->setText(QStringLiteral("%1x").arg(m_rate));
}

void MultiCamPlaybackWindow::onBackToStart()
{
    m_svc->seekWall(m_svc->contentStartWallMs());
}

// ---------------------------------------------------------------------------
// 服务信号
// ---------------------------------------------------------------------------
void MultiCamPlaybackWindow::onServiceState(MultiCamSyncService::State s)
{
    using State = MultiCamSyncService::State;
    m_playBtn->setEnabled(s == State::Ready || s == State::Playing
                          || s == State::Paused || s == State::Ended);
    m_playBtn->setText(s == State::Playing ? lang("⏸ 暂停", "⏸ Pause")
                                           : lang("▶ 播放", "▶ Play"));
    switch (s) {
    case State::Loading:  m_statusLabel->setText(lang("加载中…", "Loading…")); break;
    case State::Ready:    m_statusLabel->setText(lang("就绪", "Ready")); break;
    case State::Playing:  m_statusLabel->setText(QString()); break;
    case State::Paused:   m_statusLabel->setText(lang("已暂停", "Paused")); break;
    case State::Ended:    m_statusLabel->setText(lang("已播完", "Ended")); break;
    default: break;
    }
    if (s == State::Ready) {
        // 时长就绪：重排时间线/进度条量程
        if (m_mergedBar)
            m_mergedBar->setLanes(currentCamLanes());
        for (int i = 0; i < m_bars.size() && i < m_svc->laneCount(); ++i) {
            const qint64 dur = m_svc->lanes()[i].durationMs;
            m_bars[i]->setRange(0, int(qMax<qint64>(0, dur)));
            m_bars[i]->setEnabled(true);
        }
        refreshModeControls();
    }
}

void MultiCamPlaybackWindow::onLaneInfo(int idx)
{
    Q_UNUSED(idx);
    if (m_mergedBar)
        m_mergedBar->setLanes(currentCamLanes());
    for (int i = 0; i < m_bars.size() && i < m_svc->laneCount(); ++i) {
        const qint64 dur = m_svc->lanes()[i].durationMs;
        if (dur > 0 && m_bars[i]->maximum() != int(dur)) {
            m_bars[i]->setRange(0, int(dur));
            m_bars[i]->setEnabled(true);
        }
    }
    refreshModeControls();
}

void MultiCamPlaybackWindow::onLaneFailed(int idx, const QString &path)
{
    Q_UNUSED(idx);
    m_statusLabel->setText(
        lang("一路加载失败：%1", "A lane failed to load: %1")
            .arg(QFileInfo(path).fileName()));
}

void MultiCamPlaybackWindow::onPerfNotice(const QString &msg)
{
    m_statusLabel->setText(msg);
}

QString MultiCamPlaybackWindow::fmtStream(qint64 ms) const
{
    if (ms < 0)
        ms = 0;
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600)
        .arg((s % 3600) / 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString MultiCamPlaybackWindow::fmtWall(qint64 wallMs) const
{
    return QDateTime::fromMSecsSinceEpoch(wallMs, Qt::LocalTime)
        .toString(QStringLiteral("MM-dd HH:mm:ss"));
}

void MultiCamPlaybackWindow::onClock(qint64 wallMs)
{
    // 模式A 游标
    if (m_mergedBar)
        m_mergedBar->setCursorMs(wallMs);

    // 模式B 进度条（引擎真实位置为口径；拖动中不回写防打架）
    for (int i = 0; i < m_bars.size() && i < m_svc->laneCount(); ++i) {
        if (m_bars[i]->isSliderDown())
            continue;
        if (auto *e = m_svc->engineAt(i)) {
            const qint64 pos = e->position();
            m_bars[i]->blockSignals(true);
            m_bars[i]->setValue(int(qBound<qint64>(0, pos,
                                                   qint64(m_bars[i]->maximum()))));
            m_bars[i]->blockSignals(false);
            const auto &l = m_svc->lanes()[i];
            QString t = fmtStream(pos) + " / " + fmtStream(l.durationMs);
            if (l.temporary)
                t += QStringLiteral("  ≈%1").arg(fmtWall(syncWallOf(l, pos)));
            m_barLabels[i]->setText(t);
        }
    }
    updateTilesOsd();
}

void MultiCamPlaybackWindow::updateTilesOsd()
{
    const qint64 wall = m_svc->clockWallMs();
    for (int i = 0; i < m_tiles.size(); ++i) {
        auto *tile = m_tiles[i];
        if (!tile)
            continue;
        if (i >= m_svc->laneCount()) {
            // 未选视频的槽位（模式B 临时进/独立模式）
            tile->setPlaceholder(QString());
            tile->setOsdLines(QString(), QString());
            tile->setLaneName(lang("（点此选择视频）", "(click to pick video)"));
            continue;
        }
        const auto &l = m_svc->lanes()[i];
        tile->setLaneName(l.displayName);
        tile->setTemporaryBadge(l.temporary);
        tile->setLowresBadge(m_svc->laneIsLowres(i));
        auto *e = m_svc->engineAt(i);
        const qint64 pos = e ? e->position() : 0;
        if (l.path.isEmpty()) {
            // 空槽位：点击选视频
            tile->setOsdLines(QString(), QString());
            tile->setLaneName(lang("（点此选择视频）", "(click to pick video)"));
            tile->setPlaceholder(QString());
            continue;
        }
        if (l.temporary) {
            tile->setOsdLines(
                fmtStream(pos) + " / " + fmtStream(l.durationMs),
                QStringLiteral("≈ %1").arg(fmtWall(syncWallOf(l, pos))));
        } else {
            tile->setOsdLines(
                lang("墙钟 ", "Wall ") + fmtWall(syncWallOf(l, pos)),
                lang("流内 ", "Stream ") + fmtStream(pos) + " / "
                    + fmtStream(l.durationMs));
        }
        // 缺口/失败占位（laneUsable 涵盖加载失败与运行期暴毙）
        if (!l.path.isEmpty() && !m_svc->laneUsable(i))
            tile->setPlaceholder(lang("加载失败", "load failed"));
        else if (!l.path.isEmpty() && !m_svc->laneCoversNow(i))
            tile->setPlaceholder(lang("无信号（缺口）", "no signal (gap)"));
        else
            tile->setPlaceholder(QString());
    }
}
