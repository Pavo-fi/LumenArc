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
        else if (!l.path.isEmpty() && !m_svc->laneCoversNow(i))
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
        m_svc->pause();
        rebuildTimelineArea();          // 对时模式强制每路独立进度条
        for (auto *b : m_bars)
            if (b)
                b->setEnabled(true);    // 各条独立拖（valueChanged 直驱引擎）
        buildEventCalibPanel();
        refreshEcPanel();
        m_ecHost->show();
        refreshModeControls();
        m_statusLabel->setText(lang(
            "对时模式：分别拖两路进度条/帧步进，把两路画面对到同一事件",
            "Event sync: scrub/step each lane to the same event"));
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

    auto *title = new QLabel(lang("同事件对时（间接校时）", "Event Sync (indirect)"),
                             m_ecPanel);
    QFont tf = title->font();
    tf.setBold(true);
    title->setFont(tf);
    v->addWidget(title);

    auto *hint = new QLabel(
        lang("① 选参考路（已校时）与目标路；② 分别拖进度条/帧步进，把两路"
             "画面对到同一现实事件（同帧）；③ 填事件名点「建锚」；"
             "④ 建议≥2 个远距离锚点；⑤「生成校时并预览」联动试看，"
             "确认后保存。多跳允许，成环禁止；取证链随校时入案件留档。",
             "1) Pick reference (calibrated) & target lanes; 2) scrub/step "
             "both to the same event frame; 3) name the event & add anchor; "
             "4) 2+ far-apart anchors recommended; 5) fit & preview, then save."),
        m_ecPanel);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    v->addWidget(hint);

    v->addWidget(new QLabel(lang("参考路（已校时）：", "Reference (calibrated):"),
                            m_ecPanel));
    m_ecRefCombo = new QComboBox(m_ecPanel);
    v->addWidget(m_ecRefCombo);
    auto *refStep = new QHBoxLayout();
    auto *refBack = new QPushButton(lang("◀ 帧", "◀f"), m_ecPanel);
    auto *refFwd = new QPushButton(lang("帧 ▶", "f▶"), m_ecPanel);
    connect(refBack, &QPushButton::clicked, this, [this]() { ecFrameStep(true, -1); });
    connect(refFwd, &QPushButton::clicked, this, [this]() { ecFrameStep(true, +1); });
    refStep->addWidget(refBack);
    refStep->addWidget(refFwd);
    v->addLayout(refStep);

    v->addWidget(new QLabel(lang("目标路（待校时）：", "Target:"), m_ecPanel));
    m_ecTargetCombo = new QComboBox(m_ecPanel);
    v->addWidget(m_ecTargetCombo);
    auto *tgtStep = new QHBoxLayout();
    auto *tgtBack = new QPushButton(lang("◀ 帧", "◀f"), m_ecPanel);
    auto *tgtFwd = new QPushButton(lang("帧 ▶", "f▶"), m_ecPanel);
    connect(tgtBack, &QPushButton::clicked, this, [this]() { ecFrameStep(false, -1); });
    connect(tgtFwd, &QPushButton::clicked, this, [this]() { ecFrameStep(false, +1); });
    tgtStep->addWidget(tgtBack);
    tgtStep->addWidget(tgtFwd);
    v->addLayout(tgtStep);

    m_ecEventName = new QLineEdit(m_ecPanel);
    m_ecEventName->setPlaceholderText(
        lang("事件名（必填）：如 黑衣男子推开东门", "Event name (required)"));
    v->addWidget(m_ecEventName);
    auto *addBtn = new QPushButton(lang("＋ 建锚（取两路当前帧）", "＋ Add anchor"),
                                   m_ecPanel);
    connect(addBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcAddAnchor);
    v->addWidget(addBtn);

    v->addWidget(new QLabel(lang("锚点表：", "Anchors:"), m_ecPanel));
    m_ecAnchorList = new QListWidget(m_ecPanel);
    v->addWidget(m_ecAnchorList, 1);
    auto *delBtn = new QPushButton(lang("删除选中锚点", "Delete selected"), m_ecPanel);
    connect(delBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcRemoveAnchor);
    v->addWidget(delBtn);

    auto *fitBtn = new QPushButton(lang("生成校时并预览", "Fit && Preview"),
                                   m_ecPanel);
    connect(fitBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcFitPreview);
    v->addWidget(fitBtn);
    m_ecSaveBtn = new QPushButton(lang("保存校时（取证链确认卡）", "Save…"),
                                  m_ecPanel);
    m_ecSaveBtn->setEnabled(false);
    connect(m_ecSaveBtn, &QPushButton::clicked, this,
            &MultiCamPlaybackWindow::onEcSave);
    v->addWidget(m_ecSaveBtn);
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
                + (indirect ? lang("（间接）", " (indirect)") : QString()),
                i);
        }
        m_ecTargetCombo->clear();
        for (int i = 0; i < lanes.size(); ++i) {
            if (i == m_ecRefCombo->currentData().toInt())
                continue;   // 参考路不能同时是目标路
            m_ecTargetCombo->addItem(lanes[i].displayName, i);
        }
        const int ri = m_ecRefCombo->findData(keepRef);
        if (ri >= 0)
            m_ecRefCombo->setCurrentIndex(ri);
        const int ti = m_ecTargetCombo->findData(keepTgt);
        if (ti >= 0)
            m_ecTargetCombo->setCurrentIndex(ti);
    }
    // 目标路切换 → 装载其既有锚点（同事件校时路），否则清空会话表
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
            m_ecSaveBtn->setEnabled(false);
    }
    // 锚点表渲染（残差来自最近预览拟合，仅在预览后显示）
    m_ecAnchorList->clear();
    int vi = 0;   // 有效锚点序（残差与之对齐；UI 只收有效锚点，1:1）
    for (const auto &a : m_ecAnchors) {
        const QString wall = QDateTime::fromMSecsSinceEpoch(a.refWallMs)
            .toString(QStringLiteral("MM-dd HH:mm:ss"));
        const qint64 ts = a.targetStreamMs;
        QString row = QStringLiteral("%1 | 参考 %2 ↔ 本路 %3:%4")
            .arg(a.eventName, wall)
            .arg(ts / 60000).arg((ts % 60000) / 1000, 2, 10, QLatin1Char('0'));
        if (m_ecPreviewed && vi < m_ecFit.residualsMs.size())
            row += QStringLiteral(" | 残差 %1ms")
                .arg(qRound(m_ecFit.residualsMs[vi]));
        ++vi;
        m_ecAnchorList->addItem(row);
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
        m_ecStatus->setText(lang("事件名必填（取证链入报告的可读性依赖）",
                                 "Event name is required (provenance)"));
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
    m_ecSaveBtn->setEnabled(false);
    m_ecEventName->clear();
    refreshEcPanel();
    m_ecStatus->setText(lang("锚点 %1 已建立：%2", "Anchor %1: %2")
        .arg(m_ecAnchors.size()).arg(name));
}

void MultiCamPlaybackWindow::onEcRemoveAnchor()
{
    const int row = m_ecAnchorList ? m_ecAnchorList->currentRow() : -1;
    if (row < 0 || row >= m_ecAnchors.size())
        return;
    m_ecAnchors.removeAt(row);
    m_ecPreviewed = false;
    m_ecSaveBtn->setEnabled(false);
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
    m_ecPreviewed = true;
    m_ecSaveBtn->setEnabled(true);
    refreshEcPanel();
    double maxRes = 0;
    for (double r : m_ecFit.residualsMs)
        maxRes = qMax(maxRes, r);
    m_ecStatus->setText(lang("预览中：%1 · 置信 %2 · 最大残差 %3ms。"
                             "播放联动试看，确认后「保存校时」。",
                             "Previewing: %1 · conf %2 · max residual %3ms.")
        .arg(m_ecFit.affine
             ? QStringLiteral("rate=%1").arg(m_ecFit.rate, 0, 'f', 5)
             : lang("恒定偏移", "fixed offset"))
        .arg(cal.conf, 0, 'f', 2).arg(qRound64(maxRes)));
}

void MultiCamPlaybackWindow::onEcSave()
{
    if (!m_ecPreviewed || m_ecTargetIdx < 0)
        return;
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
            chainText += indent + QStringLiteral("■ %1（%2）\n")
                .arg(laneName(hop.laneId),
                     lang("绝对校时锚", "absolute calibration"));
        } else {
            const auto &a = hop.anchor;
            chainText += indent + QStringLiteral("● %1（间接，容差 ±%2ms）\n")
                .arg(laneName(hop.laneId)).arg(a.toleranceMs);
            chainText += indent + QStringLiteral(
                "   事件「%1」：本路 %2 ↔ 参考 %3（墙钟 %4）\n")
                .arg(a.eventName)
                .arg(QTime(0, 0).addMSecs(a.targetStreamMs)
                         .toString(QStringLiteral("HH:mm:ss")))
                .arg(laneName(a.refLaneId))
                .arg(QDateTime::fromMSecsSinceEpoch(a.refWallMs)
                         .toString(QStringLiteral("MM-dd HH:mm:ss")));
        }
        ++depth;
    }
    chainText += QStringLiteral("累积容差：±%1ms（逐跳对帧容差之和，如实声明）")
        .arg(cumTol);

    double maxRes = 0;
    for (double r : m_ecFit.residualsMs)
        maxRes = qMax(maxRes, r);
    const QString summary = lang(
        "校时成果（间接校准·CrossCamEvent）\n"
        "模式：%1\n速率：%2 · 偏移：%3ms · 置信：%4\n锚点：%5 个 · 最大残差：%6ms",
        "Indirect calibration summary")
        .arg(m_ecFit.affine ? lang("仿射（多锚点）", "affine")
                            : lang("恒定偏移（单锚点）", "fixed offset"))
        .arg(m_ecCal.rate, 0, 'f', 5)
        .arg(m_ecCal.offsetMs)
        .arg(m_ecCal.conf, 0, 'f', 2)
        .arg(m_ecCal.eventAnchors.size())
        .arg(qRound64(maxRes));

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
    refreshEcPanel();
}
