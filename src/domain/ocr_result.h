/**
 * @file ocr_result.h
 * @brief 前处理-OSD 时间戳 OCR 结果（纯数据模型，无 Qt Widgets 依赖）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 契约见 docs/PREPROCESSING_TECH_DESIGN_CN.md §6.1。
 * 取证原则：rawStartText/rawEndText 逐字保留 OCR 原始观测值；
 * wallStartMs/wallEndMs 为派生值（脚本已换算为本地墙钟毫秒）。
 */
#pragma once

#include <QString>
#include <QRectF>

struct OcrResult {
    enum Source { Ocr, Manual, None };

    QString filePath;
    qint64  wallStartMs = 0;    // 首帧墙钟（流内 rel 0 的推算值，派生）
    qint64  wallEndMs = 0;      // 尾帧墙钟（派生；0=未知）
    QString rawStartText, rawEndText;   // 原始 OCR 文本（逐字保留）
    double  conf = 0.0;
    Source  source = None;
    QString firstFrameImg, lastFrameImg;    // 证据截图（绝对路径或空）
    QString startCropImg, endCropImg;
    qint64  startFrameRelMs = 0, endFrameRelMs = 0; // 证据帧真实流内位置（实测）
    qint64  durationMs = 0;     // 脚本侧使用的时长（可信时长或 ffprobe 兜底）
    QString sha256;
    QString ocrError;           // 空=成功；非空进入人工兜底（规范 C2）
    QRectF  hitRoi;             // P-60 命中行归一化位置（ROI 自学习；无效=未回报）
};
