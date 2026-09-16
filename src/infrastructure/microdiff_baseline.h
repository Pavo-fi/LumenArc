/**
 * @file microdiff_baseline.h
 * @brief 微变分析基准提取：离线一遍解码，算逐像素时间中值 B 与噪声基底
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 为什么独立一遍解码：不改动 ffmpeg_video_engine（播放状态机复杂、注入同步
 * 解码风险高），此处用独立 AVFormatContext 打开文件，与 libav_analysis_engine
 * 同一套惰性 sws 建表模式（P-55：长 GOP 素材首帧前 codecpar->format 可能是
 * AV_PIX_FMT_NONE，直接喂 sws_getContext 会 abort）。
 *
 * 性能：实测明景 2560×1440/20fps 素材解码约 150 帧/秒；取 120 秒基准段
 * 约 2400 帧 → 约 16 秒。调用方应给出进度并允许取消（R13 慢操作）。
 */
#pragma once

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

/// 基准提取结果
struct MicroDiffBaseline {
    std::vector<uint8_t> gray;   ///< 逐像素时间中值（width*height，GRAY8）
    int width = 0;
    int height = 0;
    int framesUsed = 0;          ///< 实际参与中值的样本数
    double noiseFloor = 6.0;     ///< 自动标定的噪声基底（灰度级）
    double fps = 0.0;
    QString error;               ///< 非空即失败

    bool ok() const { return !gray.empty() && width > 0 && height > 0 && error.isEmpty(); }
};

/// 进度回调：percent 0..100；返回 false 表示用户取消
using MicroDiffProgressFn = std::function<bool(int percent, const QString &stage)>;

/**
 * @brief 提取基准段（起火前干净段）的逐像素时间中值。
 *
 * @param path 视频路径
 * @param startSec 基准段起点（秒，相对文件）
 * @param durationSec 基准段时长（秒）；建议 60~180
 * @param maxSamples 最多取多少个样本帧做中值（建议 24；样本越多越抗遮挡）
 * @param temporalFrames 噪声标定用的时域窗（与显示参数一致）
 * @param sigma 噪声标定用的空间 σ（与显示参数一致）
 * @param progress 进度回调（可空）
 * @param cancel 取消标志（可空）
 * @return 结果；失败时 error 非空
 *
 * 复杂度 O(解码帧数 + maxSamples·W·H)；内存 O(maxSamples·W·H)（2560×1440
 * ×24 样本 ≈ 88MB）。
 */
MicroDiffBaseline extractMicroDiffBaseline(const QString &path, double startSec,
                                           double durationSec, int maxSamples,
                                           int temporalFrames, double sigma,
                                           const MicroDiffProgressFn &progress = {},
                                           std::atomic<bool> *cancel = nullptr);
