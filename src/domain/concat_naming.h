/**
 * @file concat_naming.h
 * @brief 拼接产物命名规则（§45 定案：LAMerged_<通道>_<首>_<尾>）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 规则（2026-08-17 用户拍板）：
 * - 拼接产物命名 LAMerged_<首视频名>_<尾视频名>.mp4；
 * - 有通道号时带通道前缀：LAMerged_<通道>_<首>_<尾>.mp4；
 * - 首尾视频名中"时间戳以外的共同部分只保留在首视频名"——监控惯例
 *   HHMMSS_<通道>：剥离最后一个 '_' 之后的公共段（如 _100），尾名去掉该段。
 * 例：首 000131_100 / 尾 024556_100（共同后缀 _100）→ LAMerged_000131_100_024556.mp4
 */
#pragma once

#include <QString>

/// 生成拼接产物文件名（不含目录；扩展名 .mp4 由调用方 allocateOutput 追加）
/// @param channel 组通道（默认组/空 → 无通道前缀）
/// @param firstFilePath 组内排序后第一段文件
/// @param lastFilePath  组内排序后最后一段文件
QString concatOutputName(const QString &channel,
                         const QString &firstFilePath,
                         const QString &lastFilePath);