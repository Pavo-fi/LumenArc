/**
 * @file libav_analysis_engine.cpp
 * @brief 进程内 FFmpeg 分析引擎实现（v1.5.0 P3）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-15
 * @version 1.1（2026-08-18 P-55：亮度分析按解码帧属性惰性建 sws 表，
 *           修复长前导音轨素材探测不到 pix_fmt 触发 libswscale 断言闪退）
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "libav_analysis_engine.h"
#include "domain/audio_denoise.h"   // P-54 谱门控降噪

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/tx.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include <QThread>
#include <QCoreApplication>
#include <QFileInfo>
#include <cmath>

// ============================================================================
// Worker：承载 QThread 生命周期
// ============================================================================
class LibavAnalysisEngine::Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(LibavAnalysisEngine *engine) : m_engine(engine) {}

public slots:
    void runLuminance() { m_engine->runLuminanceTask(); }
    void runAudio() { m_engine->runAudioTask(); }

private:
    LibavAnalysisEngine *m_engine = nullptr;
};

// ============================================================================
// 静态工具
// ============================================================================

/// 多边形扫描线交点（半开区间 [x1,x2) 语义，对齐 cv2.fillPoly 像素覆盖）。
QVector<LibavAnalysisEngine::RoiSpan> LibavAnalysisEngine::rasterizePolygonSpans(
    const QPolygon &poly, int width, int height)
{
    QVector<RoiSpan> spans;
    const int n = poly.size();
    if (n < 3 || width <= 0 || height <= 0)
        return spans;

    // 对每条扫描线 y（bbox 内），收集所有边在该行的 x 交点，排序后两两配对成 span。
    const QRect bb = poly.boundingRect();
    const int y0 = qMax(0, bb.top());
    const int y1 = qMin(height - 1, bb.bottom());
    if (y0 > y1)
        return spans;

    QVector<QPair<int, double>> edgesX;  // (x 交点, 参数 t)
    for (int y = y0; y <= y1; ++y) {
        edgesX.clear();
        const double fy = static_cast<double>(y);
        for (int i = 0; i < n; ++i) {
            const QPoint &a = poly[i];
            const QPoint &b = poly[(i + 1) % n];
            const int ay = a.y(), by = b.y();
            // 水平边跳过（不贡献交点）
            if (ay == by)
                continue;
            // 边归一化：从 ymin 到 ymax（交点公式与 OpenCV polyfill 一致）
            const QPoint &lo = (ay < by) ? a : b;
            const QPoint &hi = (ay < by) ? b : a;
            const int yLo = lo.y(), yHi = hi.y();
            // 半开规则：y 属于 [yLo, yHi)（顶点行不重复计数）
            if (fy < yLo || fy >= yHi)
                continue;
            const double x = lo.x()
                + (hi.x() - lo.x()) * (fy - yLo) / (hi.y() - yLo);
            edgesX.append({static_cast<int>(std::round(x)), 0.0});
        }
        if (edgesX.size() < 2)
            continue;
        std::sort(edgesX.begin(), edgesX.end(),
                  [](const QPair<int, double> &p, const QPair<int, double> &q) {
                      return p.first < q.first;
                  });
        // 两两配对（偶奇），闭合区间 [xa, xb]（cv2 fillPoly 实测：
        // round 取整 + 含右端点；顶点行 1px 差异可忽略）
        for (int k = 0; k + 1 < edgesX.size(); k += 2) {
            int xa = qMax(0, edgesX[k].first);
            int xb = qMin(width - 1, edgesX[k + 1].first);
            if (xb >= xa)
                spans.append({y, xa, xb + 1});
        }
    }
    return spans;
}

QRect LibavAnalysisEngine::scaleRectToFrame(const QRect &roi, int outW, int outH,
                                            int origW, int origH)
{
    const double sx = outW / static_cast<double>(qMax(1, origW));
    const double sy = outH / static_cast<double>(qMax(1, origH));
    const int x1 = qMax(0, static_cast<int>(std::lround(roi.x() * sx)));
    const int y1 = qMax(0, static_cast<int>(std::lround(roi.y() * sy)));
    const int x2 = qMin(outW, static_cast<int>(std::lround((roi.x() + roi.width()) * sx)));
    const int y2 = qMin(outH, static_cast<int>(std::lround((roi.y() + roi.height()) * sy)));
    return QRect(x1, y1, x2 - x1, y2 - y1);
}

// ============================================================================
// 构造 / 析构
// ============================================================================
LibavAnalysisEngine::LibavAnalysisEngine(QObject *parent)
    : IAnalysisEngine(parent)
{
    m_worker = new Worker(this);
    auto *thread = new QThread(this);
    thread->setObjectName(QStringLiteral("libavAnalysis"));
    m_worker->moveToThread(thread);
    connect(thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &LibavAnalysisEngine::beginAnalysis,
            m_worker, &Worker::runLuminance);
    connect(this, &LibavAnalysisEngine::beginAudio,
            m_worker, &Worker::runAudio);
    thread->start();
}

LibavAnalysisEngine::~LibavAnalysisEngine()
{
    m_cancel = true;
    if (m_worker->thread()) {
        m_worker->thread()->quit();
        m_worker->thread()->wait(3000);
    }
}

// ============================================================================
// 接口实现
// ============================================================================
void LibavAnalysisEngine::startAnalysis(const QString &videoPath,
                                        const QVector<QRect> &regions,
                                        const QVector<QPolygon> &polygons,
                                        const QStringList &extraVideos,
                                        const QVector<int> &rectRoiIds,
                                        const QVector<int> &polygonRoiIds)
{
    if (m_running.load())
        return;

    m_videoPath = videoPath;
    m_extraVideos = extraVideos;
    m_rois.clear();
    for (int i = 0; i < regions.size(); ++i) {
        RoiSpec spec;
        spec.kind = RoiSpec::Rect;
        spec.rect = regions[i];
        spec.roiId = (i < rectRoiIds.size()) ? rectRoiIds[i] : -1;
        m_rois.append(spec);
    }
    for (int i = 0; i < polygons.size(); ++i) {
        RoiSpec spec;
        spec.kind = RoiSpec::Polygon;
        spec.polygon = polygons[i];
        spec.roiId = (i < polygonRoiIds.size()) ? polygonRoiIds[i] : -1;
        m_rois.append(spec);
    }

    m_cancel = false;
    m_running = true;
    emit beginAnalysis();
}

void LibavAnalysisEngine::startAudioAnalysis(const QString &videoPath)
{
    if (m_running.load())
        return;
    m_videoPath = videoPath;
    m_cancel = false;
    m_running = true;
    emit beginAudio();
}

void LibavAnalysisEngine::setAudioDenoiseStrength(double strength)
{
    m_audioDenoiseStrength.store(qBound(0.0, strength, 10.0));
}

void LibavAnalysisEngine::cancelAnalysis()
{
    m_cancel = true;
}

bool LibavAnalysisEngine::isRunning() const
{
    return m_running.load();
}

LibavAnalysisEngine::VideoTiming LibavAnalysisEngine::videoTiming(const QString &videoPath)
{
    // v1.7.1 缓存：同文件（大小/mtime 未变）直接复用（切换视频路径多次调用）
    const QFileInfo fi(videoPath);
    const auto it = m_timingCache.constFind(videoPath);
    if (it != m_timingCache.constEnd()
        && it->sizeBytes == fi.size()
        && it->mtimeMs == fi.lastModified().toMSecsSinceEpoch()
        && fi.size() > 0) {
        return it->timing;
    }
    VideoTiming timing;
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    int vstream = -1;
    if (!openVideo(videoPath, &fmt, &dec, &vstream))
        return timing;

    const AVStream *st = fmt->streams[vstream];
    // fps：容器帧率优先（avg_frame_rate 常见错值用 r_frame_rate 兜底）
    double fps = 0.0;
    if (st->avg_frame_rate.den > 0 && st->avg_frame_rate.num > 0)
        fps = av_q2d(st->avg_frame_rate);
    if (fps <= 0.0 && st->r_frame_rate.den > 0 && st->r_frame_rate.num > 0)
        fps = av_q2d(st->r_frame_rate);
    if (fps <= 0.0)
        fps = 30.0;

    // 前 48 帧 PTS 实测校准（复刻 analyze_video.py _probe_video：
    // 中位间隔，偏差 >4% 以实测为准——DVR 文件元数据常翻倍/错误）
    QVector<double> ptsMs;
    AVFrame *frame = av_frame_alloc();
    int64_t startPts = AV_NOPTS_VALUE;
    double tb = st->time_base.den ? av_q2d(st->time_base) : 0.001;
    for (int i = 0; i < 48; ++i) {
        if (!readNextVideoFrame(fmt, dec, vstream, frame))
            break;
        int64_t pts = frame->pts;
        if (pts == AV_NOPTS_VALUE)
            pts = frame->best_effort_timestamp;
        if (pts == AV_NOPTS_VALUE)
            continue;
        if (startPts == AV_NOPTS_VALUE)
            startPts = pts;
        const double ms = (pts - startPts) * tb * 1000.0;
        if (ms > 0.0)
            ptsMs.append(ms);
    }
    av_frame_free(&frame);
    closeVideo(fmt, dec);

    double measuredFps = 0.0;
    if (ptsMs.size() >= 8) {
        QVector<double> deltas;
        for (int i = 1; i < ptsMs.size(); ++i) {
            const double d = ptsMs[i] - ptsMs[i - 1];
            if (d > 0.5)
                deltas.append(d);
        }
        if (!deltas.isEmpty()) {
            std::sort(deltas.begin(), deltas.end());
            const double median = deltas[deltas.size() / 2];
            measuredFps = 1000.0 / median;
        }
    }
    if (measuredFps >= 3.0 && measuredFps <= 240.0
        && std::fabs(measuredFps - fps) / fps > 0.04) {
        fps = measuredFps;
    }

    timing.fps = static_cast<float>(fps);
    if (st->duration > 0) {
        timing.durationMs = static_cast<qint64>(st->duration * tb * 1000.0);
    } else if (fmt->duration > 0) {
        timing.durationMs = static_cast<qint64>(fmt->duration / 1000);
    }
    if (timing.fps > 0.0f && timing.durationMs > 0 && fi.size() > 0) {
        TimingCacheEntry e;
        e.timing = timing;
        e.sizeBytes = fi.size();
        e.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
        m_timingCache.insert(videoPath, e);
    }
    return timing;
}

// ============================================================================
// libav 封装
// ============================================================================
bool LibavAnalysisEngine::openVideo(const QString &path, AVFormatContext **fmtOut,
                                    AVCodecContext **decOut, int *vstreamOut)
{
    AVFormatContext *fmt = nullptr;
    const QByteArray pathUtf8 = path.toUtf8();
    if (avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr) < 0)
        return false;
    // v1.7.1：限制流信息分析时长（默认对长文件可读数 MB 耗时秒级；
    // 500ms 已足够拿到码率/时长/帧率元数据——用户实测切换卡顿优化）
    fmt->max_analyze_duration = AV_TIME_BASE / 2;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    int vstream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            vstream = static_cast<int>(i);
            break;
        }
    }
    if (vstream < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    const AVCodec *codec = avcodec_find_decoder(
        fmt->streams[vstream]->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return false;
    }
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    if (!dec) {
        avformat_close_input(&fmt);
        return false;
    }
    // 软解自动多线程（对齐播放引擎：thread_count=0；硬解才需单线程）
    dec->thread_count = 0;
    if (avcodec_parameters_to_context(dec, fmt->streams[vstream]->codecpar) < 0
        || avcodec_open2(dec, codec, nullptr) < 0) {
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return false;
    }

    *fmtOut = fmt;
    *decOut = dec;
    *vstreamOut = vstream;
    return true;
}

void LibavAnalysisEngine::closeVideo(AVFormatContext *fmt, AVCodecContext *dec)
{
    if (dec)
        avcodec_free_context(&dec);
    if (fmt)
        avformat_close_input(&fmt);
}

bool LibavAnalysisEngine::readNextVideoFrame(AVFormatContext *fmt,
                                             AVCodecContext *dec,
                                             int vstream, AVFrame *frame)
{
    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
        return false;
    bool ok = false;
    bool flushed = false;
    while (!m_cancel.load()) {
        if (!flushed) {
            const int r = av_read_frame(fmt, pkt);
            if (r < 0) {
                // EOF：进入 flush 模式（B 帧缓冲尾帧必须 send NULL 吐出）
                flushed = true;
                avcodec_send_packet(dec, nullptr);
                continue;
            }
            if (pkt->stream_index != vstream) {
                av_packet_unref(pkt);
                continue;
            }
            if (avcodec_send_packet(dec, pkt) < 0) {
                av_packet_unref(pkt);
                break;
            }
            av_packet_unref(pkt);
        }
        const int d = avcodec_receive_frame(dec, frame);
        if (d == 0) {
            ok = true;
            break;
        }
        if (d == AVERROR(EAGAIN)) {
            if (flushed)
                break;   // flush 后 EAGAIN 说明无剩余帧
            continue;
        }
        break;   // AVERROR_EOF 或其他
    }
    av_packet_free(&pkt);
    return ok;
}

// ============================================================================
// 亮度分析
// ============================================================================
bool LibavAnalysisEngine::analyzeLuminanceOne(const QString &path,
                                              const QVector<RoiSpec> &rois,
                                              qint64 totalFramesEst,
                                              QVector<qint64> *outTs,
                                              QVector<QVector<qreal>> *outLums)
{
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    int vstream = -1;
    if (!openVideo(path, &fmt, &dec, &vstream))
        return false;

    const AVStream *st = fmt->streams[vstream];
    const double tb = (st->time_base.den > 0) ? av_q2d(st->time_base) : 0.001;

    // P-55 修复（2026-08-18）：长 GOP 素材（明景拼接视频 GOP=12.5s 实测）在
    // max_analyze_duration=500ms 探测窗口内 find_stream_info 拿不到像素格式
    //（dec->pix_fmt == AV_PIX_FMT_NONE），直接喂 sws_getContext 会触发
    // libswscale av_assert(desc) → 整个进程 abort 闪退。
    // 改为与播放引擎（ffmpeg_video_engine）同一模式：首帧解码后按
    // frame->width/height/format 惰性建表；帧属性中途变化（拼接源分辨率
    // 切换）时重建转换表/缓冲并重做 ROI 预处理。正常素材 frame 属性与
    // codecpar 一致——正常路径行为零变化。
    SwsContext *sws = nullptr;
    AVFrame *gray = av_frame_alloc();
    if (!gray) {
        closeVideo(fmt, dec);
        return false;
    }
    int curW = 0, curH = 0, curFmt = AV_PIX_FMT_NONE;

    // ROI 预处理：矩形缩放取整；多边形行 span 光栅化（随帧尺寸重建）
    struct PreparedRoi {
        RoiSpec spec;
        QRect r;                                  // rect 用
        QVector<RoiSpan> spans;                   // polygon 用
    };
    QVector<PreparedRoi> prepared;

    // 为当前帧属性建立/重建 GRAY8 转换上下文与 ROI 预处理；失败返回 false
    auto prepareForFrame = [&](int fw, int fh, int ffmt) -> bool {
        if (fw <= 0 || fh <= 0 || ffmt == AV_PIX_FMT_NONE)
            return false;
        SwsContext *ctx = sws_getContext(fw, fh, static_cast<AVPixelFormat>(ffmt),
                                         fw, fh, AV_PIX_FMT_GRAY8,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!ctx)
            return false;
        sws_freeContext(sws);
        sws = ctx;
        av_frame_unref(gray);
        gray->format = AV_PIX_FMT_GRAY8;
        gray->width = fw;
        gray->height = fh;
        if (av_frame_get_buffer(gray, 32) < 0) {
            sws_freeContext(sws);
            sws = nullptr;
            return false;
        }
        prepared.clear();
        prepared.reserve(rois.size());
        for (const RoiSpec &roi : rois) {
            PreparedRoi pr;
            pr.spec = roi;
            if (roi.kind == RoiSpec::Rect) {
                pr.r = scaleRectToFrame(roi.rect, fw, fh, fw, fh);
            } else {
                pr.spans = rasterizePolygonSpans(roi.polygon, fw, fh);
            }
            prepared.append(pr);
        }
        curW = fw;
        curH = fh;
        curFmt = ffmt;
        return true;
    };

    // 时间戳：帧 PTS 相对首帧（showinfo pts_time 语义：-ss 归零等效）
    AVFrame *frame = av_frame_alloc();
    int64_t startPts = AV_NOPTS_VALUE;
    qint64 frameCount = 0;
    double lastTs = 0.0;

    outTs->clear();
    outLums->clear();
    outLums->resize(rois.size());

    while (!m_cancel.load() && readNextVideoFrame(fmt, dec, vstream, frame)) {
        const int64_t pts = (frame->pts != AV_NOPTS_VALUE)
            ? frame->pts : frame->best_effort_timestamp;
        if (startPts == AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE)
            startPts = pts;
        double tsMs = 0.0;
        if (pts != AV_NOPTS_VALUE)
            tsMs = (pts - startPts) * tb * 1000.0;
        else
            tsMs = ++frameCount / 30.0 * 1000.0;   // PTS 全缺兜底（罕见）
        lastTs = tsMs;

        // GRAY8 转换（BT.601 表；与 Python 快速路径 format=gray 同一转换，
        // Q-14 方案 A）。首帧/帧属性变化时惰性建表（P-55）；建表失败中止
        // 本视频分析（首帧即失败 → outTs 空 → 上层走 analysisFailed）
        if (frame->width != curW || frame->height != curH
            || frame->format != curFmt) {
            if (!prepareForFrame(frame->width, frame->height, frame->format)) {
                av_frame_unref(frame);
                break;
            }
        }
        sws_scale(sws, frame->data, frame->linesize, 0, curH,
                  gray->data, gray->linesize);
        const uint8_t *g = gray->data[0];
        const int gls = gray->linesize[0];

        // ROI 统计
        for (int k = 0; k < prepared.size(); ++k) {
            const PreparedRoi &pr = prepared[k];
            double sum = 0.0;
            qint64 count = 0;
            if (pr.spec.kind == RoiSpec::Rect) {
                const QRect &r = pr.r;
                if (!r.isEmpty()) {
                    for (int y = r.top(); y <= r.bottom(); ++y) {
                        const uint8_t *row = g + y * gls;
                        for (int x = r.left(); x <= r.right(); ++x)
                            sum += row[x];
                    }
                    count = qint64(r.width()) * r.height();
                }
            } else {
                for (const RoiSpan &span : pr.spans) {
                    const uint8_t *row = g + span.y * gls;
                    for (int x = span.x1; x < span.x2; ++x)
                        sum += row[x];
                    count += span.x2 - span.x1;
                }
            }
            const qreal v = (count > 0) ? sum / static_cast<double>(count) : 0.0;
            (*outLums)[k].append(v);
        }
        outTs->append(static_cast<qint64>(std::lround(tsMs)));

        if ((++frameCount & 0x3F) == 0 && totalFramesEst > 0) {
            const qreal pct = 100.0 * frameCount / static_cast<double>(totalFramesEst);
            emit progressUpdated(static_cast<int>(frameCount),
                                 static_cast<int>(totalFramesEst), pct);
        }
        av_frame_unref(frame);
    }

    av_frame_free(&frame);
    av_frame_free(&gray);
    sws_freeContext(sws);
    closeVideo(fmt, dec);

    if (outTs->isEmpty())
        return false;
    // 末尾补一个接近时长的点？不需要——Python 也只到最后一帧。
    return true;
}

void LibavAnalysisEngine::runLuminanceTask()
{
    // 总帧数预探测（进度用）
    qint64 totalEst = 0;
    {
        const VideoTiming vt = videoTiming(m_videoPath);
        totalEst = vt.durationMs > 0 && vt.fps > 0
            ? static_cast<qint64>(vt.durationMs / 1000.0 * vt.fps) : 0;
    }

    QVector<qint64> mergedTs;
    QVector<QVector<qreal>> mergedLums;
    double offsetMs = 0.0;
    bool anyOk = false;
    bool failed = false;

    QStringList allVideos;
    allVideos << m_videoPath;
    for (const QString &v : m_extraVideos)
        if (!v.isEmpty())
            allVideos << v;

    for (int vi = 0; vi < allVideos.size(); ++vi) {
        if (m_cancel.load())
            break;
        QVector<qint64> ts;
        QVector<QVector<qreal>> lums;
        // 本视频总帧估计（进度用；DVR 文件 st->duration 不可靠时退化为 0=无心跳）
        qint64 thisTotal = 0;
        {
            const VideoTiming vt = videoTiming(allVideos[vi]);
            if (vt.fps > 0 && vt.durationMs > 0)
                thisTotal = static_cast<qint64>(vt.durationMs / 1000.0 * vt.fps);
        }
        if (!analyzeLuminanceOne(allVideos[vi], m_rois, thisTotal, &ts, &lums)) {
            if (!anyOk) {
                failed = true;
                break;
            }
            // 后续视频失败：跳过，偏移按 probe 前进（对齐 Python 语义）
            const VideoTiming vt = videoTiming(allVideos[vi]);
            if (vt.fps > 0 && vt.durationMs > 0)
                offsetMs += vt.durationMs;
            continue;
        }
        anyOk = true;

        // 合并：时间戳偏移
        for (int i = 0; i < ts.size(); ++i)
            mergedTs.append(ts[i] + static_cast<qint64>(offsetMs));
        if (mergedLums.isEmpty())
            mergedLums.resize(m_rois.size());
        for (int k = 0; k < lums.size() && k < mergedLums.size(); ++k)
            mergedLums[k] += lums[k];

        // 下一视频偏移：对齐 Python（total_frames / fps * 1000）——
        // st->nb_frames 可用时优先（容器帧数），否则退化为流时长
        const VideoTiming vt = videoTiming(allVideos[vi]);
        if (vt.fps > 0) {
            AVFormatContext *fmt = nullptr;
            AVCodecContext *dec = nullptr;
            int vs = -1;
            qint64 frameCount = 0;
            if (openVideo(allVideos[vi], &fmt, &dec, &vs)) {
                if (fmt->streams[vs]->nb_frames > 0)
                    frameCount = fmt->streams[vs]->nb_frames;
                closeVideo(fmt, dec);
            }
            if (frameCount > 0) {
                offsetMs += frameCount * 1000.0 / vt.fps;
            } else if (vt.durationMs > 0) {
                offsetMs += vt.durationMs;
            } else if (!ts.isEmpty()) {
                offsetMs += ts.last() + 1000.0 / vt.fps;
            }
        }

        emit progressUpdated(vi + 1, allVideos.size(),
                             (vi + 1) * 100.0 / allVideos.size());
    }

    m_running = false;
    if (m_cancel.load()) {
        emit analysisFailed(tr("分析已取消"));
        return;
    }
    if (failed || mergedTs.isEmpty()) {
        emit analysisFailed(tr("libav 亮度分析失败：无法解码视频"));
        return;
    }

    AnalysisSnapshot snap;
    QVector<DataEntry> entries;
    entries.reserve(m_rois.size());
    for (int k = 0; k < m_rois.size(); ++k) {
        DataEntry e;
        e.roiId = m_rois[k].roiId;
        e.type = (m_rois[k].kind == RoiSpec::Rect) ? DataEntry::Rect : DataEntry::Polygon;
        entries.append(e);
    }
    // 防御：行长度与共享时间轴对齐（异常素材帧序错乱时截断，防下游越界；
    // 正常路径零变化——2026-08-18 亮度分析闪退排查加固）
    const int tsN = mergedTs.size();
    for (auto &row : mergedLums)
        if (row.size() > tsN)
            row.resize(tsN);
    snap.setLuminance(std::move(mergedTs), std::move(mergedLums), std::move(entries));
    emit analysisFinished(snap);
}

// ============================================================================
// 音频分析（对齐 analyze_video.py analyze_audio 语义）
// ============================================================================
bool LibavAnalysisEngine::analyzeAudioOne(const QString &path, AudioData *out)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    int astream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            astream = static_cast<int>(i);
            break;
        }
    }
    if (astream < 0) {
        avformat_close_input(&fmt);
        return false;   // 无音轨
    }

    const AVStream *ast = fmt->streams[astream];
    const AVCodec *codec = avcodec_find_decoder(ast->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return false;
    }
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    if (!dec || avcodec_parameters_to_context(dec, ast->codecpar) < 0
        || avcodec_open2(dec, codec, nullptr) < 0) {
        if (dec)
            avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return false;
    }

    // swr → float32 mono 24000Hz（与 Python extract_audio 的 -ac 1 -ar 24000 一致）
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, 1);
    SwrContext *swr = nullptr;
    if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, 24000,
                            &dec->ch_layout, dec->sample_fmt, dec->sample_rate,
                            0, nullptr) < 0 || !swr || swr_init(swr) < 0) {
        if (swr)
            swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return false;
    }

    QVector<float> pcm;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool flushed = false;
    // 拼点空档追踪（P-59 用户实测实锤：前处理拼接产物走 concat 直拷，
    // 每个拼点音频 PTS 插入 ~0.7s 空档对齐墙钟——播放端按 PTS 走，音画正确；
    // 而本分析此前纯按解码序拼 PCM，把空档塌掉 → 音量曲线/语谱时间轴渐进
    // 提前（实测 58 分钟产物 37 分钟处偏早 27s、尾部累计 ~56s）。修复：
    // 逐帧比对 PTS 与「上一帧 PTS+时长」的期望位置，超出阈值即补等量静音）
    int64_t expectPts = AV_NOPTS_VALUE;   // 期望下一帧起点（ast->time_base）
    while (!m_cancel.load()) {
        if (!flushed) {
            const int r = av_read_frame(fmt, pkt);
            if (r < 0) {
                flushed = true;
                avcodec_send_packet(dec, nullptr);
                continue;
            }
            if (pkt->stream_index != astream) {
                av_packet_unref(pkt);
                continue;
            }
            if (avcodec_send_packet(dec, pkt) < 0) {
                av_packet_unref(pkt);
                break;
            }
            av_packet_unref(pkt);
        }
        const int d = avcodec_receive_frame(dec, frame);
        if (d == 0) {
            const int64_t fpts = (frame->pts != AV_NOPTS_VALUE)
                ? frame->pts : frame->best_effort_timestamp;
            // 拼点空档：本帧起点晚于期望位置 → 先补等量静音（>20ms 起补，
            // 单段上限 30s 防御；与下方首偏移补齐同策略）
            if (fpts != AV_NOPTS_VALUE && expectPts != AV_NOPTS_VALUE
                && fpts > expectPts) {
                const double gapMs = double(fpts - expectPts)
                    * av_q2d(ast->time_base) * 1000.0;
                if (gapMs > 20.0) {
                    const int padN = int(std::lround(
                        qMin(gapMs, 30000.0) * 24.0));   // 24 样本/ms@24k
                    const int old = pcm.size();
                    pcm.resize(old + padN);
                    memset(pcm.data() + old, 0, padN * sizeof(float));
                }
            }
            // AAC priming samples：ffmpeg CLI 默认 trim（skip_samples 侧数据），
            // 引擎必须同样丢弃，否则 PCM 与 Python 通路错位（实测 corr 0.57）
            int skip = 0;
            const AVFrameSideData *sd = av_frame_get_side_data(
                frame, AV_FRAME_DATA_SKIP_SAMPLES);
            if (sd && sd->size >= static_cast<int>(sizeof(uint32_t) * 2)) {
                const uint32_t *p = reinterpret_cast<const uint32_t *>(sd->data);
                skip = static_cast<int>(p[0]);   // skip_samples
            }
            // 转换到输出缓冲
            const int outSamples = swr_get_out_samples(swr, frame->nb_samples);
            QVector<float> buf(static_cast<int>(outSamples));
            uint8_t *outPtr = reinterpret_cast<uint8_t *>(buf.data());
            const int got = swr_convert(swr, &outPtr, outSamples,
                                        const_cast<const uint8_t **>(frame->data),
                                        frame->nb_samples);
            const int begin = qMin(skip, got);
            if (got > begin) {
                const int old = pcm.size();
                pcm.resize(old + got - begin);
                memcpy(pcm.data() + old, buf.constData() + begin,
                       (got - begin) * sizeof(float));
            }
            // 期望位置推进：本帧 PTS + 帧时长（输入率样本数换算到流 time_base）；
            // PTS 缺失帧按期望位置自然顺延（无校可依，维持连续假设）
            const int64_t durTb = av_rescale_q(frame->nb_samples,
                {1, frame->sample_rate}, ast->time_base);
            if (fpts != AV_NOPTS_VALUE)
                expectPts = fpts + durTb;
            else if (expectPts != AV_NOPTS_VALUE)
                expectPts += durTb;
            av_frame_unref(frame);
            continue;
        }
        if (d == AVERROR(EAGAIN)) {
            if (flushed)
                break;
            continue;
        }
        break;   // AVERROR_EOF 或其他
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    avcodec_free_context(&dec);

    if (pcm.size() < 2048) {
        avformat_close_input(&fmt);
        return false;   // 有效 PCM 不足一个窗
    }

    // 音频流起始偏移补齐（对齐 probe_stream_starts + adelay：
    // 音频晚于视频 >20ms 补前导静音，上限 30s；早于视频不裁切）
    const double vt = (ast->start_time != AV_NOPTS_VALUE)
        ? ast->start_time * av_q2d(ast->time_base) : 0.0;
    double vs = 0.0;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            const AVStream *s = fmt->streams[i];
            vs = (s->start_time != AV_NOPTS_VALUE)
                ? s->start_time * av_q2d(s->time_base)
                : (fmt->start_time != AV_NOPTS_VALUE ? fmt->start_time / AV_TIME_BASE : 0.0);
            break;
        }
    }
    const double deltaMs = (vt - vs) * 1000.0;
    if (deltaMs > 20.0) {
        const double padMs = qMin(deltaMs, 30000.0);
        QVector<float> pad(static_cast<int>(std::lround(padMs * 24.0)), 0.0f);
        pcm = pad + pcm;
    }
    avformat_close_input(&fmt);

    // ---- P-54 谱门控降噪（分析显示链路；strength>0 启用；播放音频不动）----
    if (m_audioDenoiseStrength.load() > 0.0)
        spectralGateDenoise(pcm, 24000, m_audioDenoiseStrength.load());

    // ---- RMS 音量（frame 2048 / hop 512，除以 max 归一化） ----
    QVector<qreal> volume;
    volume.reserve(pcm.size() / 512);
    for (int i = 0; i + 2048 <= pcm.size(); i += 512) {
        double sum = 0.0;
        for (int j = 0; j < 2048; ++j) {
            const double v = pcm[i + j];
            sum += v * v;
        }
        volume.append(std::sqrt(sum / 2048.0));
    }
    qreal volMax = 0.0;
    for (qreal v : volume)
        volMax = qMax(volMax, v);
    if (volMax > 0.0)
        for (qreal &v : volume)
            v /= volMax;

    // ---- STFT 语谱（n_fft 1920 / hop 512 / hanning / log10+1e-10） ----
    // Q-15 拍板 av_rdft，但 av_rdft 仅支持 2^n 点；1920=2^7·3·5 非 2 幂，
    // 改用同家族 libavutil av_tx（任意 N 混合基，零新依赖）。
    // 精度：AV_TX_FLOAT_FFT(float32) 动态范围不足——主峰 ~700 时 1e-6 级
    // 底噪被吞（实测 9kHz 底噪 -124dB vs numpy -66dB）；用 DOUBLE_FFT 对齐
    // numpy float64（底噪差 <0.01）。stride 必须是 AVComplexDouble 大小（8+8）。
    constexpr int kFft = 1920;
    constexpr int kHop = 512;
    constexpr int kFreqBins = kFft / 2 + 1;   // 961
    AVTXContext *tx = nullptr;
    av_tx_fn fn = nullptr;
    if (av_tx_init(&tx, &fn, AV_TX_DOUBLE_FFT, 0, kFft, nullptr, 0) < 0) {
        if (tx)
            av_tx_uninit(&tx);
        return false;
    }

    QVector<double> window(kFft);
    for (int i = 0; i < kFft; ++i)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kFft - 1));  // np.hanning

    const int nFrames = 1 + (pcm.size() - kFft) / kHop;
    QVector<QVector<qreal>> spec;
    spec.resize(kFreqBins);
    for (auto &b : spec)
        b.resize(nFrames);
    QVector<double> fftIn(2 * kFft, 0.0);
    QVector<double> fftOut(2 * kFft, 0.0);
    qreal specMin = std::numeric_limits<qreal>::max();
    qreal specMax = std::numeric_limits<qreal>::lowest();

    for (int f = 0; f < nFrames; ++f) {
        const int start = f * kHop;
        for (int i = 0; i < kFft; ++i) {
            fftIn[2 * i] = pcm[start + i] * window[i];
            fftIn[2 * i + 1] = 0.0;
        }
        fn(tx, fftOut.data(), fftIn.data(), sizeof(AVComplexDouble));
        for (int k = 0; k < kFreqBins; ++k) {
            const double re = fftOut[2 * k], im = fftOut[2 * k + 1];
            const qreal mag = std::sqrt(re * re + im * im);
            const qreal v = std::log10(mag + 1e-10);
            spec[k][f] = v;
            specMin = qMin(specMin, v);
            specMax = qMax(specMax, v);
        }
        if (m_cancel.load())
            break;
        if ((f & 0x3F) == 0)
            emit progressUpdated(f + 1, nFrames, 100.0 * (f + 1) / nFrames);
    }
    av_tx_uninit(&tx);
    if (m_cancel.load())
        return false;

    out->volume = volume;
    out->spectrogram = spec;
    out->sampleRate = 24000;
    out->hopLength = kHop;
    out->nFft = kFft;
    out->timeResolutionMs = 1000.0 * kHop / 24000.0;   // 21.3333… 全精度
    out->specMin = specMin;
    out->specMax = specMax;
    return true;
}

void LibavAnalysisEngine::runAudioTask()
{
    AudioData audio;
    const bool ok = analyzeAudioOne(m_videoPath, &audio);
    m_running = false;
    if (m_cancel.load()) {
        emit analysisFailed(tr("分析已取消"));
        return;
    }
    if (!ok) {
        emit analysisFailed(tr("libav 音频分析失败：无法解码音轨"));
        return;
    }
    AnalysisSnapshot snap;
    snap.setAudio(std::move(audio));
    emit analysisFinished(snap);
}

#include "libav_analysis_engine.moc"
