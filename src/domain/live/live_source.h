/**
 * @file live_source.h
 * @brief 监控直播源描述与网络流地址组装（纯逻辑，无 Qt Widgets）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计说明（为什么放在 domain）：
 * - 录像机 IP/通道/码流 → RTSP 地址是**纯字符串组装**，无 IO、无状态、
 *   无 Qt Widgets 依赖，属于可单测的领域逻辑（DEVELOPMENT_STANDARDS §2.1）。
 * - 基础设施层（直播引擎）只接收最终 URL，不认识"通道/码流"这些设备概念。
 */
#pragma once

#include <QString>

namespace live {

/// 码流类型（大华 RTSP 的 subtype 参数：0=主码流 1=子码流）
enum class StreamKind { Main = 0, Sub = 1 };

/// RTSP 传输层协议（TCP 穿透性最好，现场首选）
enum class Transport { Tcp = 0, Udp = 1 };

/// 一路直播源配置。
/// `urlOverride` 非空时直接使用（通用 RTSP / 非大华设备 / 已知完整地址）。
struct LiveSourceConfig {
    QString host;                       ///< 录像机 IP 或主机名
    int port = 554;                     ///< RTSP 端口
    QString username;                   ///< 设备用户名
    QString password;                   ///< 设备密码（不落盘，见 LiveMonitorWindow）
    int channel = 1;                    ///< 通道号（1 起）
    StreamKind kind = StreamKind::Main; ///< 主/子码流
    Transport transport = Transport::Tcp;
    QString urlOverride;                ///< 非空则直接使用该地址

    /// 必填项是否齐备（urlOverride 时只看后者）
    bool isComplete() const;

    /// 界面显示名，例如 "192.168.1.108 · 通道 2 · 子码流"
    QString displayName() const;
};

/// RFC 3986 userinfo 百分号编码（保留 A-Za-z0-9-._~，其余转义）。
/// 设备密码常含 @ : / ? # 等字符，不编码会破坏 URL 结构。
QString percentEncodeUserInfo(const QString &s);

/// 组装大华录像机 RTSP 地址：
///   rtsp://user:pass@host:port/cam/realmonitor?channel=N&subtype=S
/// host/username 为空时返回空串（调用方先做 isComplete 校验）。
QString buildDahuaRtspUrl(const LiveSourceConfig &cfg);

/// 最终用于打开的网络流地址（urlOverride 优先）。
QString resolveStreamUrl(const LiveSourceConfig &cfg);

/// 掩码显示用地址（密码替换为 ***），用于状态栏/日志，避免密码泄露。
QString maskedStreamUrl(const LiveSourceConfig &cfg);

/// RTSP 传输协议参数名/值（供基础设施层设置 demuxer 选项）。
/// 返回 "tcp"/"udp"。
QString transportName(Transport t);

} // namespace live
