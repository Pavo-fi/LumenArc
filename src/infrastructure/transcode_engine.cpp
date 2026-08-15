/**
 * @file transcode_engine.cpp
 * @brief 统一转码引擎实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "transcode_engine.h"
#include "encoder_probe.h"
#include "python_analysis_engine.h"
#include "domain/preprocess_text.h"

#include <QProcess>
#include <QThread>
#include <QTimer>
#include <QFile>
#include <QDir>

// 转码是用户盯进度等待的前台任务：恢复正常优先级（此前 BELOW_NORMAL 在
// 有后台负载的机器上会被饿死，现场反馈转码巨慢）；同时限制编码线程数
// 留核给主窗口（反馈③诉求：转码期间 UI 保持响应）

TranscodeEngine::TranscodeEngine(QObject *parent)
    : QObject(parent)
{
}

TranscodeEngine::~TranscodeEngine()
{
    cancel();
}

QStringList TranscodeEngine::buildArgs(const TranscodeRequest &req,
                                       const QString &tempOutput)
{
    // v1.7.0 M1：编码器选择。默认空 = libx264（等价性评审实测：本机
    // NVENC p4 23.6s vs libx264 veryfast 19.5s（5min 1440p）——强 CPU 下
    // 软编更快且 SSIM 0.998/PSNR 48dB 达标；硬编留作可选（现场机 CPU
    // 弱时经 UI 显式选择 h264_nvenc/h264_qsv）
    const QString enc = req.encoder.isEmpty()
        ? QStringLiteral("libx264") : req.encoder;
    encoder_probe::EncoderArgs ea = encoder_probe::encoderArgsFor(enc);
    if (ea.encoder == QStringLiteral("libx264"))
        ea.qualityValue = req.crf;   // 软编沿用调用方自定义 CRF（默认 18）

    QStringList args;
    // v1.7.0 M2：重叠剪切（输入侧 -ss/-t；输入 seek 后时间戳重定零，
    // 配合 setpts=PTS-STARTPTS 输出从 0 起，拼接相位一致）
    if (req.trimStartMs > 0)
        args << QStringLiteral("-ss")
             << QString::number(double(req.trimStartMs) / 1000.0, 'f', 3);
    args << QStringLiteral("-i") << req.input;
    if (req.trimEndMs > 0) {
        const qint64 len = req.trimEndMs - req.trimStartMs;
        if (len > 0)
            args << QStringLiteral("-t")
                 << QString::number(double(len) / 1000.0, 'f', 3);
    }
    // 默认参数预设（§5.5.1）：分析引擎与播放内核最优路径
    args << QStringLiteral("-map") << QStringLiteral("0:v:0")
         << QStringLiteral("-map") << QStringLiteral("0:a?");   // 无音轨不报错
    args << QStringLiteral("-c:v") << ea.encoder
         << ea.presetFlag << ea.presetValue
         << ea.qualityFlag << QString::number(ea.qualityValue)
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    // 编码线程上限 = 核心数 - 2（≥2）：转码期间主窗口播放/操作保持响应
    args << QStringLiteral("-threads")
         << QString::number(qMax(2, QThread::idealThreadCount() - 2));
    if (req.fps > 0.0f) {
        // 多段帧率不一时统一 CFR（拼接前置要求，2026-08 人工测试：
        // 8fps/12.5fps 混排 concat 时间戳错位 → 尾段无帧卡住）；
        // 低帧率段重复帧差分编码，体积增量≈0
        args << QStringLiteral("-r") << QString::number(double(req.fps), 'f', 2);
    }
    // 时间戳归零：监控源首帧偏移/时间基各异，concat demuxer 按时间戳
    // 拼接，段间偏移不归一必然错位（PTS-STARTPTS 每段从 0 起播）
    QString vf = QStringLiteral("setpts=PTS-STARTPTS");
    if (req.deinterlace) {
        // 隔行源默认 yadif（探测到 field_order≠progressive 时由调用方置位）
        vf = QStringLiteral("yadif,") + vf;
    }
    args << QStringLiteral("-vf") << vf;
    if (req.keyframeInterval > 0) {
        // 短 GOP：拖拽 seek 只解码 ≤2s（现场反馈：默认 GOP 250 帧太长，
        // 15fps 源 16.7s 才一个关键帧，seek 需逐帧解码整段 → 拖拽卡死）
        args << QStringLiteral("-g") << QString::number(req.keyframeInterval)
             << QStringLiteral("-keyint_min") << QString::number(req.keyframeInterval);
    }
    // 音频：原音轨为 AAC 时直拷（保留原始数据层级，零损失——2026-08
    // 人工反馈：统一重编码可能丢监控音频特征）；否则 aac 重编码且
    // 保留原采样率/声道，asetpts 归零时间戳
    if (req.copyAudio) {
        args << QStringLiteral("-c:a") << QStringLiteral("copy");
    } else {
        args << QStringLiteral("-af") << QStringLiteral("asetpts=PTS-STARTPTS")
             << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-b:a") << QStringLiteral("128k");
        if (req.audioSampleRate > 0)
            args << QStringLiteral("-ar") << QString::number(req.audioSampleRate);
        if (req.audioChannels > 0)
            args << QStringLiteral("-ac") << QString::number(req.audioChannels);
        if (req.audioSampleRate <= 0)
            args << QStringLiteral("-ar") << QStringLiteral("48000");
        if (req.audioChannels <= 0)
            args << QStringLiteral("-ac") << QStringLiteral("2");
    }
    args << QStringLiteral("-movflags") << QStringLiteral("+faststart")
         << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero")
         << QStringLiteral("-f") << QStringLiteral("mp4")
         << QStringLiteral("-progress") << QStringLiteral("pipe:1")
         << QStringLiteral("-nostats")
         << tempOutput;
    return args;
}

void TranscodeEngine::run(const TranscodeRequest &req)
{
    if (isRunning())
        return;
    m_cancelled = false;
    m_lastPercent = -1;
    m_durationMs = req.durationMs;
    m_finalOutput = req.output;
    // 临时输出必须保留 .mp4 扩展名（否则 muxer 无法推断格式）
    m_tempOutput = req.output;
    if (m_tempOutput.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive))
        m_tempOutput.chop(4);
    m_tempOutput += QStringLiteral(".part.mp4");

    m_stdoutBuf.clear();
    m_stderrBuf.clear();
    m_process = new QProcess(this);
    m_process->setProgram(PythonAnalysisEngine::findFfmpegPath());
    m_process->setArguments(buildArgs(req, m_tempOutput));
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &TranscodeEngine::onProgressLine);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_stderrBuf += m_process->readAllStandardError();
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onFinished(code); });

    if (!m_watchdog) {
        m_watchdog = new QTimer(this);
        m_watchdog->setSingleShot(true);
        connect(m_watchdog, &QTimer::timeout, this, [this]() {
            if (m_process)
                m_process->kill();
            emit failed(PreprocessError::Timeout, QStringLiteral("transcode timeout"));
        });
    }
    // 超时：每文件默认 60 分钟（可配项由 v2 参数面开放，§10.2）
    m_watchdog->start(60 * 60 * 1000);
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        emit failed(PreprocessError::TranscodeFailed,
                    QStringLiteral("failed to start ffmpeg"));
    }
}

void TranscodeEngine::cancel()
{
    m_cancelled = true;
    if (m_watchdog)
        m_watchdog->stop();
    if (m_process) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000))
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (!m_tempOutput.isEmpty() && QFile::exists(m_tempOutput))
        QFile::remove(m_tempOutput);   // 半成品清理（§5.6）
}

bool TranscodeEngine::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void TranscodeEngine::onProgressLine()
{
    m_stdoutBuf += m_process->readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuf.left(nl).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        // R-5：out_time_ms 单位是微秒，parseFfmpegProgressMs 已换算为毫秒
        const qint64 ms = preprocess_text::parseFfmpegProgressMs(line);
        if (ms < 0 || m_durationMs <= 0)
            continue;
        const int pct = qBound(0, int(ms * 100 / m_durationMs), 99);
        if (pct != m_lastPercent) {
            m_lastPercent = pct;
            emit progress(pct, m_finalOutput);
        }
    }
}

void TranscodeEngine::onFinished(int exitCode)
{
    if (m_watchdog)
        m_watchdog->stop();
    QProcess *proc = m_process;
    m_process = nullptr;
    if (proc)
        proc->deleteLater();
    if (m_cancelled)
        return;     // 取消由状态机接管（C1）

    if (exitCode != 0) {
        if (QFile::exists(m_tempOutput))
            QFile::remove(m_tempOutput);   // 不产生半成品（规范 C2）
        emit failed(PreprocessError::TranscodeFailed,
                    QStringLiteral("exit %1: %2").arg(exitCode)
                        .arg(QString::fromUtf8(m_stderrBuf.right(500))));
        return;
    }
    // 原子改名进输出位置（§5.5.2）
    if (QFile::exists(m_finalOutput))
        QFile::remove(m_finalOutput);   // 输出避让由命名保证，此处仅为幂等
    if (!QFile::rename(m_tempOutput, m_finalOutput)) {
        emit failed(PreprocessError::OutputConflict,
                    QStringLiteral("rename failed: %1 -> %2")
                        .arg(m_tempOutput, m_finalOutput));
        return;
    }
    emit progress(100, m_finalOutput);
    emit finished(m_finalOutput);
}
