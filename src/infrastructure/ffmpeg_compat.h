#pragma once
// FFmpeg 头兼容层（2026-09 macOS CI 修复）：
// FFmpeg 8.0（master，BtbN 构建）把 libavutil 头文件改名去下划线
// （sample_fmt.h → samplefmt.h）；stable 发行（homebrew 7.x/8.x）保留旧名。
// 各使用点显式包含本头，取得 AVSampleFormat 枚举——不再依赖
// swresample.h 的传递包含（8.x 已移除该传递，macOS CI 因此报
// "use of undeclared identifier 'AV_SAMPLE_FMT_*'"）。
#if __has_include(<libavutil/samplefmt.h>)
#include <libavutil/samplefmt.h>
#else
#include <libavutil/sample_fmt.h>
#endif