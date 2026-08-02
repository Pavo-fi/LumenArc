/**
 * @file tick_utils.h
 * @brief 坐标轴刻度步长工具（纯函数，可 headless 单测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 像素密度自适应：长视频（小时级）自动放大步长，避免 X 轴标签糊成一团
 * （现场反馈）。步长从 nice 候选序列中取首个满足“标签间距 ≥ minLabelPx”
 * 的档位；序列覆盖 0.5s ~ 24h。
 */
#pragma once

#include <QtGlobal>

/// 计算时间轴主刻度步长（毫秒）。durationMs>0 且 widthPx>0 时按像素密度选档；
/// 退化输入返回 60000（1 分钟）兜底。
inline qint64 computeXAxisStepMs(qint64 durationMs, int widthPx, int minLabelPx = 72)
{
    if (durationMs <= 0 || widthPx <= 0)
        return 60000;

    static const qint64 kSteps[] = {
        500,        // 0.5s
        1000,       // 1s
        2000,       // 2s
        5000,       // 5s
        10000,      // 10s
        15000,      // 15s
        30000,      // 30s
        60000,      // 1min
        120000,     // 2min
        300000,     // 5min
        600000,     // 10min
        900000,     // 15min
        1800000,    // 30min
        3600000,    // 1h
        7200000,    // 2h
        21600000,   // 6h
        86400000,   // 24h
    };
    const qreal pxPerMs = widthPx / static_cast<qreal>(durationMs);
    qint64 step = kSteps[0];
    for (qint64 s : kSteps) {
        if (static_cast<qreal>(s) * pxPerMs >= minLabelPx) {
            step = s;
            break;
        }
        step = s;
    }
    return step;
}
