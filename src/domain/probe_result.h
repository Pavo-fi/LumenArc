/**
 * @file probe_result.h
 * @brief 前处理-视频探测结果（纯数据模型，无 Qt Widgets 依赖）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 契约见 docs/PREPROCESSING_TECH_DESIGN_CN.md §6.1。
 * 时间值一律 qint64 毫秒（规范 C6）；原始观测值保留原文（取证原则）。
 */
#pragma once

#include <QString>

struct ProbeResult {
    QString filePath;
    QString container;        // mp4 / mpegts / matroska ...
    QString videoCodec;       // h264 / hevc / mpeg4 ...
    int     profile = 0;      // FF_PROFILE_*
    int     level = 0;
    int     width = 0, height = 0;
    double  fps = 0.0;        // avg_frame_rate 换算
    bool    fpsDubious = false;   // avg 与 r_frame_rate 双源偏差 > 1‰
    int     keyframeIntervalMs = 0;  // 实测中位关键帧间隔（<2 个关键帧=0）
    bool    keyframeSparse = false;  // 关键帧间隔 > 2.5s（拖拽需从上一关键帧解码）
    QString pixFmt;           // yuv420p / yuvj420p ...
    QString colorRange, colorSpace;
    int     fieldOrder = 0;   // AVFieldOrder
    int     rotation = 0;     // display matrix 角度
    qint64  startTimeMs = 0;        // 流起始（相对0基准，负值合法）
    qint64  firstFramePtsMs = 0;    // 首视频包 PTS（相对毫秒）
    bool    firstPktKeyframe = false;
    qint64  durationMs = 0;         // 容器时长
    bool    durationDubious = false; // 截断/虚报存疑
    bool    indexed = true;         // 可 seek 且有索引（TS/PS 为 false）
    int     audioStreams = 0;
    QString audioCodec, audioSampleRate, audioChannels;
    QString creationTimeRaw;        // 原始字符串（取证留档，逐字保留）
    qint64  creationTimeMs = 0;     // 解析值（脏值=0）
    /// 流内绝对起始墙钟（Dahua DHAV 等私有容器由录像机固件写入的录制时刻，
    /// epoch ms UTC；0=无）。仅接受 2000-01-01 ~ 当前+1天 区间的合理值，
    /// 防止普通容器损坏文件的垃圾 PTS 被误判为纪元时间。
    qint64  absStartEpochMs = 0;
    qint64  fileMtimeMs = 0;        // 文件 mtime（证据④兜底）
    QString probeError;             // 空=成功

    bool ok() const { return probeError.isEmpty(); }
};
