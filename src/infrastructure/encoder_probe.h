/**
 * @file encoder_probe.h
 * @brief 编码器探测与选择（v1.7.0 M1：NVENC→QSV→libx264 逐级回退）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-15
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计（DEVELOPMENT_PLAN_V1.7 Q1）：
 *  - 运行时 `-encoders` 嗅探 + 试编码 1 帧验证（防驱动 API 版本不可用——
 *    历史实发 HANDOVER 6.6.5：NVENC 探测存在但编码失败）
 *  - 选择链：NVENC → QSV → libx264；失败逐级回退，结果缓存 + 日志
 *  - 等价性评审（M1-4）：NVENC CQ / QSV global_quality ↔ CRF18 映射实测
 */
#pragma once

#include <QString>
#include <QStringList>

namespace encoder_probe {

/// 编码器标识（TranscodeRequest.encoder 语义值）
struct EncoderInfo {
    enum Kind { Libx264, Nvenc, Qsv, None };
    Kind kind = None;
    QString name;        // ffmpeg -c:v 参数值（libx264 / h264_nvenc / h264_qsv）
    QString display;     // 用户可读名
};

/// 探测全部可用 H.264 编码器（-encoders 嗅探 + 试编码验证），结果静态缓存。
QStringList availableEncoders();

/// 试编码 1 帧验证编码器真实可用（lavfi testsrc 1 帧 → null muxer）。
bool probeEncoderWorks(const QString &encoderName);

/// 选择最佳编码器（NVENC → QSV → libx264），缓存结果。
QString selectBestEncoder();

/// 硬编质量参数映射（等价性评审校准后写入；CRF18 基准）：
/// 返回 {编码器名, 质量参数名, 质量值} 供 buildArgs 使用。
/// NVENC: -cq <19>（默认映射，评审后可调）
/// QSV:   -global_quality <18>
struct EncoderArgs {
    QString encoder;
    QString qualityFlag;     // "-cq" 或 "-global_quality"
    int     qualityValue = 18;
    QString presetFlag;      // "-preset"
    QString presetValue;
};
EncoderArgs encoderArgsFor(const QString &encoderName);

}  // namespace encoder_probe
