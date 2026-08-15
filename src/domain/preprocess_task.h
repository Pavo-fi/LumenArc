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
    bool    copyAudio = false;    // 原音轨为 AAC 时直拷（探测驱动，2026-08：
                                  // 保留原始音频数据层级，避免重编码丢特征）
    int     audioSampleRate = 0;  // 重编码时保留原采样率（0=48000）
    int     audioChannels = 0;    // 重编码时保留原声道数（0=2）
    float   fps = 0.0f;           // >0 时统一 CFR 输出该帧率（多段帧率不一
                                  // 时拼接前置要求；0=保留源节奏）
    int     keyframeInterval = 0; // 关键帧间隔（帧数）；0=libx264 默认 250
                                  // 现场反馈：默认 GOP 太长（15fps≈17s），
                                  // 拖拽 seek 需从上一关键帧逐帧解码 → 卡死
    // v1.7.0 M1：编码器（空=Auto 探测：NVENC→QSV→libx264）
    QString encoder;
    // v1.7.0 M2：重叠剪切区间（>0 生效；输入侧 -ss/-t，全程重编码流程
    // 下无需流拷贝——方案 §8 局部重编码优化在本流程不适用，记录偏差）
    qint64  trimStartMs = 0;
    qint64  trimEndMs = 0;        // <=0 = 到文件尾
    // v1.7.0 M4：输出分辨率统一（跨相机混拼，>0 时 vf 加 scale）
    int     outWidth = 0;
    int     outHeight = 0;
};

struct ProcessingOptions {
    QString outputDir;            // 空 = 素材目录下 LumenArc_Transcode_<时间戳>/
    int     crf = 18;
    bool    deinterlace = true;
    bool    normalizeTimestamps = false;
    bool    ignoreWarnings = false;
    bool    withSha256 = true;    // 证据报告含源文件哈希（可选开关，§9.2）
    QString encoder;              // v1.7.0 M1：空=libx264；h264_nvenc/h264_qsv
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
