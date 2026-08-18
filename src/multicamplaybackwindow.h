/**
 * @file multicamplaybackwindow.h
 * @brief 多机同步播放独立窗口（ui 层，P-57）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-18
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/MULTICAM_PLAYBACK_TECH_DESIGN_CN.md（v0.3 已拍板）；
 * P-59 流程重设（2026-08-18 用户布置）：案件模式开窗进**机位勾选面板**——
 * 直读案件视频+前处理产物清单，逐路实读 .vla 标识已校时/未校时，用户
 * 勾选 2-4 路后「开始同步播放」；校验引导内联（3 路以上须全校时/最多
 * 4 路/未校时路一键去校时），未校时路 2 路场景走临时对齐（不落盘）。
 * 独立大窗口：2-4 路瓦片网格 + 模式A 合并时间线（全部已校时，共享墙钟
 * 游标实时追逐）/ 模式B 分开进度条（2 路含临时进，带时间进度条，无分析
 * 面板）+ 瓦片放大镜（滚轮/中键）+ 单路切听 + 双击回单路分析。
 * 本窗口不持业务状态：同步/时钟/纠偏归 MultiCamSyncService（app 层）。
 */
#pragma once

#include <QDialog>
#include <QVector>
#include <QString>
#include <functional>
#include "domain/sync_model.h"
#include "app/cam_timeline.h"        // CamLane（模式A 条图块位）+ CamInventoryItem
#include "app/multicam_sync_service.h"

class CamTileWidget;
class MultiCamViewWidget;
class QSlider;
class QLabel;
class QPushButton;
class QCheckBox;
class QGridLayout;
class QVBoxLayout;
class QStackedLayout;
class QTimer;
class CaseManager;

class MultiCamPlaybackWindow : public QDialog
{
    Q_OBJECT
public:
    explicit MultiCamPlaybackWindow(QWidget *parent = nullptr);
    ~MultiCamPlaybackWindow() override;

    /// 引擎工厂（MainWindow 注入具体引擎；测试注入假引擎）
    void setEngineFactory(MultiCamSyncService::EngineFactory f);

    /// 案件模式：进机位勾选面板（P-59：清单全量+校时标识+勾选开始）
    bool openCaseLanes(const CaseManager &cm);
    /// 独立模式（无案件）：2 路空槽位，用户逐个选视频（模式B）
    void openStandalone();

    /// 双击瓦片回单路分析（U-6）
    std::function<void(const QString &path)> onOpenVideo;

    /// 为槽位装载视频（onPickVideo 的对话框后段；测试通道：绕过文件框直喂路径）
    void pickVideoForSlot(int slot, const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onTogglePlay();
    void onCycleRate();
    void onBackToStart();
    void onClock(qint64 wallMs);
    void onServiceState(MultiCamSyncService::State s);
    void onLaneInfo(int idx);
    void onLaneFailed(int idx, const QString &path);
    void onPerfNotice(const QString &msg);
    void onPickVideo(int slot);
    void onEnterAlign();
    void onConfirmAlign();
    void onCancelAlign();
    void onStartSync();       ///< 勾选面板「开始同步播放」（P-59）
    void onBackToPicker();    ///< 工具行「重选机位」
    void onRefreshPicker();   ///< 勾选面板「刷新清单」（校时后回来刷状态）

private:
    void buildUi();
    void buildPlayPage();
    void buildPickerPage();
    void refreshInventory();        ///< 重读案件清单+重建勾选行（默认勾已校时≤4）
    void updatePickerValidation();  ///< 勾选校验 + 模式预览 + 引导文案
    void rebuildTiles();
    void rebuildTimelineArea();
    void refreshModeControls();
    void updateTilesOsd();
    QString fmtStream(qint64 ms) const;
    QString fmtWall(qint64 wallMs) const;
    QVector<CamLane> currentCamLanes() const;   ///< SyncLaneData → 条图块位
    bool lanesLoaded() const;

    MultiCamSyncService *m_svc = nullptr;
    MultiCamSyncService::EngineFactory m_factory;

    // P-59 双页栈：机位勾选面板 ↔ 播放页
    QStackedLayout *m_stack = nullptr;
    QWidget *m_pickerPage = nullptr;
    QWidget *m_playPage = nullptr;
    const CaseManager *m_case = nullptr;     ///< 案件模式（独立模式为空）
    QVector<CamInventoryItem> m_inventory;   ///< 清单快照（勾选行一一对应）
    QVector<QCheckBox *> m_checks;
    QVBoxLayout *m_checkListLay = nullptr;
    QLabel *m_pickHint = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_repickBtn = nullptr;

    // UI 元素
    QGridLayout *m_grid = nullptr;
    QVector<CamTileWidget *> m_tiles;
    QVector<QSlider *> m_bars;             ///< 模式B 每路进度条
    QVector<QLabel *> m_barLabels;         ///< 模式B 每路时间文案
    MultiCamViewWidget *m_mergedBar = nullptr;   ///< 模式A 合并时间线
    QWidget *m_timelineHost = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_rateBtn = nullptr;
    QPushButton *m_osdBtn = nullptr;
    QPushButton *m_alignBtn = nullptr;
    QPushButton *m_alignOkBtn = nullptr;
    QPushButton *m_alignCancelBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_hintLabel = nullptr;

    SyncTimelineMode m_mode = SyncTimelineMode::Merged;
    bool m_osdOn = true;
    bool m_aligning = false;
    int m_tempSlot = -1;                    ///< 模式B 临时进槽位（-1=无）
    qint64 m_savedOffsetBeforeAlign = 0;
    float m_rate = 1.0f;
};
