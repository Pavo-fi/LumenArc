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
#include "app/segment_switch_engine.h"

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
#include <QTimer>
#include <QCloseEvent>
#include <algorithm>

#include "app/cam_timeline.h"
#include "app/case_manager.h"
#include "app/project_io.h"
#include <QSignalBlocker>
#include <QLineEdit>
#include <QListWidget>
#include <QComboBox>
#include "camtilewidget.h"
#include "playbackadjustpanel.h"
#include "multicamview.h"
#include "segmentexportdialog.h"
#include "infrastructure/segment_export_engine.h"
#include <QProgressDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
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
    // 用户布置：标题栏最小化/最大化钮与前处理页同款（QDialog 默认只带关闭钮）
    setWindowFlags(windowFlags() | Qt::WindowMinMaxButtonsHint);
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
    m_groupChecks.clear();
    m_inventory = m_case ? buildCamInventory(*m_case)
                         : QVector<CamInventoryItem>{};
    m_mergedGroups = m_case ? buildMergedGroups(m_inventory)
                            : QVector<CamMergedGroup>{};

    // P-69：合并轨组行置顶（同编号多文件并一路；先起步者赢，重叠 ⚠ 标注）
    for (int gi = 0; gi < m_mergedGroups.size(); ++gi) {
        const auto &g = m_mergedGroups[gi];
        auto *rowW = new QWidget;
        auto *row = new QHBoxLayout(rowW);
        row->setContentsMargins(0, 0, 0, 0);
        auto *cb = new QCheckBox(
            QStringLiteral("⊞ %1 · %2 段合并轨").arg(g.label).arg(g.memberIdx.size()),
            rowW);
        QFont bf = cb->font();
        bf.setBold(true);
        cb->setFont(bf);
        cb->setToolTip(lang(
            "同编号 %1 个文件并成一路播放（须全部已校时）。重叠时段先起步的段"
            "赢，时间线上以 ⚠ 标注重叠。",
            "Merge %1 files of the same camera into one lane (all must be "
            "calibrated). Overlap: earliest-start segment wins, marked ⚠.")
            .arg(g.memberIdx.size()));
        connect(cb, &QCheckBox::toggled, this,
                &MultiCamPlaybackWindow::updatePickerValidation);
        row->addWidget(cb, 1);
        auto *tag = new QLabel(lang("合并轨", "merged"), rowW);
        tag->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Success));
        row->addWidget(tag);
        m_checkListLay->addWidget(rowW);
        m_groupChecks.append(cb);
    }
    // 默认勾选决策（P-69 二轮返修）：组行优先——否则成员先被自动勾上，
    // 互斥规则把组行禁用变灰，用户根本选不到合并轨
    const PickerDefaults defaults = pickerDefaultChecks(m_inventory, m_mergedGroups);
    for (int gi = 0; gi < m_groupChecks.size(); ++gi)
        if (defaults.groups.contains(gi))
            m_groupChecks[gi]->setChecked(true);
    if (!m_mergedGroups.isEmpty()) {
        auto *h = new QLabel(lang("—— 单文件路 ——", "—— Single-file lanes ——"));
        h->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextMuted));
        m_checkListLay->addWidget(h);
    }

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
        // 有标签（groupKey≠id）：标签打头、编号括注——用户按机位标签认路，
        // 编号打头会误读（「P005 P002」被读成 P005，真机反馈）
        QString rowText;
        if (!it.groupKey.isEmpty() && it.groupKey != it.id)
            rowText = QStringLiteral("%1（%2） %3")
                          .arg(it.displayName, it.id,
                               QFileInfo(it.path).fileName());
        else
            rowText = QStringLiteral("%1  %2").arg(it.id, it.displayName);
        auto *cb = new QCheckBox(rowText, rowW);
        cb->setEnabled(it.pathExists);
        // 默认勾：已校时且在盘，最多 4 路（拍板 2-4）；组成员不勾（组行已代勾）
        if (defaults.members.contains(i)) {
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
    // P-69 组-成员互斥：勾选合并轨 → 成员行禁选；反之成员有勾 → 组行禁选
    QSet<int> groupedMembers;
    for (int gi = 0; gi < m_mergedGroups.size(); ++gi) {
        const bool gOn = m_groupChecks[gi]->isChecked();
        bool memberOn = false;
        for (int mi : m_mergedGroups[gi].memberIdx)
            if (mi < m_checks.size() && m_checks[mi]->isChecked())
                memberOn = true;
        m_groupChecks[gi]->setEnabled(!memberOn);
        for (int mi : m_mergedGroups[gi].memberIdx) {
            if (mi < m_checks.size()) {
                groupedMembers.insert(mi);
                if (m_inventory[mi].pathExists)
                    m_checks[mi]->setEnabled(!gOn);
            }
        }
    }
    int n = 0, uncal = 0;
    for (int i = 0; i < m_checks.size(); ++i) {
        if (!m_checks[i]->isChecked() || !m_checks[i]->isEnabled())
            continue;
        ++n;
        if (!m_inventory[i].calibrated)
            ++uncal;
    }
    for (auto *gc : m_groupChecks)
        if (gc->isChecked() && gc->isEnabled())
            ++n;   // 合并轨 = 1 路（成员全部已校时，uncal 不增）
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
    // P-69：合并轨先行装配（成员按墙钟起点升序已在分组时排好）
    for (int gi = 0; gi < m_mergedGroups.size(); ++gi) {
        if (!m_groupChecks[gi]->isChecked() || !m_groupChecks[gi]->isEnabled())
            continue;
        const auto &g = m_mergedGroups[gi];
        SyncLaneData L;
        L.id = QStringLiteral("M_") + g.label;
        L.displayName = g.label + QStringLiteral("（%1 段合并）").arg(g.memberIdx.size());
        L.calibrated = true;
        L.temporary = false;
        qint64 totalDur = 0;
        for (int mi : g.memberIdx) {
            const auto &it = m_inventory[mi];
            SyncSegment seg;
            seg.path = it.path;
            seg.srcId = it.id;
            seg.cal = it.lane.cal;
            // 段时长：.vla 已分析值优先，缺则 ffprobe 预读（拍板设计）
            seg.durationMs = it.analyzedDurationMs > 0
                ? it.analyzedDurationMs : probeMediaDurationMs(it.path);
            totalDur += seg.durationMs;
            L.segments.append(seg);
        }
        L.durationMs = totalDur;
        L.path = L.segments.isEmpty() ? QString() : L.segments.first().path;
        if (L.segments.size() >= 2)
            lanes.append(L);
    }
    for (int i = 0; i < m_checks.size(); ++i) {
        if (!m_checks[i]->isChecked() || !m_checks[i]->isEnabled())
            continue;   // P-69：组勾选后成员行禁用，不参与单路
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
    // v1.13.2（P-73）：外包水平层——右列「同事件对时」面板宿主（默认隐藏）
    auto *outerLay = new QHBoxLayout(m_playPage);
    outerLay->setContentsMargins(0, 0, 0, 0);
    outerLay->setSpacing(0);
    auto *contentCol = new QWidget(m_playPage);
    auto *root = new QVBoxLayout(contentCol);
    outerLay->addWidget(contentCol, 1);
    m_ecHost = new QWidget(m_playPage);
    m_ecHost->setFixedWidth(310);
    m_ecHost->setStyleSheet("background: " + Theme::BgPanel
        + "; border-left: 1px solid " + Theme::Border + ";");
    m_ecHost->hide();
    outerLay->addWidget(m_ecHost);

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

    // v1.12.8（用户拍板）：画面调节只作用于当前选中的瓦片，每路独立参数
    // （点击瓦片选中，强调色外框标示；仅显示链路，证据链不变）
    m_adjustBtn = new QPushButton(lang("画面调节", "Adjust"), this);
    m_adjustBtn->setCheckable(true);
    m_adjustBtn->setToolTip(lang(
        "调节当前选中机位的亮度/对比度/伽马/色阶/反色/旋转（仅显示）。"
        "点击瓦片切换作用对象。",
        "Adjust the selected tile (brightness/contrast/gamma/levels/invert/"
        "rotate, display only). Click a tile to change target."));
    connect(m_adjustBtn, &QPushButton::toggled, this, [this](bool on) {
        if (on) {
            if (!m_adjustPanel) {
                m_adjustPanel = new PlaybackAdjustPanel(this);
                m_adjustPanel->setFloating(true);
                connect(m_adjustPanel, &PlaybackAdjustPanel::adjustChanged,
                        this, [this](const DisplayAdjust &adj) {
                    if (m_selectedTile >= 0
                        && m_selectedTile < m_tileAdjusts.size()) {
                        m_tileAdjusts[m_selectedTile] = adj;
                        applyAdjustToTiles();
                    }
                });
                connect(m_adjustPanel, &PlaybackAdjustPanel::rotationChanged,
                        this, [this](int deg) {
                    if (m_selectedTile >= 0
                        && m_selectedTile < m_tileRotations.size()) {
                        m_tileRotations[m_selectedTile] = deg;
                        applyAdjustToTiles();
                    }
                });
                connect(m_adjustPanel, &QDockWidget::visibilityChanged,
                        this, [this](bool vis) {
                    if (m_adjustBtn)
                        m_adjustBtn->setChecked(vis);
                });
                m_adjustPanel->resize(300, 420);
            }
            if (m_selectedTile < 0 && m_svc->laneCount() > 0)
                selectTile(0);
            selectTile(m_selectedTile);   // 刷新面板数值与标题
            m_adjustPanel->show();
            m_adjustPanel->raise();
        } else if (m_adjustPanel) {
            m_adjustPanel->hide();
        }
    });
    bar->addWidget(m_adjustBtn);

    // P-68 第 10 条：多机选段导出（与单路同款分段变速逻辑；模式A 可用）
    m_exportClipBtn = new QPushButton(lang("导出选段", "Export Clip"), this);
    m_exportClipBtn->setToolTip(lang(
        "导出 A-B 选段：A/B 键在合并时间线上打点；分段变速 + 全机位宫格同框 MP4",
        "Export A-B selection: mark with A/B keys on the merged timeline; "
        "variable-speed multi-cam grid MP4"));
    m_exportClipBtn->setEnabled(false);
    connect(m_exportClipBtn, &QPushButton::clicked,
            this, &MultiCamPlaybackWindow::onExportClip);
    bar->addWidget(m_exportClipBtn);

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

    // P-73：多机同事件间接校时（≥2 路且至少一路已校时时可见）
    m_eventCalibBtn = new QPushButton(lang("同事件对时", "Event Sync"), this);
    m_eventCalibBtn->setCheckable(true);
    m_eventCalibBtn->setToolTip(lang(
        "以已校时机位为参考，用同一事件（如：同一辆车压线）给未校时机位"
        "生成正式校时。间接校准·取证链入报告·事件名必填。",
        "Cross-camera event sync: calibrate an uncalibrated lane against a "
        "calibrated one via the same real-world event. Provenance recorded."));
    m_eventCalibBtn->setVisible(false);
    connect(m_eventCalibBtn, &QPushButton::toggled, this,
            &MultiCamPlaybackWindow::onEventCalibToggled);
    bar->addWidget(m_eventCalibBtn);

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
        tile->setSelected(i == m_selectedTile);
        const int idx = i;
        connect(tile, &CamTileWidget::clicked, this, [this, idx]() {
            selectTile(idx);   // v1.12.8：画面调节作用对象跟随点击
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
    // v1.12.8：逐瓦片调节参数表与路数对齐（保留旧值，新路补默认）
    while (m_tileAdjusts.size() < n) {
        m_tileAdjusts.append(DisplayAdjust());
        m_tileRotations.append(0);
    }
    applyAdjustToTiles();
    if (m_selectedTile < 0 && n > 0)
        selectTile(0);
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
        // P-69：合并轨逐段块（时间线一行多色块）
        if (l.isMerged()) {
            for (int k = 0; k < l.segments.size(); ++k) {
                CamLane::SegBlock sb;
                sb.segIdx = k;
                sb.wallStartMs = l.segments[k].wallStartMs();
                sb.wallEndMs = l.segments[k].wallEndMs();
                c.segs.append(sb);
            }
        }
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

    // P-73：对时模式强制每路独立进度条（合并条无法分路定位事件帧）
    if (m_mode == SyncTimelineMode::Merged && !m_eventCalib) {
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
            // P-73 返修：创建即按当前路时长设量程——量程只在 Ready/LaneInfo
            // 信号里设，重建后的新滑条永远停在默认 0..99，对时模式拖动
            // seek 只落在前 99ms →「画面没动」（用户真机反馈）
            const qint64 dur0 = (i < m_svc->laneCount())
                ? m_svc->lanes()[i].durationMs : 0;
            slider->setRange(0, int(qMax<qint64>(0, dur0)));
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
                if (m_aligning || m_eventCalib || idx >= m_svc->laneCount())
                    return;   // 对时模式同对齐：valueChanged 直驱引擎
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
                        if (m_aligning || m_eventCalib)
                            return;   // 对齐/对时模式：valueChanged 单独处理
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
                if (m_aligning || m_eventCalib) {
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
                        if ((!m_aligning && !m_eventCalib)
                            || idx >= m_svc->laneCount())
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
    m_alignBtn->setVisible(sep && !m_aligning && canAlign && !m_eventCalib);
    m_alignOkBtn->setVisible(m_aligning);
    m_alignCancelBtn->setVisible(m_aligning);
    // P-73：≥2 路全载且至少一路已校时（参考路存在）可见；对时中保持可见
    {
        bool anyCal = false;
        for (const auto &l : m_svc->lanes())
            if (l.calibrated)
                anyCal = true;
        m_eventCalibBtn->setVisible(!m_aligning && lanesLoaded()
            && m_svc->laneCount() >= 2 && anyCal);
    }
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
    if (m_svc->laneCount() != 2 || m_eventCalib)
        return;   // P-73：对时模式与对齐会话互斥
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
    if (m_eventCalib) {   // P-73 对时沙盒：自由播放，不经服务联动
        ecTogglePlay();
        return;
    }
    m_svc->togglePlay();
}

void MultiCamPlaybackWindow::ecTogglePlay()
{
    m_ecPlaying = !m_ecPlaying;
    for (int i = 0; i < m_svc->laneCount(); ++i)
        if (auto *e = m_svc->engineAt(i))
            m_ecPlaying ? e->play() : e->pause();
    m_playBtn->setText(m_ecPlaying ? lang("⏸ 暂停", "⏸ Pause")
                                   : lang("▶ 播放", "▶ Play"));
    if (!m_ecTick) {
        m_ecTick = new QTimer(this);
        m_ecTick->setInterval(150);
        connect(m_ecTick, &QTimer::timeout, this, [this]() {
            updateBarsFromEngines();
            updateTilesOsd();
        });
    }
    if (m_ecPlaying)
        m_ecTick->start();
    else
        m_ecTick->stop();
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

void MultiCamPlaybackWindow::applyAdjustToTiles()
{
    for (int i = 0; i < m_tiles.size(); ++i) {
        if (!m_tiles[i])
            continue;
        const DisplayAdjust adj = (i < m_tileAdjusts.size())
            ? m_tileAdjusts[i] : DisplayAdjust();
        const int rot = (i < m_tileRotations.size()) ? m_tileRotations[i] : 0;
        m_tiles[i]->setDisplayAdjust(adj, rot);
    }
}

void MultiCamPlaybackWindow::selectTile(int idx)
{
    if (idx < 0 || idx >= m_tiles.size())
        return;
    m_selectedTile = idx;
    for (int i = 0; i < m_tiles.size(); ++i)
        if (m_tiles[i])
            m_tiles[i]->setSelected(i == idx);
    if (m_adjustPanel) {
        const DisplayAdjust adj = (idx < m_tileAdjusts.size())
            ? m_tileAdjusts[idx] : DisplayAdjust();
        const int rot = (idx < m_tileRotations.size()) ? m_tileRotations[idx] : 0;
        m_adjustPanel->setValues(adj, rot);
        QString name;
        if (idx < m_svc->laneCount())
            name = m_svc->lanes()[idx].displayName;
        m_adjustPanel->setWindowTitle(
            lang("画面调节（当前：%1）", "Adjust (lane: %1)").arg(name));
    }
}

void MultiCamPlaybackWindow::onClock(qint64 wallMs)
{
    m_lastWallMs = wallMs;   // P-68 A/B 打点用
    if (m_exportDlg)
        m_exportDlg->setCursorMs(wallMs);   // 导出面板游标实时跟随（反馈②）
    // 模式A 游标
    if (m_mergedBar)
        m_mergedBar->setCursorMs(wallMs);

    updateBarsFromEngines();
    updateTilesOsd();
}

void MultiCamPlaybackWindow::updateBarsFromEngines()
{
    // 模式B/对时 进度条（引擎真实位置为口径；拖动中不回写防打架）
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
        // P-69：合并轨段标 [k/N]（引擎当前段号）
        QString lname = l.displayName;
        if (l.isMerged()) {
            if (auto *se = qobject_cast<SegmentSwitchEngine *>(m_svc->engineAt(i))) {
                const int k = se->currentSegment();
                if (k >= 0)
                    lname += QStringLiteral(" [%1/%2]").arg(k + 1).arg(l.segments.size());
            }
        }
        tile->setLaneName(lname);
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
        else if (!m_eventCalib && !l.path.isEmpty()
                 && !m_svc->laneCoversNow(i))
            tile->setPlaceholder(lang("无信号（缺口）", "no signal (gap)"));
        else
            tile->setPlaceholder(QString());
    }
}

// ---------------------------------------------------------------------------
// P-68 第 10 条：多机选段导出（A/B 打点 + 分段变速 + 宫格合成）
// ---------------------------------------------------------------------------

void MultiCamPlaybackWindow::keyPressEvent(QKeyEvent *event)
{
    // A/B 键在合并时间线当前游标处打点（仅模式A；与单路分析同手感）
    if (!m_mergedBar || !m_mergedBar->isVisible() || m_lastWallMs < 0) {
        QDialog::keyPressEvent(event);
        return;
    }
    const auto setPt = [this](qint64 &pt, const QString &cn) {
        pt = m_lastWallMs;
        if (m_abA >= 0 && m_abB >= 0 && m_abA > m_abB)
            qSwap(m_abA, m_abB);
        m_mergedBar->setABRegion(m_abA, m_abB);
        if (m_exportClipBtn)
            m_exportClipBtn->setEnabled(m_mergedBar->hasAB());
        m_statusLabel->setText(QStringLiteral("%1 %2").arg(cn, fmtWall(m_lastWallMs)));
    };
    switch (event->key()) {
    case Qt::Key_A:
        if (event->modifiers() & Qt::ControlModifier) {
            // Ctrl+A：清除选段
            m_abA = m_abB = -1;
            m_mergedBar->setABRegion(-1, -1);
            if (m_exportClipBtn)
                m_exportClipBtn->setEnabled(false);
            m_statusLabel->setText(lang("选段已清除", "Selection cleared"));
            return;
        }
        setPt(m_abA, lang("A 点", "A:"));
        return;
    case Qt::Key_B:
        setPt(m_abB, lang("B 点", "B:"));
        return;
    default:
        QDialog::keyPressEvent(event);
    }
}

void MultiCamPlaybackWindow::onExportClip()
{
    if (!m_mergedBar || !m_mergedBar->hasAB())
        return;
    // P-69：合并轨跨段合成导出不支持（本批明文拒导+说明，后续再支持）
    for (const auto &L : m_svc->lanes())
        if (L.isMerged()) {
            QMessageBox::warning(this, lang("导出选段", "Export clip"),
                lang("本次会话含合并轨（%1）——跨段合成导出不支持（本批暂不落地）。"
                     "请将各段分别作单路加入会话再导出。",
                     "Session contains a merged lane (%1) - cross-segment "
                     "export is not supported yet. Add each segment as a "
                     "single-file lane to export.").arg(L.displayName));
            return;
        }
    const qint64 aMs = m_mergedBar->abA();
    const qint64 bMs = m_mergedBar->abB();

    speedplan::SpeedPlan plan;
    plan.aMs = aMs;
    plan.bMs = bMs;
    plan.rates = {1.0};
    plan.normalize();

    // 建议输出路径：案件模式 = 案内 exports/；独立模式 = 首路源旁
    const QVector<SyncLaneData> &lanes = m_svc->lanes();
    QString dir;
    if (m_case) {
        dir = m_case->caseDir() + QStringLiteral("/exports");
        QDir().mkpath(dir);
    } else if (!lanes.isEmpty()) {
        dir = QFileInfo(lanes.first().path).absolutePath();
    }
    const QString suggested = QDir(dir).filePath(
        QStringLiteral("LAClip_MC_%1-%2.mp4").arg(aMs).arg(bMs));

    // 非模态浮窗（用户反馈②③：与单路同款，不挡播放/游标）
    if (!m_exportDlg) {
        m_exportDlg = new SegmentExportDialog(plan, m_lastWallMs, suggested, this,
                                              /*wallEpoch=*/true);
        connect(m_exportDlg, &SegmentExportDialog::exportRequested, this,
                [this](const speedplan::SpeedPlan &planIn, bool burnOsd,
                       const QString &outPath) {
                    startClipExport(planIn, burnOsd, outPath);
                });
        connect(m_exportDlg, &SegmentExportDialog::cancelRequested, this, [this]() {
            if (m_segmentExporter)
                m_segmentExporter->cancel();
        });
    } else {
        m_exportDlg->setPlan(plan, suggested);
        m_exportDlg->setCursorMs(m_lastWallMs);
    }
    m_exportDlg->show();
    m_exportDlg->raise();
    m_exportDlg->activateWindow();
}

/// P-68 多机：面板「开始导出」执行体
void MultiCamPlaybackWindow::startClipExport(const speedplan::SpeedPlan &planIn,
                                             bool burnOsd, const QString &outPath)
{
    if (m_segmentExporter && m_segmentExporter->isRunning()) {
        m_exportDlg->setResult(false, lang("已有导出进行中", "Export already running"));
        return;
    }
    speedplan::SpeedPlan plan = planIn;
    plan.normalize();

    SegmentExportEngine::Params pp;
    pp.outputPath = outPath;
    pp.plan = plan;
    pp.lanes = m_svc->lanes();
    pp.audioLane = m_svc->audibleLane();
    double fps = 25.0;
    for (int i = 0; i < m_svc->laneCount(); ++i)
        if (auto *e = m_svc->engineAt(i))
            if (e->fps() > 0.0f) { fps = e->fps(); break; }
    pp.outFps = fps;
    pp.burnOsd = burnOsd;
    pp.caseLabel = m_case ? m_case->meta().caseNo : QString();
    // 逐瓦片放大镜快照入导出（真机反馈：导出画面应包含放大镜画面）——
    // 导出时刻各瓦片的缩放/取景中心随参数冻结入格，格内右下 PIP 呈现
    pp.laneZooms.resize(pp.lanes.size());
    for (int i = 0; i < pp.lanes.size() && i < m_tiles.size(); ++i)
        if (m_tiles[i] && m_tiles[i]->zoom() > 1.0) {
            pp.laneZooms[i].zoom = m_tiles[i]->zoom();
            pp.laneZooms[i].center = m_tiles[i]->zoomCenter();
        }

    if (!m_segmentExporter)
        m_segmentExporter = new SegmentExportEngine(this);
    SegmentExportEngine *eng = m_segmentExporter;
    m_exportDlg->setExportRunning(true, int(plan.outputFrameCount(pp.outFps)));
    connect(eng, &SegmentExportEngine::progress, m_exportDlg,
            &SegmentExportDialog::setProgress, Qt::UniqueConnection);
    connect(eng, &SegmentExportEngine::finished, this,
            [this](bool ok, const QString &msg) {
                if (m_exportDlg)
                    m_exportDlg->setResult(ok, msg);
            }, Qt::UniqueConnection);
    eng->start(pp);
}

// ---------------------------------------------------------------------------
// P-73 多机同事件间接校时（拍板：允许多跳+取证链全记录入报告+成环禁止+
// 事件名必填+累积容差如实呈现；间接校准=相对传递，与绝对校时视觉区分）
// ---------------------------------------------------------------------------
void MultiCamPlaybackWindow::onEventCalibToggled(bool on)
{
    if (on) {
        if (m_aligning) {
            m_statusLabel->setText(lang("对齐会话进行中，先结束对齐",
                                        "Finish alignment first"));
            QSignalBlocker b(m_eventCalibBtn);
            m_eventCalibBtn->setChecked(false);
            return;
        }
        bool anyCal = false;
        for (const auto &l : m_svc->lanes())
            if (l.calibrated)
                anyCal = true;
        if (!lanesLoaded() || m_svc->laneCount() < 2 || !anyCal) {
            m_statusLabel->setText(lang(
                "同事件对时需要：≥2 路已加载，且至少一路已校时（作参考路）",
                "Need 2+ loaded lanes with at least one calibrated"));
            QSignalBlocker b(m_eventCalibBtn);
            m_eventCalibBtn->setChecked(false);
            return;
        }
        m_eventCalib = true;
        m_ecSaved = false;
        m_ecPreviewed = false;
        m_ecTargetIdx = -1;
        m_ecAnchors.clear();
        m_ecPlaying = false;            // 沙盒播放态复位（服务保持暂停）
        if (m_ecBothAudio) {
            QSignalBlocker b(m_ecBothAudio);
            m_ecBothAudio->setChecked(false);
        }
        m_svc->clearCustomAudible();
        m_svc->pause();
        m_playBtn->setEnabled(true);
        m_playBtn->setText(lang("▶ 播放", "▶ Play"));
        rebuildTimelineArea();          // 对时模式强制每路独立进度条
        for (auto *b : m_bars)
            if (b)
                b->setEnabled(true);    // 各条独立拖（valueChanged 直驱引擎）
        buildEventCalibPanel();
        refreshEcPanel();
        m_ecHost->show();
        refreshModeControls();
        m_statusLabel->setText(lang(
            "对时模式：跟着右侧面板 ①→④ 走，随时看顶部横幅提示",
            "Event sync: follow steps 1-4 in the right panel"));
    } else {
        onEcExit();
    }
}

void MultiCamPlaybackWindow::onEcExit()
{
    if (!m_eventCalib && !m_ecHost)
        return;
    m_eventCalib = false;
    m_ecAnchors.clear();
    // 沙盒收场：停播 + 停跟随定时器 + 还单可听路 + 播放钮回位
    if (m_ecTick)
        m_ecTick->stop();
    if (m_ecPlaying) {
        m_ecPlaying = false;
        for (int i = 0; i < m_svc->laneCount(); ++i)
            if (auto *e = m_svc->engineAt(i))
                e->pause();
    }
    m_playBtn->setText(lang("▶ 播放", "▶ Play"));
    if (m_svc) {
        m_svc->clearCustomAudible();
        for (int k = 0; k < m_tiles.size(); ++k)
            if (m_tiles[k])
                m_tiles[k]->setAudible(k == m_svc->audibleLane());
    }
    if (m_ecHost)
        m_ecHost->hide();
    rebuildTimelineArea();              // 恢复合并条/联动条
    refreshModeControls();
    if (m_eventCalibBtn) {
        QSignalBlocker b(m_eventCalibBtn);
        m_eventCalibBtn->setChecked(false);
    }
    if (!m_ecSaved && m_ecPreviewed)
        m_statusLabel->setText(lang(
            "对时预览仅本会话生效（未保存，关窗即还原）",
            "Preview applies to this session only (unsaved)"));
}

void MultiCamPlaybackWindow::buildEventCalibPanel()
{
    if (m_ecPanel)
        return;
    auto *lay = new QVBoxLayout(m_ecHost);
    lay->setContentsMargins(8, 8, 8, 8);
    m_ecPanel = new QWidget(m_ecHost);
    lay->addWidget(m_ecPanel);
    auto *v = new QVBoxLayout(m_ecPanel);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    auto *title = new QLabel(lang("同事件对时", "Event Sync"), m_ecPanel);
    QFont tf = title->font();
    tf.setBold(true);
    tf.setPointSize(tf.pointSize() + 1);
    title->setFont(tf);
    v->addWidget(title);
    auto *principle = new QLabel(
        lang("原理一句话：在两路画面里找到「同一件事的同一瞬间」告诉软件，"
             "就能算出要修的那路钟差多少。找到 1 次能修整体偏差；找到 2 次"
             "还能修越走越快/慢（第 2 次自愿，不强制）。",
             "Mark the same instant of the same event in both videos to "
             "compute the clock error. 1 mark fixes offset; a 2nd (optional) "
             "also fixes speed drift."),
        m_ecPanel);
    principle->setWordWrap(true);
    principle->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    v->addWidget(principle);

    // 状态横幅：永远告诉用户「现在该干嘛」（updateEcGuidance 驱动）
    m_ecBanner = new QLabel(m_ecPanel);
    m_ecBanner->setWordWrap(true);
    m_ecBanner->setStyleSheet(
        QStringLiteral("background:%1; color:%2; padding:6px; border-radius:4px;")
            .arg(Theme::Accent, Theme::AccentOnDark));
    v->addWidget(m_ecBanner);

    // ---- ① 谁的钟是准的 ----
    auto *s1 = new QLabel(lang("① 谁的钟是准的？（已校时的机位）",
                               "1) Whose clock is right? (calibrated lane)"),
                          m_ecPanel);
    QFont bf = s1->font();
    bf.setBold(true);
    s1->setFont(bf);
    v->addWidget(s1);
    m_ecRefCombo = new QComboBox(m_ecPanel);
    v->addWidget(m_ecRefCombo);
    auto *refStep = new QHBoxLayout();
    refStep->addWidget(new QLabel(lang("微调画面：", "Nudge:"), m_ecPanel));
    auto *refBack = new QPushButton(lang("◀ 上一帧", "◀ frame"), m_ecPanel);
    auto *refFwd = new QPushButton(lang("下一帧 ▶", "frame ▶"), m_ecPanel);
    connect(refBack, &QPushButton::clicked, this, [this]() { ecFrameStep(true, -1); });
    connect(refFwd, &QPushButton::clicked, this, [this]() { ecFrameStep(true, +1); });
    refStep->addWidget(refBack);
    refStep->addWidget(refFwd);
    v->addLayout(refStep);

    // ---- ② 要修谁的钟 ----
    auto *s2 = new QLabel(lang("② 要修谁的钟？（时间不准的机位）",
                               "2) Whose clock to fix?"),
                          m_ecPanel);
    s2->setFont(bf);
    v->addWidget(s2);
    m_ecTargetCombo = new QComboBox(m_ecPanel);
    v->addWidget(m_ecTargetCombo);
    auto *tgtStep = new QHBoxLayout();
    tgtStep->addWidget(new QLabel(lang("微调画面：", "Nudge:"), m_ecPanel));
    auto *tgtBack = new QPushButton(lang("◀ 上一帧", "◀ frame"), m_ecPanel);
    auto *tgtFwd = new QPushButton(lang("下一帧 ▶", "frame ▶"), m_ecPanel);
    connect(tgtBack, &QPushButton::clicked, this, [this]() { ecFrameStep(false, -1); });
    connect(tgtFwd, &QPushButton::clicked, this, [this]() { ecFrameStep(false, +1); });
    tgtStep->addWidget(tgtBack);
    tgtStep->addWidget(tgtFwd);
    v->addLayout(tgtStep);

    // ---- ③ 打「同一瞬间」标记（两路选好才解锁）----
    m_ecStep3 = new QWidget(m_ecPanel);
    auto *v3 = new QVBoxLayout(m_ecStep3);
    v3->setContentsMargins(0, 4, 0, 0);
    v3->setSpacing(4);
    auto *s3 = new QLabel(lang("③ 找一件两路都看得见的事，把两边都播到那个瞬间",
                               "3) Find one event visible in both, scrub both "
                               "to that instant"),
                          m_ecStep3);
    s3->setFont(bf);
    s3->setWordWrap(true);
    v3->addWidget(s3);
    auto *eg = new QLabel(
        lang("例如：同一辆车开过门口 / 同一声喇叭 / 有人挥手 / 灯亮灯灭。"
             "用上面「微调画面」逐帧对到最准的一瞬。",
             "e.g. same car passing, same horn, a wave, a light flipping."),
        m_ecStep3);
    eg->setWordWrap(true);
    eg->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    v3->addWidget(eg);
    m_ecBothAudio = new QCheckBox(
        lang("🔊 同时听这两路的声音（比对喇叭/轰鸣类声音事件用）",
             "🔊 Hear both lanes at once (for sound events)"),
        m_ecStep3);
    connect(m_ecBothAudio, &QCheckBox::toggled, this, [this](bool on) {
        const int ri = m_ecRefCombo->currentData().toInt();
        const int ti = m_ecTargetCombo->currentData().toInt();
        if (on && ri >= 0 && ti >= 0) {
            m_svc->setCustomAudible({ri, ti});
            for (int k = 0; k < m_tiles.size(); ++k)
                if (m_tiles[k])
                    m_tiles[k]->setAudible(k == ri || k == ti);
        } else {
            m_svc->clearCustomAudible();
            for (int k = 0; k < m_tiles.size(); ++k)
                if (m_tiles[k])
                    m_tiles[k]->setAudible(k == m_svc->audibleLane());
        }
    });
    v3->addWidget(m_ecBothAudio);
    m_ecEventName = new QLineEdit(m_ecStep3);
    m_ecEventName->setPlaceholderText(
        lang("给这件事起个名（必填，报告里要念出来），如：白车过东门",
             "Name the event (required, appears in report)"));
    v3->addWidget(m_ecEventName);
    m_ecAddBtn = new QPushButton(lang("📍 就是这一瞬！打标记",
                                      "📍 This instant! Mark it"),
                                 m_ecStep3);
    {
        QFont f = m_ecAddBtn->font();
        f.setBold(true);
        f.setPointSize(f.pointSize() + 1);
        m_ecAddBtn->setFont(f);
    }
    m_ecAddBtn->setStyleSheet(
        QStringLiteral("background:%1; color:%2; padding:6px;")
            .arg(Theme::Accent, Theme::AccentOnDark));
    connect(m_ecAddBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcAddAnchor);
    v3->addWidget(m_ecAddBtn);
    v->addWidget(m_ecStep3);

    // ---- ④ 标记列表 + 预览/保存（有 1 个标记即解锁）----
    m_ecStep4 = new QWidget(m_ecPanel);
    auto *v4 = new QVBoxLayout(m_ecStep4);
    v4->setContentsMargins(0, 4, 0, 0);
    v4->setSpacing(4);
    auto *s4 = new QLabel(lang("④ 已记下的同一瞬间", "4) Marked instants"),
                          m_ecStep4);
    s4->setFont(bf);
    v4->addWidget(s4);
    m_ecAnchorList = new QListWidget(m_ecStep4);
    v4->addWidget(m_ecAnchorList, 1);
    auto *delBtn = new QPushButton(lang("删除选中的标记", "Delete selected"),
                                   m_ecStep4);
    connect(delBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcRemoveAnchor);
    v4->addWidget(delBtn);
    m_ecHint2 = new QLabel(
        lang("1 个标记就能修「整体偏差」。如果这路越播越偏（走时快/慢），"
             "再找一个瞬间打第 2 个标记（自愿）。",
             "1 mark fixes the overall offset. If this lane drifts over "
             "time, add a 2nd mark (optional)."),
        m_ecStep4);
    m_ecHint2->setWordWrap(true);
    m_ecHint2->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Info));
    v4->addWidget(m_ecHint2);
    m_ecFitBtn = new QPushButton(lang("▶ 应用预览（播放试看两路是否贴齐）",
                                      "▶ Apply preview"),
                                 m_ecStep4);
    connect(m_ecFitBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcFitPreview);
    v4->addWidget(m_ecFitBtn);
    m_ecSaveBtn = new QPushButton(lang("💾 看着贴齐了，保存校时",
                                       "💾 Looks aligned - save"),
                                  m_ecStep4);
    // v1.15.3 用户实测：旧实现 setEnabled(false) 但样式表绿底不随禁用态
    // 变化——按钮看着能点、点了无反应。改常可点：未预览时点击出指引，
    // 样式随预览态灰/绿切换（updateEcSaveBtn）。
    updateEcSaveBtn();
    connect(m_ecSaveBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcSave);
    v4->addWidget(m_ecSaveBtn);
    v->addWidget(m_ecStep4);

    auto *exitBtn = new QPushButton(lang("退出对时", "Exit"), m_ecPanel);
    connect(exitBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcExit);
    v->addWidget(exitBtn);

    m_ecStatus = new QLabel(m_ecPanel);
    m_ecStatus->setWordWrap(true);
    m_ecStatus->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Accent));
    v->addWidget(m_ecStatus);

    connect(m_ecRefCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshEcPanel(); });
    connect(m_ecTargetCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshEcPanel(); });
}

void MultiCamPlaybackWindow::refreshEcPanel()
{
    if (!m_ecPanel)
        return;
    const auto &lanes = m_svc->lanes();
    const int keepRef = m_ecRefCombo->currentData().isValid()
        ? m_ecRefCombo->currentData().toInt() : -1;
    const int keepTgt = m_ecTargetCombo->currentData().isValid()
        ? m_ecTargetCombo->currentData().toInt() : -1;
    {
        QSignalBlocker b1(m_ecRefCombo), b2(m_ecTargetCombo);
        m_ecRefCombo->clear();
        for (int i = 0; i < lanes.size(); ++i) {
            if (!lanes[i].calibrated)
                continue;
            const bool indirect =
                lanes[i].cal.source == TimeCalibration::Source::CrossCamEvent;
            m_ecRefCombo->addItem(
                lanes[i].displayName
                + (indirect ? lang("（经别的机位接力对过）", " (via relay)")
                           : QString()),
                i);
        }
        // 先还原参考路选择再填目标下拉——clear() 把参考当前项重置为 0
        // （第一路），若先填目标会错把第一路永久排除（真机实测「要修的钟
        // 选不了第一路」根因）
        const int ri = m_ecRefCombo->findData(keepRef);
        if (ri >= 0)
            m_ecRefCombo->setCurrentIndex(ri);
        m_ecTargetCombo->clear();
        for (int i = 0; i < lanes.size(); ++i) {
            if (i == m_ecRefCombo->currentData().toInt())
                continue;   // 准钟不能同时是要修的钟
            m_ecTargetCombo->addItem(lanes[i].displayName, i);
        }
        const int ti = m_ecTargetCombo->findData(keepTgt);
        if (ti >= 0)
            m_ecTargetCombo->setCurrentIndex(ti);
    }
    // 目标路切换 → 装载其既有标记（同事件校时路），否则清空会话表
    const int tgt = m_ecTargetCombo->count() > 0
        ? m_ecTargetCombo->currentData().toInt() : -1;
    if (tgt != m_ecTargetIdx) {
        m_ecTargetIdx = tgt;
        m_ecPreviewed = false;
        m_ecSaved = false;
        m_ecAnchors.clear();
        if (tgt >= 0 && tgt < lanes.size()
            && lanes[tgt].cal.source == TimeCalibration::Source::CrossCamEvent)
            m_ecAnchors = lanes[tgt].cal.eventAnchors;
        if (m_ecSaveBtn)
            updateEcSaveBtn();
    }
    // 标记列表（口语行；预览后附对时误差，>半秒红字提醒）
    m_ecAnchorList->clear();
    int vi = 0;   // 有效标记序（残差与之对齐；UI 只收有效标记，1:1）
    for (const auto &a : m_ecAnchors) {
        const QString wall = QDateTime::fromMSecsSinceEpoch(a.refWallMs)
            .toString(QStringLiteral("MM-dd HH:mm:ss"));
        const QString tstream = QTime(0, 0).addMSecs(a.targetStreamMs)
            .toString(QStringLiteral("HH:mm:ss"));
        QString row = QStringLiteral("「%1」准钟墙钟 %2 ⇄ 本路画面 %3")
            .arg(a.eventName, wall, tstream);
        if (m_ecPreviewed && vi < m_ecFit.residualsMs.size()) {
            const double res = m_ecFit.residualsMs[vi];
            row += QStringLiteral("（误差 %1 秒）")
                .arg(res / 1000.0, 0, 'f', 2);
            if (res > 500.0)
                row += lang(" ⚠误差偏大，这对标记可能找错事了",
                            " ⚠large error - wrong instant?");
        }
        ++vi;
        m_ecAnchorList->addItem(row);
    }
    // 「同听两路」勾选中：参考/目标换路 → 可听集合跟随重挂
    if (m_ecBothAudio && m_ecBothAudio->isChecked()) {
        const int ri = m_ecRefCombo->currentData().toInt();
        const int ti = m_ecTargetCombo->currentData().toInt();
        if (ri >= 0 && ti >= 0) {
            m_svc->setCustomAudible({ri, ti});
            for (int k = 0; k < m_tiles.size(); ++k)
                if (m_tiles[k])
                    m_tiles[k]->setAudible(k == ri || k == ti);
        }
    }
    updateEcGuidance();
}

void MultiCamPlaybackWindow::updateEcGuidance()
{
    if (!m_ecPanel)
        return;
    const bool picked = m_ecRefCombo->currentData().isValid()
        && m_ecRefCombo->currentData().toInt() >= 0
        && m_ecTargetCombo->currentData().isValid()
        && m_ecTargetCombo->currentData().toInt() >= 0;
    const int step = eventcalib::guidanceStep(picked, m_ecAnchors.size(),
                                              m_ecPreviewed);
    if (m_ecStep3)
        m_ecStep3->setEnabled(step >= 1);
    if (m_ecStep4)
        m_ecStep4->setEnabled(step >= 2);
    if (m_ecHint2)
        m_ecHint2->setVisible(m_ecAnchors.size() == 1 && !m_ecPreviewed);
    if (m_ecBanner) {
        QString txt;
        switch (step) {
        case 0:
            txt = lang("🧭 现在：第①②步——上面选好「准钟」和「要修的钟」",
                       "🧭 Step 1-2: pick the good clock and the one to fix");
            break;
        case 1:
            txt = lang("🧭 现在：第③步——找一件两路都看得见的事，把两边画面"
                       "都播到那个瞬间，起个名，点「📍 就是这一瞬！」",
                       "🧭 Step 3: find an event visible in both, scrub to "
                       "that instant, name it, mark it");
            break;
        case 2:
            txt = lang("🧭 现在：可以点「▶ 应用预览」了。若目标路越播越偏，"
                       "再找第二个瞬间打标记（自愿）",
                       "🧭 Ready to preview. Add a 2nd mark only if the "
                       "lane drifts (optional)");
            break;
        default:
            txt = lang("🧭 预览中：播放试看——两路贴齐了就点「💾 保存校时」；"
                       "没贴齐可删标记重打",
                       "🧭 Previewing: if both lanes stick together, save; "
                       "else delete and re-mark");
            break;
        }
        m_ecBanner->setText(txt);
    }
}

void MultiCamPlaybackWindow::ecFrameStep(bool refLane, int dir)
{
    if (!m_ecPanel)
        return;
    const int idx = refLane ? m_ecRefCombo->currentData().toInt()
                            : m_ecTargetCombo->currentData().toInt();
    if (idx < 0 || idx >= m_svc->laneCount())
        return;
    auto *e = m_svc->engineAt(idx);
    if (!e)
        return;
    const double fps = e->fps() > 1.0 ? e->fps() : 25.0;
    qint64 t = e->position() + dir * qRound64(1000.0 / fps);
    t = qBound<qint64>(0, t, m_svc->lanes()[idx].durationMs);
    e->seek(t);
    if (idx < m_bars.size() && m_bars[idx])
        m_bars[idx]->setValue(int(t));   // 进度条跟随（valueChanged 同值无害）
}

void MultiCamPlaybackWindow::onEcAddAnchor()
{
    if (!m_ecPanel)
        return;
    const QString name = m_ecEventName->text().trimmed();
    if (name.isEmpty()) {   // 拍板②：事件名必填
        m_ecStatus->setText(lang("先给这件事起个名（必填，报告里要念出来）",
                                 "Name the event first (required)"));
        m_ecEventName->setFocus();
        return;
    }
    const int ri = m_ecRefCombo->currentData().toInt();
    const int ti = m_ecTargetCombo->currentData().toInt();
    const auto &lanes = m_svc->lanes();
    if (ri < 0 || ti < 0 || ri >= lanes.size() || ti >= lanes.size()
        || !lanes[ri].calibrated)
        return;
    auto *er = m_svc->engineAt(ri);
    auto *et = m_svc->engineAt(ti);
    if (!er || !et)
        return;

    eventcalib::EventAnchor a;
    a.refLaneId = lanes[ri].id;
    a.refStreamMs = er->position();
    // 参考路墙钟快照（含北京时间口径；快照入锚，防参考校时后改链断）
    a.refWallMs = syncWallOf(lanes[ri], a.refStreamMs);
    a.targetStreamMs = et->position();
    a.eventName = name;
    a.markedAtMs = QDateTime::currentMSecsSinceEpoch();
    a.toleranceMs = eventcalib::frameToleranceMs(er->fps(), et->fps());

    // 成环守卫（拍板：多跳允许、成环禁止）：目标路锚点集=既有+会话
    QHash<QString, QVector<eventcalib::EventAnchor>> byLane;
    for (const auto &l : lanes)
        if (!l.cal.eventAnchors.isEmpty())
            byLane.insert(l.id, l.cal.eventAnchors);
    byLane[lanes[ti].id] = m_ecAnchors;
    if (eventcalib::wouldCreateCycle(lanes[ti].id, lanes[ri].id, byLane)) {
        QMessageBox::warning(this, lang("成环禁止", "Cycle rejected"),
            lang("该校时链会成环（%1 的参考链上游已出现 %2），取证链无法陈述。"
                 "请改选其他参考路。",
                 "This link would create a cycle in the provenance chain. "
                 "Pick another reference lane.")
                .arg(lanes[ri].displayName, lanes[ti].displayName));
        return;
    }
    m_ecAnchors.append(a);
    m_ecPreviewed = false;              // 锚点变了须重新拟合
    updateEcSaveBtn();
    m_ecEventName->clear();
    refreshEcPanel();
    m_ecStatus->setText(lang("已记下第 %1 个瞬间：「%2」✅",
                             "Marked instant #%1: %2")
        .arg(m_ecAnchors.size()).arg(name));
}

void MultiCamPlaybackWindow::onEcRemoveAnchor()
{
    const int row = m_ecAnchorList ? m_ecAnchorList->currentRow() : -1;
    if (row < 0 || row >= m_ecAnchors.size())
        return;
    m_ecAnchors.removeAt(row);
    m_ecPreviewed = false;
    updateEcSaveBtn();
    refreshEcPanel();
}

void MultiCamPlaybackWindow::onEcFitPreview()
{
    if (!m_ecPanel || m_ecTargetIdx < 0)
        return;
    m_ecFit = eventcalib::fitAnchors(m_ecAnchors);
    if (!m_ecFit.ok) {
        m_ecStatus->setText(m_ecFit.error);   // C1：类型化错误原文上屏
        return;
    }
    // v1.15.3：应用前抓目标路旧校时——修正量要跟旧钟比（旧 bug 把 epoch
    // 毫秒偏移当钟差念：目标路已有校时时会出现「慢 495740 小时」疯话）
    const TimeCalibration oldCal =
        (m_ecTargetIdx < m_svc->lanes().size())
            ? m_svc->lanes()[m_ecTargetIdx].cal : TimeCalibration();
    TimeCalibration cal;
    cal.source = TimeCalibration::Source::CrossCamEvent;
    cal.dateKnown = true;
    cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
    cal.eventAnchors = m_ecAnchors;           // 溯源链随校时一体入 .vla
    cal.sigmaRate = m_ecFit.sigmaRate;
    if (m_ecFit.affine) {
        cal.rate = m_ecFit.rate;
        cal.rateApplied = true;
        cal.offsetMs = qRound64(m_ecFit.interceptMs);
    } else {
        cal.rate = 1.0;
        cal.rateApplied = false;
        cal.offsetMs = qRound64(m_ecFit.offsetMs);
    }
    // 置信度：间接校准天然低一档——单锚 0.6，多锚低残差最多 0.8
    double conf = 0.6;
    if (m_ecAnchors.size() >= 2) {
        double maxRes = 0;
        for (double r : m_ecFit.residualsMs)
            maxRes = qMax(maxRes, r);
        conf = (maxRes <= 200.0) ? 0.8 : 0.7;
    }
    cal.conf = conf;
    m_ecCal = cal;
    m_svc->applyLaneCalibration(m_ecTargetIdx, cal);   // 预览：播放联动即生效
    // 修正量人话口径：与旧校时在首个标记瞬间的墙钟差；原先无有效校时则
    // 如实说「按参考路对齐」（不能编钟差）
    {
        const auto &a0 = m_ecAnchors.first();
        const qint64 newWall = cal.wallMsOf(a0.targetStreamMs);
        if (oldCal.isValid() && oldCal.dateKnown) {
            const qint64 delta = newWall - oldCal.beijingMsOf(a0.targetStreamMs);
            if (qAbs(delta) < 1000) {
                m_ecCorrText = lang("目标路的钟原本就基本准（差不到 1 秒），已对齐",
                                    "lane clock was already within 1s, aligned");
            } else {
                m_ecCorrText = lang("目标的钟原来%1，已按此修正",
                                    "lane clock was off by %1, fixed")
                    .arg(eventcalib::plainClockDeltaText(delta)
                             .replace(QStringLiteral("目标的钟"),
                                      QStringLiteral("")));
            }
        } else {
            m_ecCorrText = lang("目标路原先没有有效校时——已按参考路对齐到墙钟 %1",
                                "no prior calibration; aligned to ref clock %1")
                .arg(QDateTime::fromMSecsSinceEpoch(newWall)
                         .toString(QStringLiteral("MM-dd HH:mm:ss")));
        }
    }
    // 预览即所见（用户实测「点完没反应」）：两路立即跳到同一墙钟瞬间，
    // 暂停中也当场看到贴齐与否
    {
        const int ri = m_ecRefCombo->currentData().toInt();
        if (ri >= 0 && ri < m_svc->lanes().size())
            if (auto *er = m_svc->engineAt(ri))
                m_svc->seekWall(syncWallOf(m_svc->lanes()[ri], er->position()));
    }
    m_ecPreviewed = true;
    updateEcSaveBtn();
    refreshEcPanel();
    double maxRes = 0;
    for (double r : m_ecFit.residualsMs)
        maxRes = qMax(maxRes, r);
    QString how = m_ecCorrText;
    if (m_ecFit.affine) {
        const double driftDay = (m_ecFit.rate - 1.0) * 86400.0;
        how += lang("；快慢：每 1 天%1约 %2 秒，已一并修正",
                    "; drift %1 s/day, fixed")
            .arg(driftDay >= 0 ? lang("快", "fast") : lang("慢", "slow"))
            .arg(qAbs(driftDay), 0, 'f', 1);
    }
    m_ecStatus->setText(lang("预览中：%1。播放试看两路是否贴齐，贴齐就保存。"
                             "（最大误差 %2 秒 · 可信度 %3，间接对时如实标注）\n"
                             "注：画面烧录时间是主机原始读数不会变，对齐看底部墙钟。",
                             "Previewing: %1. max err %2s, conf %3"),
        .arg(how).arg(maxRes / 1000.0, 0, 'f', 2)
        .arg(cal.conf, 0, 'f', 2));
}

void MultiCamPlaybackWindow::updateEcSaveBtn()
{
    if (!m_ecSaveBtn)
        return;
    if (m_ecPreviewed) {
        m_ecSaveBtn->setStyleSheet(
            QStringLiteral("background:%1; color:%2; padding:6px; "
                           "font-weight:bold;")
                .arg(Theme::Success, Theme::AccentOnDark));
    } else {
        m_ecSaveBtn->setStyleSheet(
            QStringLiteral("background:%1; color:%2; padding:6px;")
                .arg(Theme::BgPanel, Theme::TextSecond));
    }
}

void MultiCamPlaybackWindow::onEcSave()
{
    // v1.15.3：守卫不再静默——按步骤指引（旧：禁用态点击零反馈，用户困惑）
    if (m_ecTargetIdx < 0) {
        m_ecStatus->setText(lang("先在上面选好「要修的钟」（目标路）",
                                 "Pick the target lane first"));
        return;
    }
    if (!m_ecPreviewed) {
        QMessageBox::information(this, lang("还不能保存", "Not ready"),
            lang(QStringLiteral("保存前请先完成：\n"
                 "① 上面选好「准钟」与「要修的钟」\n"
                 "② 两路都停在同一事件瞬间，点「记下这个瞬间」\n"
                 "③ 点「▶ 应用预览」，播放试看两路是否贴齐\n"
                 "贴齐后再点本按钮保存。"),
                 "Mark the instant first, then Apply Preview, then save."));
        return;
    }
    const auto &lanes = m_svc->lanes();
    const auto &tgt = lanes[m_ecTargetIdx];

    // ---- 取证链展开（确认卡独立小节，拍板③）----
    QHash<QString, QVector<eventcalib::EventAnchor>> byLane;
    QSet<QString> absIds;
    for (const auto &l : lanes) {
        if (!l.cal.eventAnchors.isEmpty())
            byLane.insert(l.id, l.cal.eventAnchors);
        if (l.calibrated
            && l.cal.source != TimeCalibration::Source::CrossCamEvent)
            absIds.insert(l.id);
    }
    byLane[tgt.id] = m_ecCal.eventAnchors;
    const auto chain = eventcalib::expandChain(tgt.id, byLane, absIds);
    const qint64 cumTol = eventcalib::cumulativeToleranceMs(chain);

    auto laneName = [this](const QString &id) {
        for (const auto &l : m_svc->lanes())
            if (l.id == id)
                return l.displayName;
        return id;
    };
    QString chainText;
    int depth = 0;
    for (const auto &hop : chain) {
        const QString indent(depth * 2, QLatin1Char(' '));
        if (hop.absolute) {
            chainText += indent + QStringLiteral("■ %1（基准：本机已直接对时）\n")
                .arg(laneName(hop.laneId));
        } else {
            const auto &a = hop.anchor;
            chainText += indent + QStringLiteral("● %1（间接对时，对表误差 ±%2 秒）\n")
                .arg(laneName(hop.laneId)).arg(a.toleranceMs / 1000.0, 0, 'f', 2);
            chainText += indent + QStringLiteral(
                "   事件「%1」：本路画面 %2 ↔ 参考 %3（墙钟 %4）\n")
                .arg(a.eventName)
                .arg(QTime(0, 0).addMSecs(a.targetStreamMs)
                         .toString(QStringLiteral("HH:mm:ss")))
                .arg(laneName(a.refLaneId))
                .arg(QDateTime::fromMSecsSinceEpoch(a.refWallMs)
                         .toString(QStringLiteral("MM-dd HH:mm:ss")));
        }
        ++depth;
    }
    chainText += QStringLiteral("本次对时最大可能误差：±%1 秒（逐跳对帧容差如实累加）")
        .arg(cumTol / 1000.0, 0, 'f', 2);

    // v1.15.3 用户拍板：确认卡改大白话（旧：速率 0.99980/偏移 epoch 毫秒
    // 原值/置信 0.80 看不懂）。单锚点明确「整体平移」，多锚点「平移+快慢」。
    double maxRes = 0;
    for (double r : m_ecFit.residualsMs)
        maxRes = qMax(maxRes, r);
    const QString refName = !m_ecCal.eventAnchors.isEmpty()
        ? laneName(m_ecCal.eventAnchors.first().refLaneId) : QString();
    QString summary = lang("「%1」将按「%2」对时（间接对时）\n",
                           "Lane %1 aligns to %2 (indirect)\n")
        .arg(tgt.displayName, refName);
    if (m_ecFit.affine) {
        const double driftDay = (m_ecCal.rate - 1.0) * 86400.0;
        summary += lang("方式：平移 + 快慢修正（%1 个标记）\n",
                        "Mode: shift + rate (%1 marks)\n")
            .arg(m_ecCal.eventAnchors.size());
        summary += lang("快慢：目标的钟每 1 天%1约 %2 秒，已一并修正\n",
                        "Drift: %1 s/day, corrected\n")
            .arg(driftDay >= 0 ? lang("快", "fast") : lang("慢", "slow"))
            .arg(qAbs(driftDay), 0, 'f', 1);
    } else {
        summary += lang("方式：整体平移（1 个标记，不改快慢）\n",
                        "Mode: fixed shift (1 mark)\n");
    }
    summary += lang("修正量：%1\n", "Correction: %1\n")
        .arg(m_ecCorrText);
    summary += lang("标记瞬间的对齐误差：最大 %1 秒\n", "Max residual: %1 s\n")
        .arg(maxRes / 1000.0, 0, 'f', 2);
    summary += lang("可信度：%1（间接对时如实降一档）\n",
                    "Confidence: %1 (indirect)\n")
        .arg(m_ecCal.conf, 0, 'f', 2);
    // v1.15.3 用户实测误解：拿画面烧录的 OSD 数字当对齐判据。
    // 固定口径注记（OSD=主机原始钟烧死在像素里永不变；对齐看墙钟/内容）
    summary += lang("注：画面里烧录的时间是监控主机原始读数（不会变）；\n"
                    "对没对齐请看底部「墙钟」和同一物体是否同现。",
                    "Note: burned-in OSD never changes; judge by wall clock "
                    "and scene content.");

    QMessageBox box(QMessageBox::Question,
                    lang("确认保存校时", "Confirm save"), summary, QMessageBox::NoButton,
                    this);
    auto *chainLabel = new QLabel(chainText, &box);
    chainLabel->setWordWrap(true);
    chainLabel->setStyleSheet(
        QStringLiteral("color:%1; font-family:monospace;").arg(Theme::TextSecond));
    // 取证链独立小节置于按钮上方
    if (auto *gl = qobject_cast<QGridLayout *>(box.layout()))
        gl->addWidget(chainLabel, gl->rowCount(), 0, 1, gl->columnCount());
    QPushButton *saveBtn = box.addButton(lang("保存入案件/侧车", "Save"),
                                         QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != saveBtn)
        return;

    // ---- 落盘：载旧 .vla 全字段 → 仅替换校时 → 写回（保其他分析成果）----
    ProjectIO io(const_cast<CaseManager *>(m_case), nullptr, this);
    const QString path = io.suggestSavePath(tgt.path);
    ProjectIO::VlaSaveRequest req;
    if (QFile::exists(path)) {
        ProjectIO::LoadedVla lv;
        if (io.loadVla(path, &lv)) {
            req.regions = lv.regions;
            req.magnifierRect = lv.magnifierRect;
            req.labels = lv.labels;
            req.pinnedRect = lv.pinnedRect;
            req.fusion = lv.fusion;
            req.polygons = lv.polygons;
            req.guideLines = lv.guideLines;
            req.regionRoiIds = lv.regionRoiIds;
            req.polygonRoiIds = lv.polygonRoiIds;
            req.abRegion = lv.abRegion;
            req.speedPlan = lv.speedPlan;
        }
    }
    req.calibration = m_ecCal;
    if (!io.saveVlaNow(path, req)) {
        QMessageBox::critical(this, lang("保存失败", "Save failed"),
            QStringLiteral("EVENTCALIB_SAVE_IO: %1").arg(path));   // C1/C2
        return;
    }
    m_ecSaved = true;
    m_ecStatus->setText(lang("已保存：%1（取证链随校时入档）", "Saved: %1").arg(path));
    // v1.15.3 用户实测：保存成功零反馈，"出去了也不知道成没成"。
    // 三件补救：案件树 ⏰ 徽标即同步 + 成功弹窗 + 主窗回调刷新
    if (m_case) {
        auto *cm = const_cast<CaseManager *>(m_case);
        cm->updateCalibrationBadge(
            tgt.path, true, ProjectIO::calibrationBadgeSummary(m_ecCal));
    }
    if (onCaseDataChanged)
        onCaseDataChanged(tgt.path);
    QMessageBox::information(this, lang("校时已保存", "Calibration saved"),
        lang(QStringLiteral("目标路「%1」的同事件校时已保存入案件/侧车。\n"
             "累积容差 ±%2ms 已如实随档（取证链可查）。"),
             "Calibration saved for lane %1. cumulative tolerance %2ms.")
            .arg(tgt.displayName).arg(cumTol));
    refreshEcPanel();
}
