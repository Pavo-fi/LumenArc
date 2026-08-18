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
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QCloseEvent>
#include <algorithm>

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
// UI 骨架（P-59 双页栈：机位勾选面板 ↔ 播放页）
// ---------------------------------------------------------------------------
void MultiCamPlaybackWindow::buildUi()
{
    m_stack = new QStackedLayout(this);
    m_pickerPage = new QWidget(this);
    buildPickerPage();
    m_stack->addWidget(m_pickerPage);
    m_playPage = new QWidget(this);
    buildPlayPage();
    m_stack->addWidget(m_playPage);
    m_stack->setCurrentWidget(m_playPage);
}

// ---- 机位勾选面板（P-59：直读案件清单 + 校时标识 + 勾选开始）----
void MultiCamPlaybackWindow::buildPickerPage()
{
    auto *lay = new QVBoxLayout(m_pickerPage);

    auto *title = new QLabel(lang("选择要同步播放的机位（勾选 2~4 路）",
                                  "Pick cameras to sync-play (2-4)"),
                             m_pickerPage);
    QFont tf = title->font();
    tf.setPixelSize(16);
    tf.setBold(true);
    title->setFont(tf);
    lay->addWidget(title);

    auto *legend = new QLabel(
        lang("✅ 已校时＝按墙钟对齐（多机同一时刻同框）· ⚠ 未校时＝2 路时可"
             "临时对齐（会话级不落盘）· ❌ 文件缺失不可选。清单=案件视频+前"
             "处理产物，校时状态实时读案内分析文件。",
             "✅ calibrated = wall-clock aligned · ⚠ uncalibrated = temporary "
             "alignment for 2 lanes (session only) · ❌ file missing."),
        m_pickerPage);
    legend->setWordWrap(true);
    legend->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(legend);

    auto *scroll = new QScrollArea(m_pickerPage);
    scroll->setWidgetResizable(true);
    auto *host = new QWidget(scroll);
    m_checkListLay = new QVBoxLayout(host);
    m_checkListLay->setContentsMargins(4, 4, 4, 4);
    m_checkListLay->setSpacing(4);
    scroll->setWidget(host);
    lay->addWidget(scroll, 1);

    m_pickHint = new QLabel(m_pickerPage);
    m_pickHint->setWordWrap(true);
    m_pickHint->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Accent));
    lay->addWidget(m_pickHint);

    auto *row = new QHBoxLayout();
    auto *refreshBtn = new QPushButton(lang("刷新清单", "Refresh"), m_pickerPage);
    refreshBtn->setToolTip(lang("校时/前处理后在主窗完成，点此重读案件清单",
                                "Re-read case inventory after calibrating"));
    connect(refreshBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onRefreshPicker);
    row->addWidget(refreshBtn);
    row->addStretch(1);
    m_startBtn = new QPushButton(lang("开始同步播放 ▶", "Start sync play ▶"),
                                 m_pickerPage);
    m_startBtn->setEnabled(false);
    connect(m_startBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onStartSync);
    row->addWidget(m_startBtn);
    lay->addLayout(row);
}

void MultiCamPlaybackWindow::refreshInventory()
{
    // 清旧行
    QLayoutItem *item;
    while ((item = m_checkListLay->takeAt(0)) != nullptr) {
        if (item->widget())
            delete item->widget();
        delete item;
    }
    m_checks.clear();
    m_inventory = m_case ? buildCamInventory(*m_case)
                         : QVector<CamInventoryItem>{};

    // 两来源都有时分组小标题
    bool hasVideo = false, hasPre = false;
    for (const auto &it : m_inventory)
        (it.fromPreprocess ? hasPre : hasVideo) = true;

    int defaultChecked = 0;
    bool videoHeaderDone = false, preHeaderDone = false;
    for (int i = 0; i < m_inventory.size(); ++i) {
        const auto &it = m_inventory[i];
        if (hasVideo && hasPre) {
            if (!it.fromPreprocess && !videoHeaderDone) {
                videoHeaderDone = true;
                auto *h = new QLabel(lang("—— 案件视频 ——", "—— Case videos ——"));
                h->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextMuted));
                m_checkListLay->addWidget(h);
            } else if (it.fromPreprocess && !preHeaderDone) {
                preHeaderDone = true;
                auto *h = new QLabel(lang("—— 前处理产物（拼接/转码） ——",
                                          "—— Preprocess outputs ——"));
                h->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextMuted));
                m_checkListLay->addWidget(h);
            }
        }

        auto *rowW = new QWidget;
        auto *row = new QHBoxLayout(rowW);
        row->setContentsMargins(0, 0, 0, 0);
        auto *cb = new QCheckBox(
            QStringLiteral("%1  %2").arg(it.id, it.displayName), rowW);
        cb->setEnabled(it.pathExists);
        // 默认勾：已校时且在盘，最多 4 路（拍板 2-4）
        if (it.calibrated && it.pathExists && defaultChecked < 4) {
            cb->setChecked(true);
            ++defaultChecked;
        }
        connect(cb, &QCheckBox::toggled, this,
                &MultiCamPlaybackWindow::updatePickerValidation);
        row->addWidget(cb, 1);

        auto *status = new QLabel(rowW);
        if (!it.pathExists) {
            status->setText(lang("❌ 文件缺失", "❌ missing"));
            status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Danger));
        } else if (it.calibrated) {
            status->setText(lang("✅ 已校时", "✅ calibrated"));
            status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Success));
        } else {
            status->setText(lang("⚠ 未校时", "⚠ not calibrated"));
            status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Accent));
        }
        row->addWidget(status);

        if (it.pathExists && !it.calibrated) {
            auto *go = new QPushButton(lang("去校时…", "Calibrate…"), rowW);
            go->setToolTip(lang("在主窗打开该路做时间设置（校时），完成后回来点"
                                "「刷新清单」",
                                "Open in main window to calibrate, then Refresh"));
            const QString path = it.path;
            connect(go, &QPushButton::clicked, this, [this, path]() {
                if (onOpenVideo)
                    onOpenVideo(path);   // 主窗打开该路（校时走既有时间设置流程）
            });
            row->addWidget(go);
        }
        m_checkListLay->addWidget(rowW);
        m_checks.append(cb);
    }
    m_checkListLay->addStretch(1);
    updatePickerValidation();
}

void MultiCamPlaybackWindow::updatePickerValidation()
{
    int n = 0, uncal = 0;
    for (int i = 0; i < m_checks.size(); ++i) {
        if (!m_checks[i]->isChecked())
            continue;
        ++n;
        if (!m_inventory[i].calibrated)
            ++uncal;
    }
    QString hint;
    bool ok = false;
    if (m_inventory.isEmpty()) {
        hint = lang("案内暂无视频或前处理产物——请先在主窗向案件添加视频，"
                    "或跑一次前处理（拼接/转码）。",
                    "No video or preprocess output in case yet.");
    } else if (n < 2) {
        hint = lang("同步播放至少 2 路——再勾选 %1 路。",
                    "Pick at least 2 lanes (%1 more).").arg(2 - n);
    } else if (n > 4) {
        hint = lang("最多 4 路同屏（当前勾了 %1 路）——请减到 4 路以内。",
                    "At most 4 lanes (%1 checked).").arg(n);
    } else if (n >= 3 && uncal > 0) {
        hint = lang("3 路以上须全部已校时（对齐精度靠校时保真）：点未校时行"
                    "「去校时…」完成后「刷新清单」，或减到 2 路走临时对齐。",
                    "3+ lanes must all be calibrated; calibrate first or "
                    "reduce to 2 lanes for temporary alignment.");
    } else if (uncal == 0) {
        ok = true;
        hint = lang("%1 路全部已校时 → 合并时间线模式：同一墙钟时刻各机位"
                    "同框，拖游标全路同步走带。",
                    "%1 calibrated lanes → merged wall-clock timeline.").arg(n);
    } else if (uncal == 1) {
        ok = true;
        hint = lang("含 1 路未校时 → 分开进度条模式：先各自独立播放，点"
                    "「对齐…」对到同一时刻后联动（会话级，不落盘）。",
                    "1 uncalibrated lane → separate bars + temp alignment.");
    } else {
        ok = true;
        hint = lang("两路均未校时 → 分开进度条模式：「对齐…」对到同一时刻后"
                    "联动（会话级，不落盘）。",
                    "Both uncalibrated → separate bars; align to sync.");
    }
    m_startBtn->setEnabled(ok);
    m_pickHint->setText(hint);
}

void MultiCamPlaybackWindow::onStartSync()
{
    QVector<SyncLaneData> lanes;
    int uncal = 0;
    for (int i = 0; i < m_checks.size(); ++i) {
        if (!m_checks[i]->isChecked())
            continue;
        const auto &it = m_inventory[i];
        if (it.calibrated) {
            lanes.append(it.lane);
        } else {
            SyncLaneData l;
            l.id = it.id;
            l.path = it.path;
            l.displayName = it.displayName;
            l.temporary = true;   // 未校时路 = 临时进（会话级对齐，不落盘）
            ++uncal;
            lanes.append(l);
        }
    }
    if (lanes.size() < 2 || lanes.size() > 4)
        return;   // 校验面板已把关，兕底
    if (lanes.size() >= 3 && uncal > 0)
        return;   // 3 路以上须全校时（与 updatePickerValidation 同口径）
    // 已校时路按墙钟起点升序排前，临时路随后（模式B 参考路锚定确定性）
    std::stable_sort(lanes.begin(), lanes.end(),
                     [](const SyncLaneData &a, const SyncLaneData &b) {
                         if (a.temporary != b.temporary)
                             return !a.temporary;   // 校时路在前
                         if (!a.temporary)
                             return syncLaneWallStart(a) < syncLaneWallStart(b);
                         return false;
                     });
    m_mode = (uncal == 0) ? SyncTimelineMode::Merged : SyncTimelineMode::Separate;
    if (!m_svc->loadLanes(lanes))
        return;
    m_stack->setCurrentWidget(m_playPage);
    rebuildTiles();
    rebuildTimelineArea();
    refreshModeControls();
}

void MultiCamPlaybackWindow::onBackToPicker()
{
    m_svc->closeAll();          // 释放全部引擎（C5 资源有界）
    refreshInventory();         // 校时状态可能已变（去校时回来）
    m_stack->setCurrentWidget(m_pickerPage);
}

void MultiCamPlaybackWindow::onRefreshPicker()
{
    refreshInventory();
}

// ---- 播放页（原窗口主体：工具行+瓦片网格+时间线区）----
void MultiCamPlaybackWindow::buildPlayPage()
{
    auto *root = new QVBoxLayout(m_playPage);

    // 工具行：重选机位 · 播放/暂停 · 回起点 · 倍速 · OSD 开关 · 状态
    auto *bar = new QHBoxLayout();
    m_repickBtn = new QPushButton(lang("↩ 重选机位", "↩ Lanes"), m_playPage);
    m_repickBtn->setVisible(false);   // 仅案件模式（openCaseLanes 置可见）
    connect(m_repickBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onBackToPicker);
    bar->addWidget(m_repickBtn);
    m_playBtn = new QPushButton(lang("▶ 播放", "▶ Play"), m_playPage);
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
// 入口装配（P-59：案件模式进机位勾选面板；独立模式直进播放页）
// ---------------------------------------------------------------------------
bool MultiCamPlaybackWindow::openCaseLanes(const CaseManager &cm)
{
    m_case = &cm;
    m_repickBtn->setVisible(true);
    refreshInventory();                       // 清单 + 默认勾已校时路
    m_stack->setCurrentWidget(m_pickerPage);  // 先选机位，用户点开始才装配
    return true;
}

void MultiCamPlaybackWindow::openStandalone()
{
    m_case = nullptr;
    m_repickBtn->setVisible(false);
    m_mode = SyncTimelineMode::Separate;
    m_tempSlot = 0;   // 两路皆临时（先选先为参考路）
    rebuildTiles();
    rebuildTimelineArea();
    refreshModeControls();
    m_stack->setCurrentWidget(m_playPage);
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
    // 清旧：布局 + 全部直接子控件（进度条/名称/时间标签/合并条）一并删除，
    // 否则换路重载后旧进度条游离叠加（占位抢交互）
    qDeleteAll(m_timelineHost->findChildren<QWidget *>(
        QString(), Qt::FindDirectChildrenOnly));
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
            // 拖动：已联动路 = 墙钟轴同步走带（scrub）；未对齐临时路 = 本路独立
            // 拖拽（引擎 scrub 追逐同手感，不动别路）；对齐模式下 = 独立拖动
            connect(slider, &QSlider::sliderPressed, this, [this, idx]() {
                if (m_aligning || idx >= m_svc->laneCount())
                    return;
                if (m_svc->laneLinked(idx)) {
                    m_svc->beginScrub();
                    m_bars[idx]->setProperty("scrub", 1);
                } else if (auto *e = m_svc->engineAt(idx)) {
                    e->setScrubMode(true);
                    m_bars[idx]->setProperty("scrub", 2);
                }
            });
            connect(slider, &QSlider::sliderMoved, this,
                    [this, idx](int v) {
                        if (m_aligning)
                            return;   // 对齐模式：valueChanged 单独处理
                        if (idx >= m_svc->laneCount())
                            return;
                        if (m_bars[idx]->property("scrub").toInt() == 2) {
                            if (auto *e = m_svc->engineAt(idx))
                                e->setScrubTarget(v);   // 独立拖拽只动本路
                        } else {
                            m_svc->scrubTo(syncWallOf(m_svc->lanes()[idx], v));
                        }
                    });
            connect(slider, &QSlider::sliderReleased, this, [this, idx]() {
                if (m_aligning) {
                    if (idx < m_svc->laneCount()) {
                        if (auto *e = m_svc->engineAt(idx))
                            e->seek(m_bars[idx]->value());
                    }
                    return;
                }
                const int scrub = m_bars[idx]->property("scrub").toInt();
                m_bars[idx]->setProperty("scrub", 0);
                if (scrub == 2) {
                    if (auto *e = m_svc->engineAt(idx)) {
                        e->setScrubMode(false);
                        e->seek(m_bars[idx]->value());   // 松手精确 seek
                    }
                } else if (scrub == 1) {
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
        // 操作引导按状态分三档：对齐中 / 有未对齐临时路 / 已对齐联动
        bool hasUnlinkedTemp = false;
        for (int i = 0; i < m_svc->laneCount(); ++i)
            if (m_svc->lanes()[i].temporary && !m_svc->laneLinked(i))
                hasUnlinkedTemp = true;
        m_hintLabel->setText(
            m_aligning
                ? lang("对齐模式：分别拖动两路进度条到同一时刻（可用声音"
                       "比对，点瓦片切听），然后点「确认对齐」。",
                       "Aligning: drag both lanes to the same moment "
                       "(use audio cues, click a tile to listen), then Apply.")
                : (hasUnlinkedTemp
                   ? lang("临时路未对齐：两路各自独立，拖进度条只动本路。"
                          "点「对齐…」把两路对到同一时刻后开始联动。",
                          "Temp lane not aligned: lanes are independent. "
                          "Use Align… to sync them at the same moment.")
                   : lang("已对齐（会话级，不落盘）：拖任一路进度条两路联动；"
                          "双击瓦片回单路分析。",
                          "Aligned (session only): drag either bar to scrub "
                          "both; double-click a tile to open that lane.")));
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
    const auto &lanes = m_svc->lanes();
    const int tempIdx = lanes[1].temporary ? 1 : 0;
    // 以参考路当前墙钟为镭建立临时路偏移（服务内完成：标联动+重算区间+
    // 时钟对齐到临时路当前画面）；独立模式双临时路时参考路一并锚定
    m_svc->alignTempLane(tempIdx, 1 - tempIdx);
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
    // 未对齐临时路不进墙钟轴：各自回流内 0
    for (int i = 0; i < m_svc->laneCount(); ++i)
        if (!m_svc->laneLinked(i))
            if (auto *e = m_svc->engineAt(i))
                e->seek(0);
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
            if (l.temporary && m_svc->laneLinked(i))
                t += QStringLiteral("  ≈%1").arg(fmtWall(syncWallOf(l, pos)));
            else if (l.temporary)
                t += lang("  未对齐", "  not aligned");
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
            if (m_svc->laneLinked(i)) {
                tile->setOsdLines(
                    fmtStream(pos) + " / " + fmtStream(l.durationMs),
                    QStringLiteral("≈ %1").arg(fmtWall(syncWallOf(l, pos))));
            } else {
                // 未对齐：墙钟推算无义，明示独立状态（防误解为故障）
                tile->setOsdLines(
                    fmtStream(pos) + " / " + fmtStream(l.durationMs),
                    lang("未对齐·独立播放", "not aligned · independent"));
            }
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
