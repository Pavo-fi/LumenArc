/**
 * @file multicam_sync_service.cpp
 * @brief 多机同步播放服务实现（P-57）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-18
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "multicam_sync_service.h"
#include "segment_switch_engine.h"

#include <QTimer>
#include "infrastructure/ivideo_engine.h"

namespace {
/// 纠偏阈值：@20fps 约 2.4 帧（方案 §3.1；长 GOP 由"持续增长才纠"防抖兜住）
constexpr qint64 kDriftThresholdMs = 120;
/// 性能治理②档阈值：软解路总像素吞吐超过该值 → 最重路降 lowres 预览档
///（≈ 2.5 路 1080p30 或 1 路 1440p20 全软解的经验门）
constexpr double kSoftLaneMpPerSec = 150.0 * 1000.0 * 1000.0;
constexpr int kTickMs = 100;
} // namespace

MultiCamSyncService::MultiCamSyncService(QObject *parent)
    : QObject(parent)
{
    m_tick = new QTimer(this);
    m_tick->setInterval(kTickMs);
    connect(m_tick, &QTimer::timeout, this, &MultiCamSyncService::onTick);
}

MultiCamSyncService::~MultiCamSyncService()
{
    closeAll();
}

void MultiCamSyncService::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

// ---------------------------------------------------------------------------
// 装配 / 拆除
// ---------------------------------------------------------------------------
bool MultiCamSyncService::loadLanes(const QVector<SyncLaneData> &lanes)
{
    if (!m_factory || lanes.size() < 1 || lanes.size() > 4)
        return false;
    closeAll();

    m_lanes = lanes;
    m_engines.clear();
    m_lowres.fill(false, m_lanes.size());
    m_laneOk.fill(false, m_lanes.size());
    m_laneLinked.clear();
    for (const auto &l : m_lanes)
        m_laneLinked.append(l.calibrated);   // 校时路恒入墙钟轴；临时路待对齐
    m_loadAccounted.fill(false, m_lanes.size());
    m_prevErr.fill(std::numeric_limits<qint64>::min(), m_lanes.size());
    m_lanePlaying.fill(false, m_lanes.size());
    m_pendingLoads = m_lanes.size();
    setState(State::Loading);

    for (int i = 0; i < m_lanes.size(); ++i) {
        if (m_lanes[i].path.isEmpty()) {
            // 空槽位（模式B 临时进/独立模式未选视频）：不是错误，静默待选
            m_engines.append(nullptr);
            m_loadAccounted[i] = true;
            if (--m_pendingLoads == 0)
                finishLoading();
            continue;
        }
        // P-69 合并轨：分段换文件装饰器（虚拟流内轴；真实引擎由工厂内建，
        // 一路一引擎，N 段不增资源占用——C5）
        IVideoEngine *e = m_lanes[i].isMerged()
            ? static_cast<IVideoEngine *>(
                  new SegmentSwitchEngine(m_factory, m_lanes[i].segments, this))
            : m_factory(this);
        if (!e) {
            m_engines.append(nullptr);
            m_loadAccounted[i] = true;
            emit laneLoadFailed(i, m_lanes[i].path);
            if (--m_pendingLoads == 0)
                finishLoading();
            continue;
        }
        m_engines.append(e);
        const int idx = i;
        connect(e, &IVideoEngine::durationChanged, this,
                [this, idx](qint64 d) { onLaneDuration(idx, d); });
        connect(e, &IVideoEngine::stateChanged, this,
                [this, idx](PlaybackState st) { onLaneState(idx, static_cast<int>(st)); });
        e->setVolume(0);          // 默认全静音，加载收口后 applyAudible
        if (!e->load(m_lanes[i].path)) {
            m_loadAccounted[i] = true;
            emit laneLoadFailed(i, m_lanes[i].path);
            if (--m_pendingLoads == 0)
                finishLoading();
        }
    }
    return true;
}

void MultiCamSyncService::closeAll()
{
    if (m_tick->isActive())
        m_tick->stop();
    for (IVideoEngine *e : m_engines) {
        if (!e)
            continue;
        disconnect(e, nullptr, this, nullptr);
        e->unload();
        e->deleteLater();
    }
    m_engines.clear();
    m_lanes.clear();
    m_lowres.clear();
    m_laneOk.clear();
    m_laneLinked.clear();
    m_loadAccounted.clear();
    m_prevErr.clear();
    m_lanePlaying.clear();
    m_pendingLoads = 0;
    m_scrubbing = false;
    m_rate = 1.0f;
    m_clockWallMs = 0;
    m_contentStart = 0;
    m_contentEnd = 0;
    setState(State::Idle);
}

// ---------------------------------------------------------------------------
// 引擎信号收口
// ---------------------------------------------------------------------------
void MultiCamSyncService::onLaneDuration(int idx, qint64 durMs)
{
    if (idx < 0 || idx >= m_lanes.size())
        return;
    m_lanes[idx].durationMs = durMs;
    if (durMs > 0)
        m_laneOk[idx] = true;
    emit laneInfoChanged(idx);
    // 加载收口以 durationChanged 为准信（m_loadAccounted 防重：lowres 重载
    // 会再报一次）；路数据预设 durationMs 的场景同样在此收口
    if (!m_loadAccounted.value(idx, true)) {
        m_loadAccounted[idx] = true;
        if (--m_pendingLoads == 0)
            finishLoading();
    }
}

/// 全路加载收口：算覆盖区间 → 性能治理 → Ready 定位首帧
void MultiCamSyncService::finishLoading()
{
    recomputeContentRange();
    evaluatePerformance();
    applyAudible();
    rebaseClock(m_contentStart);
    for (int i = 0; i < m_engines.size(); ++i) {
        if (!m_engines[i] || !m_laneOk[i])
            continue;
        if (syncLaneCovers(m_lanes[i], m_clockWallMs))
            m_engines[i]->seek(syncStreamOf(m_lanes[i], m_clockWallMs));
    }
    setState(State::Ready);
    emit loadFinished();
    emit clockChanged(m_clockWallMs);
}

// ---------------------------------------------------------------------------
// 内容区间：统一墙钟轴只含已联动路（校时路恒入，临时路对齐后入）；
// 无联动路（独立模式双临时路未对齐）时兑底全量——各路均以流内轴播放
// ---------------------------------------------------------------------------
void MultiCamSyncService::recomputeContentRange()
{
    bool anyLinked = false;
    for (int i = 0; i < m_lanes.size(); ++i)
        if (m_laneLinked.value(i, false))
            anyLinked = true;
    m_contentStart = std::numeric_limits<qint64>::max();
    m_contentEnd = 0;
    for (int i = 0; i < m_lanes.size(); ++i) {
        if (!m_laneOk.value(i, false))
            continue;   // 失败路不参与覆盖区间
        if (anyLinked && !m_laneLinked.value(i, false))
            continue;   // 未对齐临时路不进墙钟轴（独立播放，对齐后并入）
        m_contentStart = qMin(m_contentStart, syncLaneWallStart(m_lanes[i]));
        m_contentEnd = qMax(m_contentEnd, syncLaneWallEnd(m_lanes[i]));
    }
    if (m_contentStart == std::numeric_limits<qint64>::max()) {
        m_contentStart = 0;
        m_contentEnd = 0;
    }
}

void MultiCamSyncService::onLaneState(int idx, int st)
{
    // 加载失败：引擎 openFile 失败回 Idle（load() 已返回 true 的异步失败面）
    if (m_state == State::Loading
        && static_cast<PlaybackState>(st) == PlaybackState::Idle
        && idx >= 0 && idx < m_lanes.size()
        && !m_loadAccounted.value(idx, true)) {
        m_loadAccounted[idx] = true;
        emit laneLoadFailed(idx, m_lanes[idx].path);
        if (--m_pendingLoads == 0)
            finishLoading();
        return;
    }
    // 运行期引擎暴毙（R-1 对抗面：如 lowres 重载失败/解码器崩溃回 Idle）：
    // 标死该路，不再参与播放/seek；占位由 UI 画（C2 不静默）
    if (static_cast<PlaybackState>(st) == PlaybackState::Idle
        && idx >= 0 && idx < m_lanes.size()
        && m_state != State::Loading && m_state != State::Idle
        && m_laneOk.value(idx, false)) {
        m_laneOk[idx] = false;
        m_lanePlaying[idx] = false;
        emit laneLoadFailed(idx, m_lanes[idx].path);
        emit laneInfoChanged(idx);
    }
}

// ---------------------------------------------------------------------------
// 主时钟
// ---------------------------------------------------------------------------
qint64 MultiCamSyncService::masterWallNow() const
{
    if (m_state == State::Playing)
        return m_clockBaseWall
               + static_cast<qint64>(m_clockTimer.elapsed() * m_rate);
    return m_clockBaseWall;
}

void MultiCamSyncService::rebaseClock(qint64 wallMs)
{
    m_clockBaseWall = wallMs;
    m_clockWallMs = wallMs;
    m_clockTimer.restart();
}

// ---------------------------------------------------------------------------
// 播放控制
// ---------------------------------------------------------------------------
void MultiCamSyncService::play()
{
    if (m_state == State::Ended) {
        seekWall(m_contentStart);   // 播完再按播放 = 从头来（主窗同款语义）
    }
    if (m_state != State::Ready && m_state != State::Paused)
        return;
    rebaseClock(m_clockWallMs);
    for (int i = 0; i < m_engines.size(); ++i) {
        if (!m_engines[i] || !m_laneOk.value(i, false))
            continue;
        if (!m_laneLinked.value(i, false)) {
            // 未对齐临时路：独立自由播（不驻停不定位，墙钟轴未建立）
            m_lanePlaying[i] = true;
            m_engines[i]->setRate(m_rate);
            m_engines[i]->play();
            continue;
        }
        m_lanePlaying[i] = syncLaneCovers(m_lanes[i], m_clockWallMs);
        if (m_lanePlaying[i]) {
            m_engines[i]->setRate(m_rate);
            m_engines[i]->play();
        }
    }
    m_tick->start();
    setState(State::Playing);
    emit clockChanged(m_clockWallMs);
}

void MultiCamSyncService::pause()
{
    if (m_state != State::Playing)
        return;
    rebaseClock(masterWallNow());   // 冻结
    for (IVideoEngine *e : m_engines)
        if (e)
            e->pause();
    m_lanePlaying.fill(false, m_lanePlaying.size());
    setState(State::Paused);
    emit clockChanged(m_clockWallMs);
}

void MultiCamSyncService::togglePlay()
{
    if (m_state == State::Playing)
        pause();
    else
        play();
}

void MultiCamSyncService::setRate(float rate)
{
    if (rate <= 0.0f)
        rate = 1.0f;
    if (qAbs(rate - m_rate) < 0.001f)
        return;
    const bool playing = (m_state == State::Playing);
    if (playing)
        rebaseClock(masterWallNow());   // 变速瞬间锚定当前墙钟
    m_rate = rate;
    for (int i = 0; i < m_engines.size(); ++i)
        if (m_engines[i] && m_lanePlaying.value(i, false))
            m_engines[i]->setRate(rate);
}

void MultiCamSyncService::seekWall(qint64 wallMs)
{
    if (m_state == State::Idle || m_state == State::Loading)
        return;
    wallMs = qBound(m_contentStart, wallMs, qMax(m_contentStart, m_contentEnd));
    rebaseClock(wallMs);
    for (int i = 0; i < m_engines.size(); ++i) {
        if (!m_engines[i] || !m_laneOk.value(i, false))
            continue;
        if (!m_laneLinked.value(i, false))
            continue;   // 未对齐临时路：墙钟 seek 无义，不动（保持自由播放）
        m_prevErr[i] = std::numeric_limits<qint64>::min();
        const bool covers = syncLaneCovers(m_lanes[i], wallMs);
        const qint64 target = qBound<qint64>(0, syncStreamOf(m_lanes[i], wallMs),
                                             qMax<qint64>(0, m_lanes[i].durationMs));
        m_engines[i]->seek(target);
        if (m_state == State::Playing) {
            if (covers && !m_lanePlaying[i]) {
                m_engines[i]->setRate(m_rate);
                m_engines[i]->play();
            } else if (!covers && m_lanePlaying[i]) {
                m_engines[i]->pause();
            }
            m_lanePlaying[i] = covers;
        }
    }
    if (m_state == State::Ended)
        setState(State::Paused);
    emit clockChanged(m_clockWallMs);
}

// ---------------------------------------------------------------------------
// 游标追逐（U-4）
// ---------------------------------------------------------------------------
void MultiCamSyncService::beginScrub()
{
    if (m_scrubbing || (m_state != State::Playing && m_state != State::Paused
                        && m_state != State::Ready && m_state != State::Ended))
        return;
    m_scrubbing = true;
    m_wasPlayingBeforeScrub = (m_state == State::Playing);
    if (m_wasPlayingBeforeScrub)
        pause();
    for (IVideoEngine *e : m_engines)
        if (e)
            e->setScrubMode(true);
}

void MultiCamSyncService::scrubTo(qint64 wallMs)
{
    if (!m_scrubbing)
        return;
    wallMs = qBound(m_contentStart, wallMs, qMax(m_contentStart, m_contentEnd));
    m_clockWallMs = wallMs;
    m_clockBaseWall = wallMs;
    for (int i = 0; i < m_engines.size(); ++i) {
        if (!m_engines[i] || !m_laneLinked.value(i, false))
            continue;   // 未对齐临时路：追逐不跟随（模式B 未对齐拖拽各走各路）
        if (syncLaneCovers(m_lanes[i], wallMs))
            m_engines[i]->setScrubTarget(syncStreamOf(m_lanes[i], wallMs));
    }
    emit clockChanged(m_clockWallMs);
}

void MultiCamSyncService::endScrub()
{
    if (!m_scrubbing)
        return;
    m_scrubbing = false;
    for (IVideoEngine *e : m_engines)
        if (e)
            e->setScrubMode(false);
    seekWall(m_clockWallMs);   // 松手一次性精确 seek（引擎 scrub 语义）
    if (m_wasPlayingBeforeScrub)
        play();
}

// ---------------------------------------------------------------------------
// 音频 / 临时偏移
// ---------------------------------------------------------------------------
void MultiCamSyncService::setAudibleLane(int idx)
{
    if (idx < 0 || idx >= m_engines.size())
        return;
    m_audible = idx;
    applyAudible();
}

void MultiCamSyncService::applyAudible()
{
    for (int i = 0; i < m_engines.size(); ++i)
        if (m_engines[i])
            m_engines[i]->setVolume(i == m_audible ? 100 : 0);
}

bool MultiCamSyncService::applyLaneCalibration(int idx, const TimeCalibration &cal)
{
    if (idx < 0 || idx >= m_lanes.size())
        return false;
    m_lanes[idx].cal = cal;
    m_lanes[idx].calibrated = true;
    m_lanes[idx].temporary = false;      // 间接校时也是真校时：临时路转正
    m_lanes[idx].tempOffsetMs = 0;
    m_laneLinked[idx] = true;
    recomputeContentRange();
    emit laneInfoChanged(idx);
    return true;
}

void MultiCamSyncService::setLaneOffsetMs(int idx, qint64 offsetMs)
{
    if (idx < 0 || idx >= m_lanes.size() || !m_lanes[idx].temporary)
        return;
    m_lanes[idx].tempOffsetMs = offsetMs;
    m_laneLinked[idx] = true;   // 对齐即入墙钟轴
    // 覆盖区间随偏移移动：重算内容总区间
    recomputeContentRange();
    emit laneInfoChanged(idx);
    // 对齐完成即把时钟对齐到该路当前画面，避免跳变
    if (m_engines.value(idx))
        seekWall(syncWallOf(m_lanes[idx], m_engines[idx]->position()));
}

void MultiCamSyncService::alignTempLane(int tempIdx, int refIdx)
{
    if (tempIdx < 0 || tempIdx >= m_lanes.size()
        || refIdx < 0 || refIdx >= m_lanes.size() || tempIdx == refIdx)
        return;
    if (!m_lanes[tempIdx].temporary)
        return;
    auto *et = m_engines.value(tempIdx);
    auto *er = m_engines.value(refIdx);
    if (!et || !er)
        return;
    // 参考路当前位置的墙钟 = 临时路当前位置应对齐到的墙钟
    const qint64 offset = syncWallOf(m_lanes[refIdx], er->position()) - et->position();
    m_laneLinked[refIdx] = true;   // 独立模式双临时路：参考路锚定为联动基准
    setLaneOffsetMs(tempIdx, offset);
}

// ---------------------------------------------------------------------------
// 节拍：主时钟推进 + 缺口管理 + 纠偏 + 结束检测
// ---------------------------------------------------------------------------
void MultiCamSyncService::onTick()
{
    if (m_state != State::Playing)
        return;
    const qint64 wall = masterWallNow();
    m_clockWallMs = wall;
    for (int i = 0; i < m_engines.size(); ++i)
        applyCoverageAndDrift(i, wall);
    emit clockChanged(wall);
    if (wall >= m_contentEnd && m_contentEnd > m_contentStart) {
        pause();
        setState(State::Ended);
    }
}

void MultiCamSyncService::applyCoverageAndDrift(int idx, qint64 wallMs)
{
    IVideoEngine *e = m_engines.value(idx);
    if (!e || m_scrubbing || !m_laneOk.value(idx, false))
        return;
    if (!m_laneLinked.value(idx, false))
        return;   // 未对齐临时路：恒覆盖、不纠偏（自由播放，对齐后纳入）
    const bool covers = syncLaneCovers(m_lanes[idx], wallMs);
    if (!covers) {
        if (m_lanePlaying[idx]) {
            e->pause();              // 进入缺口：停该路（瓦片由 UI 画占位）
            m_lanePlaying[idx] = false;
        }
        return;
    }
    const qint64 target = syncStreamOf(m_lanes[idx], wallMs);
    if (!m_lanePlaying[idx]) {
        // 缺口复出：seek 到当前墙钟对应帧再续播
        e->seek(target);
        e->setRate(m_rate);
        e->play();
        m_lanePlaying[idx] = true;
        m_prevErr[idx] = std::numeric_limits<qint64>::min();
        return;
    }
    // 纠偏（R-2：长 GOP 路 seek 代价高，阈值放宽 500ms；靠“持续增长才纠”
    // 防抖兜底——短 GOP 路仍用 120ms 严阈值）
    const qint64 gopMs = e->learnedGopMs();
    const qint64 threshold = (gopMs > 4000) ? 500 : kDriftThresholdMs;
    qint64 seekTo = 0;
    if (decideDriftCorrection(e->position(), target, m_prevErr[idx],
                              threshold, &seekTo)) {
        e->seek(seekTo);
        m_prevErr[idx] = std::numeric_limits<qint64>::min();
    } else {
        m_prevErr[idx] = e->position() - target;
    }
}

// ---------------------------------------------------------------------------
// §4 性能三级治理（load 收口一次性判定；运行期不频繁切档防抖）
// ---------------------------------------------------------------------------
void MultiCamSyncService::evaluatePerformance()
{
    double softMpPerSec = 0.0;
    int heavyIdx = -1;
    double heavyMp = 0.0;
    for (int i = 0; i < m_engines.size(); ++i) {
        IVideoEngine *e = m_engines[i];
        if (!e || !m_laneOk.value(i, false))
            continue;
        if (!e->hardwareAdapterName().isEmpty())
            continue;   // 档①：硬解路不计入软解负载
        const double mp = double(e->videoWidth()) * e->videoHeight()
                          * qMax(1.0f, e->fps());
        softMpPerSec += mp;
        if (mp > heavyMp) {
            heavyMp = mp;
            heavyIdx = i;
        }
    }
    if (softMpPerSec > kSoftLaneMpPerSec && heavyIdx >= 0) {
        // 档②：最重的一路降 lowres 预览档并重载（仅预览降清，见方案 §4）
        m_lowres[heavyIdx] = true;
        m_engines[heavyIdx]->setPreviewLowres(1);
        m_engines[heavyIdx]->load(m_lanes[heavyIdx].path);
        emit performanceNotice(
            tr("多路高负载：%1 已切换预览降清档（不影响原始数据）")
                .arg(m_lanes[heavyIdx].displayName));
        emit laneInfoChanged(heavyIdx);
    }
    if (softMpPerSec > kSoftLaneMpPerSec * 2.0)
        emit performanceNotice(
            tr("多路高负载，建议减少路数或降低倍速（不强制）"));
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------
IVideoEngine *MultiCamSyncService::engineAt(int idx) const
{
    return m_engines.value(idx, nullptr);
}

qint64 MultiCamSyncService::contentStartWallMs() const { return m_contentStart; }
qint64 MultiCamSyncService::contentEndWallMs() const { return m_contentEnd; }

bool MultiCamSyncService::laneCoversNow(int idx) const
{
    if (idx < 0 || idx >= m_lanes.size())
        return false;
    if (!m_laneLinked.value(idx, false))
        return true;   // 未对齐临时路：无墙钟轴语义，恒示有信号（独立播放）
    return syncLaneCovers(m_lanes[idx], m_clockWallMs);
}

bool MultiCamSyncService::laneIsLowres(int idx) const
{
    return m_lowres.value(idx, false);
}

bool MultiCamSyncService::laneUsable(int idx) const
{
    return m_laneOk.value(idx, false);
}

bool MultiCamSyncService::laneLinked(int idx) const
{
    return m_laneLinked.value(idx, false);
}
