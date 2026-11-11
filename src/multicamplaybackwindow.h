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
    /// v1.16.0：多机起播前回调（主窗挂此暂停主视口——开窗时的一次暂停挡不住
    /// 用户回主窗再播，起播前拦才可靠）
    std::function<void()> onAboutToPlay;
    /// v3.1：后建控件（每段倍速下拉）补挂快捷键守卫（主窗注入 installKeyGuardOn）
    std::function<void(QWidget *)> onInstallKeyGuard;
    /// v3.1：导出结束回调（主窗据此弹顶部浮条，做到「一定有提示」）
    std::function<void(bool ok, const QString &msg)> onExportFinished;

    /// P-54b：播放降噪设置透传到全部路引擎（主窗设置联动）
    void applyPlaybackDenoise(bool on, double strength);

    /// 为槽位装载视频（onPickVideo 的对话框后段；测试通道：绕过文件框直喂路径）
    void pickVideoForSlot(int slot, const QString &path);

    /// 测试通道：直接装载已校时机位（进入模式A 合并时间轴），
    /// 供自检在无案件/无真实视频时验证布局与时间轴行为。
    void loadCalibratedLanesForTest(const QVector<SyncLaneData> &lanes);

    /// 当前宫格顺序（lane → 格子；自检与导出对齐用同一份数据）
    QVector<int> cellOrder() const { return m_cellOfLane; }

    // ---- v2.9 选段/断点（测试通道：自检驱动，与界面按钮同一套逻辑）----
    void testSetCursor(qint64 wallMs) { m_lastWallMs = wallMs; }
    void testMarkAB(bool isA) { markAB(isA); }
    void testAddSplit() { addSplitAtCursor(); }
    void testApplyPlan(const speedplan::SpeedPlan &p) { applyPlanFromDialog(p); }
    QVector<qint64> splitPoints() const { return m_splitWall; }
    QVector<double> splitRates() const { return m_splitRates; }
    void setSplitRate(int seg, double rate);

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
    void onExportClip();      ///< v2.7 宫格视频导出（默认全区间；A/B 可选裁剪）
    void updateExportAvailability();   ///< 导出按钮可用性（全路已入墙钟轴）
    // ---- v2.9 选段 + 变速断点 ----
public slots:
    /// v3.1：给 QMetaObject::invokeMethod 用的快捷键入口（A / B）
    void markRangeA() { markAB(true); }
    void markRangeB() { markAB(false); }

private slots:
    void markAB(bool isA);              ///< 设 A/B（两点齐了自动放大到该区间）
    void addSplitAtCursor();            ///< 在选段内当前时刻加断点
    void rebuildSegmentSpeedBar();      ///< 选段内每段倍速条
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
    void rebuildTiles();    void rebuildTimelineArea();
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
    QPushButton *m_gridOrderBtn = nullptr;    ///< v2.7 宫格顺序（显式入口）
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
    class QPushButton *m_ecPlaySelBtn = nullptr;  ///< v1.16.0：▶ 播放选中瓦片
    class QPushButton *m_ecPlayAllBtn = nullptr;  ///< v1.16.0：▶▶ 播放全部
    bool m_ecPlaying = false;                  ///< 对时沙盒播放态（不经服务状态机）
    int m_ecPlayLane = -1;                     ///< 沙盒播放范围：-1 全部 / >=0 仅该路
    class QTimer *m_ecTick = nullptr;          ///< 沙盒播放时驱动条/OSD 跟随
    void ecTogglePlay();                       ///< 沙盒自由播放/暂停
    /// v1.16.0 拍板：沙盒播放范围收口——lane=-1 全部 / >=0 仅该路；on=false 全停。
    /// 面板双播放钮（选中瓦片/全部）与工具栏播放钮共用此入口。
    void ecSetPlayRange(int lane, bool on);
    void updateBarsFromEngines();              ///< 条值=引擎真实位置（onClock/沙盒共用）
    class QPushButton *m_ecSaveBtn = nullptr;
    QVector<eventcalib::EventAnchor> m_ecAnchors;  ///< 目标路锚点（既有+会话）
    eventcalib::FitResult m_ecFit;                 ///< 最近预览拟合
    TimeCalibration m_ecCal;                       ///< 预览生成的校时（保存用）
    bool m_ecPreviewed = false;
    QString m_ecCorrText;                          ///< 修正量人话（v1.15.3：与旧校时的真实钟差）
    bool m_ecSaved = false;                        ///< 已保存（退出不提示会话级语义）
    int m_ecTargetIdx = -1;                        ///< 当前目标路（切路重载锚点表）
    QPushButton *m_osdBtn = nullptr;
    QPushButton *m_adjustBtn = nullptr;           ///< 画面调节（选中瓦片，v1.12.8）
    class PlaybackAdjustPanel *m_adjustPanel = nullptr;
    QVector<DisplayAdjust> m_tileAdjusts;         ///< 逐瓦片独立调节参数
    QVector<int> m_tileRotations;
    int m_selectedTile = -1;                      ///< 画面调节作用对象（点击选中）
    void selectTile(int idx);

    // ── v2.0 窗位自由安排：lane 序号 → 格子序号（默认恒等）──
    QVector<int> m_cellOfLane;
    int  cellOfLane(int lane) const;
    void moveLaneToCell(int lane, int cell);
    CamTileWidget *tileAtGlobal(const QPoint &globalPos) const;   ///< 拖拽落点命中
    int m_dragFromLane = -1;   ///< v2.8 拖拽换位源路（-1=无）

    // ---- v2.9 选段 + 变速断点（定义在选段之内，改 A/B 即清零）----
    QPushButton *m_abABtn = nullptr;    ///< 工具栏 A
    QPushButton *m_abBBtn = nullptr;    ///< 工具栏 B
    QPushButton *m_splitBtn = nullptr;  ///< 工具栏「断点」
    QWidget *m_segSpeedBar = nullptr;   ///< 每段倍速条
    class QHBoxLayout *m_segSpeedLay = nullptr;
    QVector<qint64> m_splitWall;        ///< 断点（墙钟，升序）
    QVector<double> m_splitRates;       ///< 每段倍率（长度 = 断点数+1）
    bool m_syncingPlan = false;         ///< 播放页 ↔ 导出面板同步时防回环
    /// 由当前播放页状态构造导出方案（墙钟域）
    speedplan::SpeedPlan planFromWindow() const;
    /// 导出面板改了选段/断点/倍率 → 反向同步到时间轴
    void applyPlanFromDialog(const speedplan::SpeedPlan &plan);
    /// 播放页改了 → 推给已打开的导出面板
    void pushPlanToDialog();
    void resetCellOrder();
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
