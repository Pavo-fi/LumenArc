/**
 * @file libav_analysis_engine.cpp
 * @brief 进程内 FFmpeg 分析引擎实现（v1.5.0 P3）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-15
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "libav_analysis_engine.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <QThread>
#include <QCoreApplication>
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
    Q_UNUSED(videoPath)
    // v1.5.0 第三批实现（swr→RMS→STFT）；过渡期由设置项切回 Python 引擎。
    emit analysisFailed(tr("libav 音频通路尚未完成，请在设置中切回 Python 引擎"));
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
    const int W = dec->width, H = dec->height;
    if (W <= 0 || H <= 0) {
        closeVideo(fmt, dec);
        return false;
    }

    // swscale → GRAY8（BT.601 表；与 Python 快速路径 format=gray 同一转换，Q-14 方案 A）
    SwsContext *sws = sws_getContext(W, H, dec->pix_fmt, W, H, AV_PIX_FMT_GRAY8,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        closeVideo(fmt, dec);
        return false;
    }
    AVFrame *gray = av_frame_alloc();
    gray->format = AV_PIX_FMT_GRAY8;
    gray->width = W;
    gray->height = H;
    if (av_frame_get_buffer(gray, 32) < 0) {
        av_frame_free(&gray);
        sws_freeContext(sws);
        closeVideo(fmt, dec);
        return false;
    }

    // ROI 预处理（一次）：矩形缩放取整；多边形行 span 光栅化
    struct PreparedRoi {
        RoiSpec spec;
        QRect r;                                  // rect 用
        QVector<RoiSpan> spans;                   // polygon 用
    };
    QVector<PreparedRoi> prepared;
    prepared.reserve(rois.size());
    for (const RoiSpec &roi : rois) {
        PreparedRoi pr;
        pr.spec = roi;
        if (roi.kind == RoiSpec::Rect) {
            pr.r = scaleRectToFrame(roi.rect, W, H, W, H);
        } else {
            pr.spans = rasterizePolygonSpans(roi.polygon, W, H);
        }
        prepared.append(pr);
    }

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

        // GRAY8 转换
        sws_scale(sws, frame->data, frame->linesize, 0, H,
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
    snap.timestamps = mergedTs;
    snap.values = mergedLums;
    for (int k = 0; k < m_rois.size(); ++k) {
        DataEntry e;
        e.roiId = m_rois[k].roiId;
        e.type = (m_rois[k].kind == RoiSpec::Rect) ? DataEntry::Rect : DataEntry::Polygon;
        snap.dataEntries.append(e);
    }
    emit analysisFinished(snap);
}

#include "libav_analysis_engine.moc"
