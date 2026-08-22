/**
 * @file segment_switch_engine.h
 * @brief P-69 合并轨分段换文件引擎（IVideoEngine 装饰器：虚拟流内轴 ⇄ 段文件）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-22
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计：MultiCamSyncService 全程只面对虚拟流内轴（各段按墙钟起点序首尾相
 * 接），本装饰器把 seek/position 翻译为 (段号, 段内真实流内毫秒)，跨段时
 * 换文件（一引擎一房，资源有界 C5——N 段不增引擎数）。播放至段尾且下一段
 * 墙钟紧邻（≤2s 容差）时自动顺接换文件；有缺口则由服务覆盖逻辑停路，
 * 复出 seek 自然触发换段。
 */
#pragma once

#include "infrastructure/ivideo_engine.h"
#include "domain/sync_model.h"
#include <functional>

class SegmentSwitchEngine : public IVideoEngine
{
    Q_OBJECT
public:
    using RealFactory = std::function<IVideoEngine *(QObject *parent)>;

    /// factory = 真实引擎工厂（与服务同源）；segs = 合并轨段（按墙钟起点
    /// 升序，装配层已排）。durations 初始取段预读值，各段引擎回报后自修正。
    SegmentSwitchEngine(RealFactory factory, const QVector<SyncSegment> &segs,
                        QObject *parent = nullptr);
    ~SegmentSwitchEngine() override;

    // ---- IVideoEngine（虚拟轴语义）----
    bool load(const QString &filePath) override;   // 参数忽略：内部锚定段 0
    void play() override;
    void pause() override;
    void stop() override;
    void unload() override;
    void seek(qint64 virtMs) override;
    qint64 position() const override;
    qint64 duration() const override;
    PlaybackState state() const override;
    int videoWidth() const override;
    int videoHeight() const override;
    float fps() const override;
    int volume() const override { return m_volume; }
    void setVolume(int vol) override;
    void setRate(float rate) override;
    float rate() const override { return m_rate; }
    bool supportsRateAudio() const override;
    void setScrubMode(bool on) override;
    void setScrubTarget(qint64 virtMs) override;
    void setPreviewLowres(int level) override;
    int previewLowres() const override { return m_lowres; }
    void ackFrame() override;
    QString hardwareAdapterName() const override;
    qint64 learnedGopMs() const override;

    /// 当前段号（瓦片 OSD 「[k/N]」用；未载 -1）
    int currentSegment() const { return m_cur; }

private:
    void switchToSegment(int k, qint64 realMs, bool resumePlay);
    qint64 totalDuration() const;
    QVector<qint64> segCum() const;    ///< 虚拟轴前缀和

    RealFactory m_factory;
    QVector<SyncSegment> m_segs;
    IVideoEngine *m_real = nullptr;
    int m_cur = -1;
    qint64 m_pendingSeek = -1;   ///< 换文件后待落点（段内真实毫秒）
    bool m_pendingPlay = false;  ///< 换文件后是否续播
    bool m_wantPlay = false;
    bool m_scrubbing = false;
    float m_rate = 1.0f;
    int m_volume = 0;
    int m_lowres = 0;
};
