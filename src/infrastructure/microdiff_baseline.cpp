/**
 * @file microdiff_baseline.cpp
 * @brief 微变分析基准提取实现（独立 libav 解码一遍）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "microdiff_baseline.h"

#include "domain/microdiff_core.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

void closeAll(AVFormatContext *&fmt, AVCodecContext *&dec, SwsContext *&sws,
              AVFrame *&frame, AVFrame *&gray, AVPacket *&pkt)
{
    if (pkt)
        av_packet_free(&pkt);
    if (gray)
        av_frame_free(&gray);
    if (frame)
        av_frame_free(&frame);
    if (sws)
        sws_freeContext(sws);
    if (dec)
        avcodec_free_context(&dec);
    if (fmt)
        avformat_close_input(&fmt);
    sws = nullptr;
}

} // namespace

MicroDiffBaseline extractMicroDiffBaseline(const QString &path, double startSec,
                                           double durationSec, int maxSamples,
                                           int temporalFrames, double sigma,
                                           const MicroDiffProgressFn &progress,
                                           std::atomic<bool> *cancel)
{
    MicroDiffBaseline out;
    const double t0 = std::max(0.0, startSec);
    const double dur = std::max(1.0, durationSec);
    const int wantSamples = std::max(4, std::min(64, maxSamples));

    if (!QFileInfo::exists(path)) {
        out.error = QStringLiteral("文件不存在");
        return out;
    }

    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    SwsContext *sws = nullptr;
    AVFrame *frame = av_frame_alloc();
    AVFrame *gray = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    if (!frame || !gray || !pkt) {
        closeAll(fmt, dec, sws, frame, gray, pkt);
        out.error = QStringLiteral("内存分配失败");
        return out;
    }

    const QByteArray pathUtf8 = path.toUtf8();
    if (avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr) < 0) {
        closeAll(fmt, dec, sws, frame, gray, pkt);
        out.error = QStringLiteral("无法打开视频");
        return out;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        closeAll(fmt, dec, sws, frame, gray, pkt);
        out.error = QStringLiteral("无法读取流信息");
        return out;
    }

    const int vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vstream < 0) {
        closeAll(fmt, dec, sws, frame, gray, pkt);
        out.error = QStringLiteral("没有视频流");
        return out;
    }
    const AVStream *st = fmt->streams[vstream];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        closeAll(fmt, dec, sws, frame, gray, pkt);
        out.error = QStringLiteral("不支持的编码格式");
        return out;
    }
    dec = avcodec_alloc_context3(codec);
    if (!dec || avcodec_parameters_to_context(dec, st->codecpar) < 0
        || avcodec_open2(dec, codec, nullptr) < 0) {
        closeAll(fmt, dec, sws, frame, gray, pkt);
        out.error = QStringLiteral("解码器初始化失败");
        return out;
    }

    const double tb = (st->time_base.den > 0) ? av_q2d(st->time_base) : 0.001;
    const double tbase = (st->start_time != AV_NOPTS_VALUE) ? st->start_time * tb : 0.0;
    double fps = 25.0;
    if (st->avg_frame_rate.den > 0 && st->avg_frame_rate.num > 0)
        fps = av_q2d(st->avg_frame_rate);
    else if (st->r_frame_rate.den > 0 && st->r_frame_rate.num > 0)
        fps = av_q2d(st->r_frame_rate);

    QString err;
    // 后退 3 秒起解，保证基准段起点前的关键帧已解码（seek 只能落到关键帧）
    const double seekSec = std::max(0.0, t0 - 3.0);
    if (av_seek_frame(fmt, vstream, static_cast<int64_t>((seekSec + tbase) / tb),
                      AVSEEK_FLAG_BACKWARD) < 0) {
        // 不静默退化为从头解码：t0 靠后的长素材会长时间 0% 进度且难以取消
        err = QStringLiteral("无法定位基准段起点（seek 失败）");
    } else {
        avcodec_flush_buffers(dec);
    }

    const int sampleStep = std::max(1, static_cast<int>(dur * fps / wantSamples));
    const double tEnd = t0 + dur;

    std::vector<std::vector<uint8_t>> samples;
    samples.reserve(static_cast<size_t>(wantSamples));
    int segIndex = -1;              // 基准段内第几帧
    int lockedW = 0, lockedH = 0, lockedFmt = AV_PIX_FMT_NONE;
    bool flushed = false;

    for (;;) {
        if (!err.isEmpty())
            break;
        if (cancel && cancel->load()) {
            err = QStringLiteral("已取消");
            break;
        }
        // --- 取下一帧（送包 + 收帧，EOF 时 flush 解码器）---
        int got = 0;
        while (!got) {
            const int r = av_read_frame(fmt, pkt);
            if (r < 0) {
                if (!flushed) {
                    flushed = true;
                    avcodec_send_packet(dec, nullptr);
                    if (avcodec_receive_frame(dec, frame) == 0)
                        got = 1;
                }
                break;
            }
            if (pkt->stream_index == vstream) {
                if (avcodec_send_packet(dec, pkt) >= 0
                    && avcodec_receive_frame(dec, frame) == 0)
                    got = 1;
            }
            av_packet_unref(pkt);
        }
        if (!got)
            break;

        const int64_t pts = (frame->pts != AV_NOPTS_VALUE) ? frame->pts
                                                           : frame->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE)
            continue;
        const double sec = pts * tb - tbase;
        if (sec < t0)
            continue;
        if (sec >= tEnd)
            break;

        ++segIndex;
        if (progress && (segIndex % 10) == 0) {
            const int pct = static_cast<int>(std::min(99.0, (sec - t0) / dur * 100.0));
            if (!progress(pct, QStringLiteral("解码基准段")))
                break;
        }
        if ((segIndex % sampleStep) != 0)
            continue;

        // --- 惰性建 sws（P-55：首帧后才知道真实像素格式）---
        const int fw = frame->width, fh = frame->height;
        if (fw <= 0 || fh <= 0 || frame->format == AV_PIX_FMT_NONE)
            continue;
        if (lockedW == 0) {
            lockedW = fw;
            lockedH = fh;
            lockedFmt = frame->format;
        } else if (fw != lockedW || fh != lockedH || frame->format != lockedFmt) {
            continue;   // 拼接源分辨率/像素格式切换：跳过异构帧，保持基准与 sws 一致
        }
        if (!sws) {
            sws = sws_getContext(fw, fh, static_cast<AVPixelFormat>(frame->format),
                                 fw, fh, AV_PIX_FMT_GRAY8, SWS_BILINEAR,
                                 nullptr, nullptr, nullptr);
            if (!sws) {
                err = QStringLiteral("颜色空间转换初始化失败");
                break;
            }
            av_frame_unref(gray);
            gray->format = AV_PIX_FMT_GRAY8;
            gray->width = fw;
            gray->height = fh;
            if (av_frame_get_buffer(gray, 32) < 0) {
                err = QStringLiteral("帧缓冲分配失败");
                break;
            }
        }
        sws_scale(sws, frame->data, frame->linesize, 0, fh, gray->data, gray->linesize);

        std::vector<uint8_t> copy(static_cast<size_t>(fw) * fh);
        for (int y = 0; y < fh; ++y)
            std::memcpy(copy.data() + static_cast<size_t>(y) * fw, gray->data[0] + static_cast<size_t>(y) * gray->linesize[0],
                        static_cast<size_t>(fw));
        samples.push_back(std::move(copy));
        if (static_cast<int>(samples.size()) >= wantSamples)
            break;
        av_frame_unref(frame);
    }

    closeAll(fmt, dec, sws, frame, gray, pkt);

    if (!err.isEmpty()) {
        out.error = err;
        return out;
    }
    if (samples.size() < 3 || lockedW <= 0) {
        out.error = QStringLiteral("基准段可用帧不足（换一段更长/更早的区间）");
        return out;
    }

    std::vector<const uint8_t *> ptrs;
    ptrs.reserve(samples.size());
    for (const auto &s : samples)
        ptrs.push_back(s.data());

    microdiff::computeBaselineMedian(ptrs, lockedW, lockedH, out.gray);
    out.width = lockedW;
    out.height = lockedH;
    out.framesUsed = static_cast<int>(samples.size());
    out.fps = fps;
    out.noiseFloor = microdiff::calibrateNoiseFloor(
        ptrs, out.gray, lockedW, lockedH,
        std::max(2, std::min(temporalFrames, static_cast<int>(samples.size()))),
        sigma, 99.5);
    if (progress)
        progress(100, QStringLiteral("完成"));
    return out;
}
