/**
 * @file media_probe_engine.cpp
 * @brief 视频探测引擎实现：avformat 限流探测 + 首包 PTS/关键帧 + 时长交叉验证
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "media_probe_engine.h"
#include "domain/preprocess_text.h"

#include <QThreadPool>
#include <QFileInfo>
#include <QDateTime>
#include <QMetaObject>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/codec_par.h>
#include <libavutil/avutil.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
}

namespace {

QString avErr(int errnum)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(errnum, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

qint64 tsToMs(int64_t ts, AVRational tb)
{
    if (ts == AV_NOPTS_VALUE)
        return 0;
    return av_rescale_q(ts, tb, {1, 1000});
}

int rotationFromSideData(const AVStream *st)
{
    const AVPacketSideData *sd = av_packet_side_data_get(
        st->codecpar->coded_side_data, st->codecpar->nb_coded_side_data,
        AV_PKT_DATA_DISPLAYMATRIX);
    if (sd && sd->size >= 9 * sizeof(int32_t)) {
        const double angle = av_display_rotation_get(
            reinterpret_cast<const int32_t *>(sd->data));
        if (!std::isnan(angle)) {
            int deg = static_cast<int>(-angle);  // FFmpeg 约定：逆时针为正→顺时针角度
            deg %= 360;
            if (deg < 0)
                deg += 360;
            return deg;
        }
    }
    // 兜底：rotate metadata（旧导出工具）
    const AVDictionaryEntry *e = av_dict_get(st->metadata, "rotate", nullptr, 0);
    if (e)
        return QString::fromUtf8(e->value).toInt();
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// ProbeTask
// ---------------------------------------------------------------------------
class ProbeTask : public QRunnable
{
public:
    ProbeTask(MediaProbeEngine *engine, const QString &path)
        : m_engine(engine), m_path(path) {}

    void run() override
    {
        ProbeResult r;
        if (m_engine->m_cancelled.load()) {
            r.filePath = m_path;
            r.probeError = QStringLiteral("cancelled");
        } else {
            r = MediaProbeEngine::probeOne(m_path);
        }
        // 回 UI 线程（Q1）
        QMetaObject::invokeMethod(m_engine, [e = m_engine, r]() { e->onTaskDone(r); },
                                  Qt::QueuedConnection);
    }

private:
    MediaProbeEngine *m_engine;
    QString m_path;
};

// ---------------------------------------------------------------------------
// MediaProbeEngine
// ---------------------------------------------------------------------------
MediaProbeEngine::MediaProbeEngine(QObject *parent)
    : QObject(parent)
    , m_pool(new QThreadPool(this))
{
    qRegisterMetaType<ProbeResult>("ProbeResult");
    m_pool->setMaxThreadCount(4);   // NFR4：100 文件 ≤ 30s
}

MediaProbeEngine::~MediaProbeEngine()
{
    cancel();
    m_pool->waitForDone(3000);
}

void MediaProbeEngine::probe(const QStringList &paths)
{
    if (m_running.exchange(true))
        return;                     // 进行中重复调用：忽略（编程错误可见于日志）
    {
        QMutexLocker lk(&m_resultsMutex);
        m_results.clear();
        m_results.reserve(paths.size());
    }
    m_done = 0;
    m_total = paths.size();
    m_cancelled = false;
    if (paths.isEmpty()) {
        m_running = false;
        emit probeFinished({});
        return;
    }
    for (const QString &p : paths)
        m_pool->start(new ProbeTask(this, p));
}

void MediaProbeEngine::cancel()
{
    m_cancelled = true;
    m_pool->clear();
}

void MediaProbeEngine::onTaskDone(const ProbeResult &result)
{
    {
        QMutexLocker lk(&m_resultsMutex);
        m_results.append(result);
    }
    if (!result.probeError.isEmpty() && result.probeError != QLatin1String("cancelled"))
        emit probeFailed(result.filePath, result.probeError);
    emit probeResultReady(result);
    const int done = ++m_done;
    emit probeProgress(done, m_total.load());
    if (done >= m_total.load()) {
        m_running = false;
        QVector<ProbeResult> all;
        {
            QMutexLocker lk(&m_resultsMutex);
            all = m_results;
        }
        emit probeFinished(all);
    }
}

// ---------------------------------------------------------------------------
// probeOne（同步单文件探测）
// ---------------------------------------------------------------------------
ProbeResult MediaProbeEngine::probeOne(const QString &path)
{
    ProbeResult r;
    r.filePath = path;

    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isReadable()) {
        r.probeError = QStringLiteral("file unreadable");
        return r;
    }
    r.fileMtimeMs = fi.lastModified().toMSecsSinceEpoch();

    AVFormatContext *fmt = nullptr;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "probesize", "5M", 0);        // 限流防呆（§5.1）
    av_dict_set(&opts, "analyzeduration", "5M", 0);  // 5s 上限
    // 内容嗅探（不按扩展名强制格式）：伪 MP4 与播放内核同源
    int ret = avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        r.probeError = QStringLiteral("open failed: %1").arg(avErr(ret));
        return r;
    }
    ret = avformat_find_stream_info(fmt, nullptr);
    if (ret < 0) {
        r.probeError = QStringLiteral("stream info failed: %1").arg(avErr(ret));
        avformat_close_input(&fmt);
        return r;
    }

    r.container = QString::fromUtf8(fmt->iformat->name);

    const int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx < 0) {
        r.probeError = QStringLiteral("no video stream");
        avformat_close_input(&fmt);
        return r;
    }
    AVStream *vs = fmt->streams[vIdx];
    const AVCodecParameters *cp = vs->codecpar;
    r.videoCodec = QString::fromUtf8(avcodec_get_name(cp->codec_id));
    r.profile = cp->profile;
    r.level = cp->level;
    r.width = cp->width;
    r.height = cp->height;
    r.pixFmt = cp->format >= 0 ? QString::fromUtf8(av_get_pix_fmt_name(
        static_cast<AVPixelFormat>(cp->format))) : QString();
    r.colorRange = QString::fromUtf8(av_color_range_name(cp->color_range));
    r.colorSpace = QString::fromUtf8(av_color_space_name(cp->color_space));
    r.fieldOrder = static_cast<int>(cp->field_order);
    r.rotation = rotationFromSideData(vs);

    const double fpsAvg = av_q2d(vs->avg_frame_rate);
    const double fpsR = av_q2d(vs->r_frame_rate);
    r.fps = fpsAvg > 0 ? fpsAvg : fpsR;
    // 双源校验（§5.1）：偏差 > 1‰ 标记
    if (fpsAvg > 0 && fpsR > 0 && qAbs(fpsAvg - fpsR) / qMax(fpsAvg, fpsR) > 0.001)
        r.fpsDubious = true;

    r.startTimeMs = tsToMs(vs->start_time != AV_NOPTS_VALUE
                               ? vs->start_time : fmt->start_time, vs->time_base);

    // 流内绝对墙钟（Dahua DHAV 等：fmt->start_time 为录制时刻 epoch µs）。
    // 只接受合理纪元区间（2000-01-01 ~ 当前+1天），避免垃圾 PTS 误判。
    // 时区约定：DVR 把本地墙钟当作“UTC 秒”写入（OSD 墙钟与之同基准），
    // 与 OCR 的本地墙钟语义对齐 → 取 UTC 分量按本地时间重解释。
    if (fmt->start_time != AV_NOPTS_VALUE && fmt->start_time > 0) {
        const qint64 epochS = fmt->start_time / 1000000;
        const qint64 nowS = QDateTime::currentSecsSinceEpoch();
        if (epochS >= 946684800 && epochS <= nowS + 86400) {
            const QDateTime utc = QDateTime::fromSecsSinceEpoch(epochS, Qt::UTC);
            r.absStartEpochMs = QDateTime(utc.date(), utc.time(), Qt::LocalTime)
                                    .toMSecsSinceEpoch();
        }
    }

    // 首视频包（PTS 相对换算 + 关键帧标志）+ 关键帧间隔采样：
    // GO 智能路由依据——MP4 也可能关键帧稀疏（>2.5s），同样需要转码重排
    // （现场反馈：原 MP4 关键帧不行时拖拽不流畅）。读包上限 600 防呆。
    AVPacket *pkt = av_packet_alloc();
    QVector<qint64> keyPts;
    int vPkts = 0;
    for (int i = 0; i < 600 && av_read_frame(fmt, pkt) >= 0; ++i) {
        if (pkt->stream_index != vIdx) {
            av_packet_unref(pkt);
            continue;
        }
        ++vPkts;
        const int64_t pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
        const qint64 rel = tsToMs(pts, vs->time_base) - r.startTimeMs;
        if (vPkts == 1) {
            r.firstFramePtsMs = rel;
            r.firstPktKeyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
        }
        if (pkt->flags & AV_PKT_FLAG_KEY)
            keyPts.append(rel);
        av_packet_unref(pkt);
        if (keyPts.size() >= 5)
            break;
    }
    av_packet_free(&pkt);
    if (keyPts.size() >= 2 && keyPts[1] > keyPts[0]) {
        qint64 gap = keyPts[1] - keyPts[0];
        if (keyPts.size() > 2) {
            // 中位间隔抗噪（B 帧/时间戳抖动）
            QVector<qint64> gaps;
            for (int i = 1; i < keyPts.size(); ++i)
                if (keyPts[i] - keyPts[i - 1] > 0)
                    gaps.append(keyPts[i] - keyPts[i - 1]);
            if (!gaps.isEmpty()) {
                std::sort(gaps.begin(), gaps.end());
                gap = gaps[gaps.size() / 2];
            }
        }
        r.keyframeIntervalMs = int(qBound<qint64>(0, gap, 2147483647LL));
        r.keyframeSparse = r.keyframeIntervalMs > 2500;
    }

    // 时长：容器 vs 流 交叉验证（截断文件虚报 → 存疑标记）
    const qint64 fmtDurMs = fmt->duration != AV_NOPTS_VALUE
        ? fmt->duration / 1000 : 0;
    const qint64 stDurMs = tsToMs(vs->duration, vs->time_base);
    r.durationMs = fmtDurMs > 0 ? fmtDurMs : stDurMs;
    if (r.durationMs <= 0)
        r.durationDubious = true;
    else if (fmtDurMs > 0 && stDurMs > 0
             && qAbs(fmtDurMs - stDurMs) * 20 > qMax(fmtDurMs, stDurMs))
        r.durationDubious = true;   // 偏差 > 5%

    // 索引：TS/PS 无 seek 索引（拼接后 seek 受限，报告注明）
    const bool seekable = fmt->pb && (fmt->pb->seekable & AVIO_SEEKABLE_NORMAL);
    const bool streamless = r.container.contains(QLatin1String("mpegts"))
        || r.container == QLatin1String("mpeg") || r.container == QLatin1String("mpegvideo");
    r.indexed = seekable && !streamless;

    // 音频流
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters *ap = fmt->streams[i]->codecpar;
        if (ap->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        ++r.audioStreams;
        if (r.audioCodec.isEmpty()) {
            r.audioCodec = QString::fromUtf8(avcodec_get_name(ap->codec_id));
            r.audioSampleRate = QString::number(ap->sample_rate);
            r.audioChannels = QString::number(ap->ch_layout.nb_channels);
        }
    }

    // creation_time（格式级 → 视频流级；原始字符串留档，解析值脏值过滤）
    const AVDictionaryEntry *ce = av_dict_get(fmt->metadata, "creation_time", nullptr, 0);
    if (!ce)
        ce = av_dict_get(vs->metadata, "creation_time", nullptr, 0);
    if (ce) {
        r.creationTimeRaw = QString::fromUtf8(ce->value);
        r.creationTimeMs = preprocess_text::parseCreationTimeMs(r.creationTimeRaw);
    }

    avformat_close_input(&fmt);
    return r;
}
