/**
 * @file proxy_manager.h
 * @brief 拖拽预览代理管理器：后台转码全 I 帧低分代理，缓存复用，LRU 上限
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-07-26
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计要点：
 * - 代理为 960p、全 I 帧（g=1）、原帧率逐帧不丢，帧号与原片 1:1 对应，
 *   仅服务拖拽/暂停 seek 显示；播放与分析永远走原片（证据链不变）；
 * - 优先 h264_nvenc 硬件编码（有 N 卡时 47min 4K 约 3~5 分钟），
 *   无 NVENC 回退 libx264 ultrafast 限 2 线程；
 * - 缓存键 = SHA1(规范化路径|大小|mtime)，跨会话复用，10GB LRU 清理；
 * - 写临时文件完成后原子改名，杜绝半成品被当作可用代理。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QProcess>

class ProxyManager : public QObject
{
    Q_OBJECT

public:
    explicit ProxyManager(QObject *parent = nullptr);
    ~ProxyManager() override;

    /// 该视频是否需要代理（>1080p 或 PS/TS 无索引容器）
    static bool needsProxy(const QString &videoPath);
    /// 已就绪的代理路径；不存在返回空串
    QString existingProxy(const QString &videoPath) const;
    /// 请求代理：已存在则立即发 proxyReady；否则后台转码
    void requestProxy(const QString &videoPath);
    /// 取消当前转码任务（切换视频时调用）
    void cancel();
    bool isRunning() const { return m_process != nullptr; }

    static QString cacheDirPath();
    static QString ffmpegPath();

signals:
    void progressChanged(int percent);
    void proxyReady(const QString &proxyPath);
    void proxyFailed(const QString &error);

private:
    void startTranscode(const QString &videoPath, const QString &outPath);
    void startWithEncoder(const QString &videoPath, const QString &outPath,
                          bool useNvenc);
    void onFinished(int exitCode);
    void onReadyRead();
    void enforceCacheLimit();
    QString hashFor(const QString &videoPath) const;
    QString currentFinalPath() const;

    QProcess *m_process = nullptr;
    QString m_videoPath;
    QString m_partPath;          // 生成中的临时文件（.part）
    QString m_finalPath;         // 完成后的正式路径
    double m_srcDurationSec = 0; // 源时长（进度百分比换算）
    bool m_usedNvenc = false;
    bool m_triedNvenc = false;
    static constexpr qint64 CACHE_LIMIT_BYTES = 10LL * 1024 * 1024 * 1024; // 10GB
};
