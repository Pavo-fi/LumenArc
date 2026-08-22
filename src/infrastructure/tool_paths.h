/**
 * @file tool_paths.h
 * @brief 外部工具路径定位（bundled ffmpeg / Python 解释器探测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * v1.8.0 P-25：随 Python 分析引擎退役，从 python_analysis_engine 抽出的
 * 静态路径探测（原样迁移，逻辑零改动）。消费方：TimestampOcrEngine（OCR
 * 租户）、CalibrationService（证据 sidecar ffmpeg）——bundled Python 与
 * cv2/numpy/rapidocr 因 OCR/报告租户保留，与退役决策一致。
 */
#pragma once

#include <QString>

class ToolPaths
{
public:
    /// bundled/系统 ffmpeg 可执行路径（找不到返回 "ffmpeg" 走 PATH）
    static QString findFfmpegPath();
    /// ffprobe 探测（bundled ffmpeg/ 目录优先，PATH 兜底；P-69 起公用）
    static QString findFfprobePath();
    /// Python 解释器探测：bundled → 环境变量 → 注册表 → 常见路径 → py 启动器；
    /// 找不到返回空串（OCR 租户初始化时由调用方决定降级行为）
    static QString detectPythonPath();
};
