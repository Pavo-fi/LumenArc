/**
 * @file concat_precheck.cpp
 * @brief 拼接前一致性校验实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "concat_precheck.h"

#include <QSet>

namespace {

/// MP4 目标容器 codec 白名单（评审 R-7：mjpeg 不在白名单——
/// ffmpeg 可写 MP4+mjpeg 但播放器兼容性差，一律路由转码）
const QSet<QString> kMp4VideoWhitelist = {
    QStringLiteral("h264"), QStringLiteral("hevc"), QStringLiteral("mpeg4"),
    QStringLiteral("mpeg2video"), QStringLiteral("vp8"), QStringLiteral("vp9"),
};
const QSet<QString> kMp4AudioWhitelist = {
    QStringLiteral("aac"), QStringLiteral("mp3"), QStringLiteral("ac3"),
    QStringLiteral("eac3"), QStringLiteral("opus"), QStringLiteral("flac"),
};

void add(PrecheckResult &res, const QString &name, PrecheckLevel level,
         const QString &detail)
{
    res.items.append({name, level, detail});
}

/// 两两并集校验宏：组内出现不一致即触发
#define DIVERGENT(field) \
    ([&] { for (int i = 1; i < g.size(); ++i) \
        if (g[i].field != g[0].field) return true; return false; }())

} // namespace

bool PrecheckResult::hasBlock() const
{
    for (const auto &i : items)
        if (i.level == PrecheckLevel::Block)
            return true;
    return false;
}

bool PrecheckResult::hasWarn() const
{
    for (const auto &i : items)
        if (i.level == PrecheckLevel::Warn)
            return true;
    return false;
}

PrecheckResult concatPrecheck(const QVector<ProbeResult> &g)
{
    PrecheckResult res;
    if (g.isEmpty())
        return res;

    // 探测失败的文件存在 = 无法保证一致性
    for (const auto &p : g) {
        if (!p.ok())
            add(res, QStringLiteral("probe"), PrecheckLevel::Block,
                QStringLiteral("探测失败: %1 (%2)").arg(p.filePath, p.probeError));
    }

    // --- BLOCK 级 ---
    if (DIVERGENT(videoCodec))
        add(res, QStringLiteral("videoCodec"), PrecheckLevel::Block,
            QStringLiteral("编码器不一致，路由转码"));
    if (DIVERGENT(width) || DIVERGENT(height))
        add(res, QStringLiteral("resolution"), PrecheckLevel::Block,
            QStringLiteral("分辨率不一致，路由转码"));
    if (DIVERGENT(pixFmt))
        add(res, QStringLiteral("pixFmt"), PrecheckLevel::Block,
            QStringLiteral("像素格式不一致（yuvj420p vs yuv420p 视为不一致），路由转码"));
    if (DIVERGENT(audioStreams))
        add(res, QStringLiteral("audioStreams"), PrecheckLevel::Block,
            QStringLiteral("音轨数不一致，路由转码"));

    // 帧率：容差 1‰；>5% 为大偏差
    {
        const double ref = g[0].fps;
        double maxDev = 0.0;
        for (const auto &p : g)
            if (ref > 0 && p.fps > 0)
                maxDev = qMax(maxDev, qAbs(p.fps - ref) / ref);
        if (maxDev > 0.05)
            add(res, QStringLiteral("fps"), PrecheckLevel::Block,
                QStringLiteral("帧率大偏差 (%1%)，路由转码").arg(maxDev * 100, 0, 'f', 1));
        else if (maxDev > 0.001)
            add(res, QStringLiteral("fps"), PrecheckLevel::Warn,
                QStringLiteral("帧率小偏差 (%1%)，输出取首文件").arg(maxDev * 100, 0, 'f', 2));
        for (const auto &p : g)
            if (p.fpsDubious) {
                add(res, QStringLiteral("fps"), PrecheckLevel::Warn,
                    QStringLiteral("%1: avg 与 r_frame_rate 双源不一致").arg(p.filePath));
                break;
            }
    }

    // 目标容器（MP4）codec 白名单 → BLOCK 自动路由转码
    for (const auto &p : g) {
        if (!kMp4VideoWhitelist.contains(p.videoCodec)) {
            add(res, QStringLiteral("containerCompat"), PrecheckLevel::Block,
                QStringLiteral("%1: 视频编码 %2 不在 MP4 白名单，路由转码")
                    .arg(p.filePath, p.videoCodec));
            break;
        }
        if (p.audioStreams > 0 && !kMp4AudioWhitelist.contains(p.audioCodec)) {
            add(res, QStringLiteral("containerCompat"), PrecheckLevel::Block,
                QStringLiteral("%1: 音频编码 %2 不在 MP4 白名单，路由转码")
                    .arg(p.filePath, p.audioCodec));
            break;
        }
    }

    // --- WARN 级 ---
    if (DIVERGENT(profile) || DIVERGENT(level))
        add(res, QStringLiteral("profileLevel"), PrecheckLevel::Warn,
            QStringLiteral("H.264 profile/level 不一致（播放兼容性风险）"));
    if (DIVERGENT(colorRange) || DIVERGENT(colorSpace))
        add(res, QStringLiteral("color"), PrecheckLevel::Warn,
            QStringLiteral("色彩范围/空间不一致（TV/PC 跳变肉眼可见）"));
    if (DIVERGENT(rotation))
        add(res, QStringLiteral("rotation"), PrecheckLevel::Warn,
            QStringLiteral("旋转 metadata 不一致"));
    if (DIVERGENT(audioCodec) || DIVERGENT(audioSampleRate) || DIVERGENT(audioChannels))
        add(res, QStringLiteral("audioParams"), PrecheckLevel::Warn,
            QStringLiteral("音频编码/采样率/声道不一致"));
    for (const auto &p : g) {
        // 评审 R-8：段首非关键帧是源文件自身问题，与拼接无关，仅提示
        if (!p.firstPktKeyframe) {
            add(res, QStringLiteral("firstKeyframe"), PrecheckLevel::Warn,
                QStringLiteral("%1: 段首非关键帧（源文件自身属性，该段开头可能花屏）")
                    .arg(p.filePath));
            break;
        }
    }
    for (const auto &p : g) {
        if (p.durationDubious) {
            add(res, QStringLiteral("duration"), PrecheckLevel::Warn,
                QStringLiteral("%1: 时长存疑（截断文件），拼接后报告实际时长")
                    .arg(p.filePath));
            break;
        }
    }
    for (const auto &p : g) {
        if (!p.indexed) {
            add(res, QStringLiteral("indexed"), PrecheckLevel::Warn,
                QStringLiteral("%1: 无索引容器，拼接输出 seek 可能受限").arg(p.filePath));
            break;
        }
    }

    if (res.items.isEmpty())
        add(res, QStringLiteral("all"), PrecheckLevel::Ok,
            QStringLiteral("全部一致，可无损拼接"));
    return res;
}
