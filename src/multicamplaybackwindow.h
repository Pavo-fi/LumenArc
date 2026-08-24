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
#include "domain/speed_plan.h"
#include "domain/event_calib.h"
#include <QVector>
#include <QString>
#include <functional>
#include "domain/sync_model.h"
#include "displayadjust.h"        // DisplayAdjust（瓦片画面调节）
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
    /// 案件数据被本窗口改写（如同事件对时保存校时）→ 主窗口刷新案件树徽标；
    /// 参数=被保存校时的视频路径（主视口若正开着它需同步内存校时，
    /// 防旧值回写覆盖 .vla——v1.15.3 用户实测"退出多机后主页面没更新"）
    std::function<void(const QString &videoPath)> onCaseDataChanged;

    /// 为槽位装载视频（onPickVideo 的对话框后段；测试通道：绕过文件框直喂路径）
    void pickVideoForSlot(int slot, const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;   // P-68：A/B 选段

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
    void onExportClip();      ///< P-68 多机选段导出（模式A）
    void startClipExport(const speedplan::SpeedPlan &plan, bool burnOsd,
                         const QString &outPath);

    // ---- P-73 多机同事件间接校时 ----
    void onEventCalibToggled(bool on);
    void buildEventCalibPanel();      ///< 懒建右列对时面板
    void updateEcGuidance();          ///< 引导状态机（横幅+分段解锁）
    void refreshEcPanel();            ///< 下拉/锚点表刷新
    void updateEcSaveBtn();           ///< 保存钮样式随预览态（常可点，v1.15.3）
    void onEcAddAnchor();
    void onEcRemoveAnchor();
    void onEcFitPreview();
    void onEcSave();
    void onEcExit();
    void ecFrameStep(bool refLane, int dir);  ///< 帧步进（dir=±1）
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
    void applyAdjustToTiles();   ///< 画面调节按存储参数下发各瓦片（v1.12.8 逐瓦片）
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
    QVector<CamMergedGroup> m_mergedGroups;  ///< P-69 合并轨分组（与 m_groupChecks 对齐）
    QVector<class QCheckBox *> m_groupChecks;///< 组合并轨勾选框
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
    QPushButton *m_exportClipBtn = nullptr;   ///< P-68 导出选段（AB 存在时可用）
    qint64 m_lastWallMs = -1;                 ///< 最近墙钟游标（A/B 打点用）
    qint64 m_abA = -1, m_abB = -1;            ///< 选段（墙钟域）
    class SegmentExportEngine *m_segmentExporter = nullptr;
    class SegmentExportDialog *m_exportDlg = nullptr;   // 非模态导出面板

    // ---- P-73 对时模式状态 ----
    class QPushButton *m_eventCalibBtn = nullptr;
    bool m_eventCalib = false;
    QWidget *m_ecHost = nullptr;          ///< 右列宿主（默认隐藏，宽 300）
    QWidget *m_ecPanel = nullptr;
    class QComboBox *m_ecRefCombo = nullptr;
    class QComboBox *m_ecTargetCombo = nullptr;
    class QLineEdit *m_ecEventName = nullptr;
    class QListWidget *m_ecAnchorList = nullptr;
    class QLabel *m_ecStatus = nullptr;
    class QLabel *m_ecBanner = nullptr;    ///< 「现在该干嘛」横幅
    class QLabel *m_ecHint2 = nullptr;     ///< 第 2 标记自愿提示
    QWidget *m_ecStep3 = nullptr;          ///< ③ 打标记段（选路后解锁）
    QWidget *m_ecStep4 = nullptr;          ///< ④ 标记表+预览保存段（1 标记解锁）
    class QPushButton *m_ecAddBtn = nullptr;
    class QPushButton *m_ecFitBtn = nullptr;
    class QCheckBox *m_ecBothAudio = nullptr;  ///< 「同听两路」开关
    bool m_ecPlaying = false;                  ///< 对时沙盒播放态（不经服务状态机）
    class QTimer *m_ecTick = nullptr;          ///< 沙盒播放时驱动条/OSD 跟随
    void ecTogglePlay();                       ///< 沙盒自由播放/暂停
    void updateBarsFromEngines();              ///< 条值=引擎真实位置（onClock/沙盒共用）
    class QPushButton *m_ecSaveBtn = nullptr;
    QVector<eventcalib::EventAnchor> m_ecAnchors;  ///< 目标路锚点（既有+会话）
    eventcalib::FitResult m_ecFit;                 ///< 最近预览拟合
    TimeCalibration m_ecCal;                       ///< 预览生成的校时（保存用）
    bool m_ecPreviewed = false;
    bool m_ecSaved = false;                        ///< 已保存（退出不提示会话级语义）
    int m_ecTargetIdx = -1;                        ///< 当前目标路（切路重载锚点表）
    QPushButton *m_osdBtn = nullptr;
    QPushButton *m_adjustBtn = nullptr;           ///< 画面调节（选中瓦片，v1.12.8）
    class PlaybackAdjustPanel *m_adjustPanel = nullptr;
    QVector<DisplayAdjust> m_tileAdjusts;         ///< 逐瓦片独立调节参数
    QVector<int> m_tileRotations;
    int m_selectedTile = -1;                      ///< 画面调节作用对象（点击选中）
    void selectTile(int idx);
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
