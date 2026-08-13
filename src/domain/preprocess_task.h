/**
 * @file preprocess_task.h
 * @brief 前处理-任务/阶段/错误类型（纯数据，无 Qt Widgets 依赖）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 契约见 docs/PREPROCESSING_TECH_DESIGN_CN.md §6.1 / §10.1。
 * 错误用类型表达（规范 C1），禁止比较错误文案。
 */
#pragma once

#include <QString>
#include <QStringList>

struct ConcatRequest {
    QStringList orderedFiles;
    QString outputPath;
    QString workDir;              // list.txt 落地目录（Coordinator 持有，证据留档）
    qint64  totalDurationMs = 0;  // 进度换算分母
    bool    normalizeTimestamps = false;  // 时间戳归一化选项（§5.4.3）
    bool    ignoreWarnings = false;
    QVector<qint64> segmentOffsetsMs;     // 归一化累计偏移（normalize 时必填）
};

struct TranscodeRequest {
    QString input;
    QString output;
    qint64  durationMs = 0;       // 进度换算分母
    int     crf = 18;
    bool    deinterlace = true;   // 隔行源默认 yadif（可配置）
    float   fps = 0.0f;           // >0 时统一 CFR 输出该帧率（多段帧率不一
                                  // 时拼接前置要求；0=保留源节奏）
    int     keyframeInterval = 0; // 关键帧间隔（帧数）；0=libx264 默认 250
                                  // 现场反馈：默认 GOP 太长（15fps≈17s），
                                  // 拖拽 seek 需从上一关键帧逐帧解码 → 卡死
};

struct ProcessingOptions {
    QString outputDir;            // 空 = 素材目录下 LumenArc_Transcode_<时间戳>/
    int     crf = 18;
    bool    deinterlace = true;
    bool    normalizeTimestamps = false;
    bool    ignoreWarnings = false;
    bool    withSha256 = true;    // 证据报告含源文件哈希（可选开关，§9.2）
};

enum class TaskPhase {
    Idle, Probing, Ocr, Sorting, UserConfirm,
    Precheck, Transcoding, Concat, Done, Failed, Cancelled
};

/// 类型化错误码（规范 C1）
enum class PreprocessError {
    None,
    FileUnreadable,
    ProbeFailed,
    OcrEngineMissing,
    OcrAllFailed,
    SortSuspicious,
    PrecheckBlock,
    TranscodeFailed,
    ConcatFailed,
    OutputConflict,
    Cancelled,
    Timeout
};

struct PreprocessReport {
    QString outputPath;           // 拼接输出（或转码目录）
    QStringList outputPaths;      // 全部输出（多拼接组/逐文件转码，v1.3.0 案件登记用）
    QString evidenceDir;          // 证据目录（§9.1）
    QString reportCsvPath;
    QString reportHtmlPath;
    QString errorDetail;          // 失败时的补充说明（类型之外的可读信息）
    PreprocessError error = PreprocessError::None;
};
