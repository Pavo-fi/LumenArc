/**
 * @file multicam_sync_service.h
 * @brief 多机同步播放服务（app 层，P-57）——状态机 + 墙钟主时钟 + 纠偏环
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-18
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/MULTICAM_PLAYBACK_TECH_DESIGN_CN.md（v0.3 已拍板）。
 * 红线：R4 引擎中立——只经 IVideoEngine 接口，具体引擎由工厂注入；
 * R5 校时只读消费（.vla SSOT）；R7 显式状态机；C4 纠偏环有迟滞防抖。
 */
#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QVector>
#include <functional>
#include "domain/sync_model.h"

class IVideoEngine;
class QTimer;

class MultiCamSyncService : public QObject
{
    Q_OBJECT
public:
    /// R7 显式状态机
    enum class State { Idle, Loading, Ready, Playing, Paused, Ended };
    Q_ENUM(State)

    /// 引擎工厂（R4：服务不识具体引擎；MainWindow 注入，测试注入假引擎）
    using EngineFactory = std::function<IVideoEngine *(QObject *parent)>;

    explicit MultiCamSyncService(QObject *parent = nullptr);
    ~MultiCamSyncService() override;

    void setEngineFactory(EngineFactory f) { m_factory = std::move(f); }

    /// 装配 2-4 路并启动加载（逐路建引擎+load）。已有路时先 closeAll。
    /// 返回 false = 路数非法/无工厂（不动既有状态）。
    bool loadLanes(const QVector<SyncLaneData> &lanes);
    /// 停全部引擎并释放（unload + deleteLater），回 Idle。
    void closeAll();

    // ---- 播放控制（全路联动）----
    void play();
    void pause();
    void togglePlay();
    void setRate(float rate);
    float rate() const { return m_rate; }
    /// 跳转统一墙钟轴（模式A/B 均经此入口；不在覆盖的路自动缺口处理）
    void seekWall(qint64 wallMs);

    // ---- 游标实时追逐（U-4：合并时间线/进度条拖动）----
    void beginScrub();
    void scrubTo(qint64 wallMs);
    void endScrub();

    // ---- 音频（U-2：单路主听音，随时切听）----
    void setAudibleLane(int idx);
    /// P-73 对时沙盒：自定义可听集合（空集=回到单可听路 legacy 语义）。
    /// 同事件对时常需「同听两路」比对声音事件（喇叭/轰鸣），单可听路不够。
    void setCustomAudible(const QSet<int> &lanes);
    void clearCustomAudible();
    int audibleLane() const { return m_audible; }

    // ---- 临时对齐（模式B，会话级不落盘）----
    void setLaneOffsetMs(int idx, qint64 offsetMs);
    /// P-73 同事件校时成果落路：换 cal、转正（calibrated=true/temporary 摘帽）、
    /// 临时偏移清零、重算区间并广播。返回 false = 越界。
    bool applyLaneCalibration(int idx, const TimeCalibration &cal);
    /// 模式B 对齐会话收口：以 refIdx 路当前位置墙钟为锚建立 tempIdx 偏移，
    /// 两路转入联动（ref 路同为未联动临时路时一并锚定为基准轴）。
    /// 对齐前：临时路独立播放——不驻停/不纠偏/不随墙钟 seek（墙钟轴无义）。
    void alignTempLane(int tempIdx, int refIdx);

    // ---- 状态查询 ----
    State state() const { return m_state; }
    const QVector<SyncLaneData> &lanes() const { return m_lanes; }
    IVideoEngine *engineAt(int idx) const;
    int laneCount() const { return m_lanes.size(); }
    qint64 clockWallMs() const { return m_clockWallMs; }
    qint64 contentStartWallMs() const;
    qint64 contentEndWallMs() const;
    bool laneCoversNow(int idx) const;
    bool laneIsLowres(int idx) const;   ///< 预览降清档角标（性能治理 §4）
    bool laneUsable(int idx) const;     ///< 加载成功且未暴毙（占位判定）
    bool laneLinked(int idx) const;     ///< 已入统一墙钟轴（校时路恒 true；临时路对齐后 true）

signals:
    void stateChanged(MultiCamSyncService::State s);
    void clockChanged(qint64 wallMs);       ///< 100ms 节拍（UI 刷新游标/进度条）
    void laneInfoChanged(int idx);          ///< duration/降清档等到手（重排时间线/OSD）
    void laneLoadFailed(int idx, const QString &path);
    void loadFinished();                    ///< 全部路加载收口（成败均发）
    void performanceNotice(const QString &msg);   ///< 治理档③提示（C2 不静默）

private:
    void setState(State s);
    void onTick();
    void onLaneDuration(int idx, qint64 durMs);
    void onLaneState(int idx, int st);
    void finishLoading();
    void recomputeContentRange();       ///< 内容区间：仅含已联动路（无联动路时全量兑底）
    qint64 masterWallNow() const;
    void rebaseClock(qint64 wallMs);
    void applyCoverageAndDrift(int idx, qint64 wallMs);
    void applyAudible();
    void evaluatePerformance();             ///< §4 三级治理（load 收口后一次性判定）

    QVector<SyncLaneData> m_lanes;
    QVector<IVideoEngine *> m_engines;
    QVector<bool> m_lowres;
    QVector<bool> m_laneOk;                 ///< 加载成功（失败路不参与播放/seek）
    QVector<bool> m_laneLinked;             ///< 已入墙钟轴（校时路恒入；临时路对齐后入）
    QVector<bool> m_loadAccounted;          ///< 加载收口只计一次（lowres 重载防抖）
    QVector<qint64> m_prevErr;              ///< 纠偏迟滞：上次偏差
    QVector<bool> m_lanePlaying;            ///< 该路当前是否应在播（覆盖内）

    EngineFactory m_factory;
    QTimer *m_tick = nullptr;

    State m_state = State::Idle;
    float m_rate = 1.0f;
    int m_audible = 0;
    QSet<int> m_customAudible;   ///< 非空时优先于 m_audible（P-73）
    bool m_scrubbing = false;
    bool m_wasPlayingBeforeScrub = false;

    qint64 m_clockWallMs = 0;    ///< 主时钟（最近 tick/seek 值）
    qint64 m_clockBaseWall = 0;  ///< 播放锚点墙钟
    QElapsedTimer m_clockTimer;  ///< 播放锚点单调时钟
    int m_pendingLoads = 0;      ///< Loading 态待收口计数
    qint64 m_contentStart = 0;   ///< 全部 duration 就绪后计算
    qint64 m_contentEnd = 0;
};
