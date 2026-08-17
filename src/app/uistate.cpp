/**
 * @file uistate.cpp
 * @brief UiState 实现（P-31 T4）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "uistate.h"

UiState::UiState(QObject *parent)
    : QObject(parent)
{
}

void UiState::beginVideo(qint64 trustedMs)
{
    m_trustedMs = trustedMs > 0 ? trustedMs : 0;
    m_engineMs = 0;
    commitMaybeChanged();
}

void UiState::ingestEngineDuration(qint64 engineMs)
{
    if (engineMs <= 0)
        return;   // 与旧版一致：durationChanged(0) 不覆盖有效值
    m_engineMs = engineMs;
    commitMaybeChanged();
}

qint64 UiState::effectiveDurationMs() const
{
    if (m_engineMs > 0) {
        if (m_trustedMs > 0 && m_engineMs > m_trustedMs)
            return m_trustedMs;
        return m_engineMs;
    }
    return m_trustedMs;
}

void UiState::commitMaybeChanged()
{
    static thread_local qint64 lastEmitted = 0;
    const qint64 eff = effectiveDurationMs();
    if (eff != lastEmitted) {
        lastEmitted = eff;
        emit effectiveDurationChanged(eff);
    }
}
