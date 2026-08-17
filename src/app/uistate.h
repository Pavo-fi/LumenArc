/**
 * @file uistate.h
 * @brief UI 派生状态唯一数据源（P-31 T4，收口 P-37/R5：视频时长五副本 → 一源两派生）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 语义（与旧版 MainWindow 三点校准逐条一致，行为冻结）：
 *  - beginVideo(trusted)：打开视频，记分析引擎可信时长，引擎观测清零；
 *  - ingestEngineDuration(ms)：durationChanged 观测入账；
 *  - 有效时长 = 引擎观测>0 ?（可信>0 且 观测>可信 ? 可信 : 观测）: 可信
 *    （VLC/DVR 拼接容器可能虚高，可信值为上限钳制——onDurationChanged 旧规则）。
 * 消费方（图表轴/列表时长/时间标签）只收 effectiveDurationChanged 推送或查询，
 * 不再各持副本（R5 SSOT）。
 */
#pragma once

#include <QObject>

class UiState : public QObject
{
    Q_OBJECT

public:
    explicit UiState(QObject *parent = nullptr);

    /// 打开视频：注入可信时长（分析引擎 videoTiming），引擎观测复位
    void beginVideo(qint64 trustedMs);
    /// 引擎 durationChanged 观测入账（<=0 忽略，与旧版隐式行为一致）
    void ingestEngineDuration(qint64 engineMs);

    qint64 effectiveDurationMs() const;
    qint64 trustedDurationMs() const { return m_trustedMs; }
    qint64 engineDurationMs() const { return m_engineMs; }

signals:
    /// 有效时长变化（值不变不发；列表/图表统一刷新入口）
    void effectiveDurationChanged(qint64 ms);

private:
    void commitMaybeChanged();

    qint64 m_trustedMs = 0;
    qint64 m_engineMs = 0;
};
