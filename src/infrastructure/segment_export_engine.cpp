#include "segment_export_engine.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QThread>







#include "infrastructure/tool_paths.h"
#include "theme.h"

namespace {
constexpr const char *kErrPrefix = "[SEGMENT_EXPORT] ";
}

SegmentExportEngine::SegmentExportEngine(QObject *parent)
    : QObject(parent)
{
}

SegmentExportEngine::~SegmentExportEngine()
{
    cancel();
    if (m_thread) {
        m_thread->wait(5000);
        delete m_thread;
        m_thread = nullptr;
    }
}

bool SegmentExportEngine::isRunning() const { return m_running; }

void SegmentExportEngine::cancel()
{
    m_cancelled = true;
}

// ---------------------------------------------------------------------------
// 纯函数：音频滤镜链与布局
// ---------------------------------------------------------------------------

QStringList SegmentExportEngine::atempoChain(double rate)
{
    // atempo 单级合法域 [0.5, 2.0]；超出则级联分解
    QStringList chain;
    double r = rate;
    while (r > 2.0 + 1e-9) { chain << QStringLiteral("atempo=2.0"); r /= 2.0; }
    while (r < 0.5 - 1e-9) { chain << QStringLiteral("atempo=0.5"); r *= 2.0; }
    if (qAbs(r - 1.0) > 1e-9)
        chain << QStringLiteral("atempo=%1").arg(r, 0, 'f', 4);
    if (chain.isEmpty())
        chain << QStringLiteral("atempo=1.0");   // 保底合法（concat 一致性）
    return chain;
}

QString SegmentExportEngine::buildAudioFilterChain(const speedplan::SpeedPlan &plan,
                                                   const QString &inputLabel)
{
    const int n = plan.segmentCount();
    QStringList parts;
    for (int i = 0; i < n; ++i) {
        qint64 s, e;
        plan.segmentBounds(i, &s, &e);
        QString chain = QStringLiteral("[%1]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS")
                            .arg(inputLabel)
                            .arg(s / 1000.0, 0, 'f', 3)
                            .arg(e / 1000.0, 0, 'f', 3);
        for (const QString &at : atempoChain(plan.segmentRate(i)))
            chain += QLatin1Char(',') + at;
        chain += QStringLiteral("[a%1]").arg(i);
        parts << chain;
    }
    QStringList inputs;
    for (int i = 0; i < n; ++i)
        inputs << QStringLiteral("[a%1]").arg(i);
    parts << QStringLiteral("%1concat=n=%2:v=0:a=1[aout]").arg(inputs.join(QString()), QString::number(n));
    return parts.join(QStringLiteral(";"));
}

void SegmentExportEngine::layoutRects(const QSize &canvas, bool hasChart, bool hasSpec,
                                      QRect *videoRect, QRect *chartRect, QRect *specRect)
{
    const int W = canvas.width(), H = canvas.height();
    const int pad = 8;
    const int panels = (hasChart ? 1 : 0) + (hasSpec ? 1 : 0);
    int panelH = 0;
    if (panels == 2)
        panelH = 400;             // 曲线 200 + 语谱 192 + 间隔
    else if (panels == 1)
        panelH = 240;
    const QRect video(pad, pad, W - 2 * pad, H - panelH - 2 * pad - (panels ? pad : 0));
    if (videoRect) *videoRect = video;
    int y = video.bottom() + 1 + (panels ? pad : 0);
    if (chartRect) {
        *chartRect = hasChart
            ? QRect(pad, y, W - 2 * pad, panels == 2 ? 200 : 240) : QRect();
        if (hasChart)
            y += chartRect->height() + pad;
    }
    if (specRect) {
        *specRect = hasSpec
            ? QRect(pad, y, W - 2 * pad, panels == 2 ? 192 : 240) : QRect();
    }
}

// ---------------------------------------------------------------------------
// 启动 / 工作线程
// ---------------------------------------------------------------------------

void SegmentExportEngine::start(const Params &p)
{
    if (m_running) {
        emit finished(false, QLatin1String(kErrPrefix) + QStringLiteral("引擎忙"));
        return;
    }
    if (!p.plan.isValid() || p.sourcePath.isEmpty() || p.outputPath.isEmpty()
        || p.canvas.isEmpty() || p.outFps <= 0.0) {
        emit finished(false, QLatin1String(kErrPrefix)
                         + QStringLiteral("参数非法（选段/路径/画布/帧率）"));
        return;
    }
    m_params = p;
    m_cancelled = false;
    m_running = true;
    m_thread = QThread::create([this]() { run(); });
    connect(m_thread, &QThread::finished, this, [this]() {
        m_running = false;
        m_thread->deleteLater();
        m_thread = nullptr;
    });
    m_thread->start();
}

namespace {

/// 水平重映射：把 [A,B] 线性底图按分段变速映射 warp 成输出时间轴宽度
/// （输出列 x ↔ outMs ↔ srcMs ↔ 底图列）。每导出一次一图，亚秒级。
QImage warpStripByPlan(const QImage &base, const speedplan::SpeedPlan &plan,
                       int outW, int outH)
{
    if (base.isNull() || outW <= 0 || outH <= 0)
        return QImage();
    QImage src = base.convertToFormat(QImage::Format_ARGB32);
    QImage dst(outW, outH, QImage::Format_ARGB32);
    dst.fill(QColor(Theme::BgPanel));
    const double outDur = plan.outputDurationMs();
    const double span = double(plan.bMs - plan.aMs);
    if (outDur <= 0.0 || span <= 0.0)
        return dst;
    const int bw = src.width();
    for (int x = 0; x < outW; ++x) {
        const double outMs = (double(x) + 0.5) / outW * outDur;
        const double srcMs = plan.sourceMsAtOutputMs(outMs);
        const int bx = qBound(0, int((srcMs - plan.aMs) / span * bw), bw - 1);
        for (int y = 0; y < outH; ++y) {
            const int sy = qMin(src.height() - 1, int(double(y) / outH * src.height()));
            dst.setPixel(x, y, src.pixel(bx, sy));
        }
    }
    return dst;
}

} // namespace

void SegmentExportEngine::run()
{
    const Params &p = m_params;
    auto fail = [this](const QString &msg) {
        emit finished(false, QLatin1String(kErrPrefix) + msg);
    };

    // ---- 打开源（libav，只读）----

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, p.sourcePath.toUtf8().constData(), nullptr, nullptr) < 0) {
        fail(QStringLiteral("无法打开源文件"));
        return;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        fail(QStringLiteral("流信息读取失败"));
        return;
    }
    int vstream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { vstream = int(i); break; }
    if (vstream < 0) {
        avformat_close_input(&fmt);
        fail(QStringLiteral("无视频流"));
        return;
    }
    bool hasAudio = false;
    for (unsigned i = 0; i < fmt->nb_streams; ++i)
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) { hasAudio = true; break; }

    const AVCodec *codec = avcodec_find_decoder(fmt->streams[vstream]->codecpar->codec_id);
    AVCodecContext *dec = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!dec || avcodec_parameters_to_context(dec, fmt->streams[vstream]->codecpar) < 0
        || avcodec_open2(dec, codec, nullptr) < 0) {
        if (dec) avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        fail(QStringLiteral("解码器打开失败"));
        return;
    }
    const AVRational tb = fmt->streams[vstream]->time_base;

    // 从 A 前最近关键帧起解码（长 GOP 源兜底）
    const int64_t seekTs = av_rescale_q(p.plan.aMs, AV_TIME_BASE_Q, tb);
    av_seek_frame(fmt, vstream, seekTs, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(dec);

    SwsContext *sws = nullptr;
    auto frameToImage = [&](AVFrame *fr, QImage *out) -> bool {
        sws = sws_getCachedContext(sws, fr->width, fr->height, AVPixelFormat(fr->format),
                                   fr->width, fr->height, AV_PIX_FMT_RGBA,
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws)
            return false;
        QImage img(fr->width, fr->height, QImage::Format_RGBA8888);
        uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
        int dstStride[4] = {int(img.bytesPerLine()), 0, 0, 0};
        sws_scale(sws, fr->data, fr->linesize, 0, fr->height, dst, dstStride);
        *out = img;
        return true;
    };

    // ---- 启动 ffmpeg 编码子进程（rawvideo stdin 管道）----

    const QString ffmpeg = ToolPaths::findFfmpegPath();
    QStringList args;
    args << QStringLiteral("-y")
         << QStringLiteral("-f") << QStringLiteral("rawvideo")
         << QStringLiteral("-pix_fmt") << QStringLiteral("rgba")
         << QStringLiteral("-s") << QStringLiteral("%1x%2").arg(p.canvas.width()).arg(p.canvas.height())
         << QStringLiteral("-r") << QString::number(p.outFps, 'f', 3)
         << QStringLiteral("-i") << QStringLiteral("pipe:0");
    if (hasAudio)
        args << QStringLiteral("-i") << p.sourcePath;
    QString filter;
    if (hasAudio) {
        filter = buildAudioFilterChain(p.plan, QStringLiteral("1:a"));
        args << QStringLiteral("-filter_complex") << filter
             << QStringLiteral("-map") << QStringLiteral("0:v")
             << QStringLiteral("-map") << QStringLiteral("[aout]");
    } else {
        args << QStringLiteral("-map") << QStringLiteral("0:v");
    }
    args << QStringLiteral("-c:v") << QStringLiteral("libx264")
         << QStringLiteral("-preset") << QStringLiteral("medium")
         << QStringLiteral("-crf") << QStringLiteral("18")
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (hasAudio)
        args << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-b:a") << QStringLiteral("128k")
             << QStringLiteral("-shortest");
    args << p.outputPath;

    QProcess proc;
    proc.setProgram(ffmpeg);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        sws_freeContext(sws);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        fail(QStringLiteral("ffmpeg 启动失败：") + proc.errorString());
        return;
    }

    // ---- 画布布局 + 底图 warp（一次）----

    QRect videoRect, chartRect, specRect;
    const bool hasChart = !p.chartBase.isNull();
    const bool hasSpec = !p.specBase.isNull();
    layoutRects(p.canvas, hasChart, hasSpec, &videoRect, &chartRect, &specRect);
    QImage chartStrip = hasChart ? warpStripByPlan(p.chartBase, p.plan,
                                                   chartRect.width(), chartRect.height()) : QImage();
    QImage specStrip = hasSpec ? warpStripByPlan(p.specBase, p.plan,
                                                 specRect.width(), specRect.height()) : QImage();

    const qint64 totalFrames = p.plan.outputFrameCount(p.outFps);
    const double outDur = p.plan.outputDurationMs();
    const QString srcBase = QFileInfo(p.sourcePath).completeBaseName();

    // ---- 解码-合成-写管道主循环 ----

    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    QImage curFrame;            // 当前源帧（RGBA）
    double curPtsMs = -1.0;
    bool eof = false;
    qint64 k = 0;
    QString errMsg;
    bool cancelled = false;

    auto pullNext = [&]() -> bool {   // 推进到下一解码帧（填 curFrame/curPtsMs）
        while (!eof) {
            if (m_cancelled) return false;
            int rr = av_read_frame(fmt, pkt);
            if (rr < 0) {   // EOF：flush
                av_packet_unref(pkt);
                avcodec_send_packet(dec, nullptr);
                eof = true;
                continue;
            }
            if (pkt->stream_index != vstream) { av_packet_unref(pkt); continue; }
            if (avcodec_send_packet(dec, pkt) < 0) { av_packet_unref(pkt); continue; }
            av_packet_unref(pkt);
            while (avcodec_receive_frame(dec, fr) == 0) {
                const int64_t pts = (fr->best_effort_timestamp != AV_NOPTS_VALUE)
                                        ? fr->best_effort_timestamp : fr->pts;
                if (pts == AV_NOPTS_VALUE) { av_frame_unref(fr); continue; }
                const double ms = double(pts) * tb.num * 1000.0 / tb.den;
                if (ms < p.plan.aMs - 1.0) { av_frame_unref(fr); continue; }  // 关键帧前导丢弃
                if (!frameToImage(fr, &curFrame)) { av_frame_unref(fr); continue; }
                curPtsMs = ms;
                av_frame_unref(fr);
                return true;
            }
        }
        return false;
    };

    while (k < totalFrames) {
        if (m_cancelled) { cancelled = true; break; }
        const double target = p.plan.sourceMsAtOutputFrame(k, p.outFps);
        // 推进解码到覆盖 target（快放跳帧；慢放 target 前进慢则复用当前帧）
        while ((curPtsMs < 0.0 || curPtsMs < target - 0.5) && !eof) {
            if (!pullNext())
                break;
        }
        if (k == 0) { fprintf(stderr, "[eng] first target %.1f curPts %.1f\n", target, curPtsMs); fflush(stderr); }
        if (curFrame.isNull()) {
            if (!pullNext() || curFrame.isNull()) {
                errMsg = QStringLiteral("源解码失败（选段起点之后无可用帧）");
                break;
            }
        }

        // ---- 合成 ----
        QImage canvas(p.canvas, QImage::Format_RGBA8888);
        canvas.fill(Qt::black);
        {
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            QImage scaled = curFrame.scaled(videoRect.size(), Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
            const int vx = videoRect.x() + (videoRect.width() - scaled.width()) / 2;
            const int vy = videoRect.y() + (videoRect.height() - scaled.height()) / 2;
            painter.drawImage(vx, vy, scaled);

            const double outMs = double(k) * 1000.0 / p.outFps;
            const int cursorX = outDur > 0.0
                ? int(outMs / outDur * p.canvas.width()) : 0;
            if (!chartStrip.isNull()) {
                painter.drawImage(chartRect, chartStrip);
                painter.setPen(QPen(QColor(Theme::Accent), 2));
                painter.drawLine(cursorX, chartRect.top(), cursorX, chartRect.bottom());
            }
            if (!specStrip.isNull()) {
                painter.drawImage(specRect, specStrip);
                painter.setPen(QPen(QColor(Theme::Accent), 2));
                painter.drawLine(cursorX, specRect.top(), cursorX, specRect.bottom());
            }
            if (p.burnOsd) {
                const double rate = p.plan.rateAtOutputMs(outMs);
                QString timeStr;
                if (p.calibration.isValid()) {
                    const qint64 wall = p.calibration.beijingMsOf(qint64(target));
                    timeStr = QDateTime::fromMSecsSinceEpoch(wall).toString(
                                  QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                } else {
                    const qint64 t = qint64(target);
                    timeStr = QStringLiteral("%1:%2:%3")
                                  .arg(t / 3600000, 2, 10, QLatin1Char('0'))
                                  .arg(t / 60000 % 60, 2, 10, QLatin1Char('0'))
                                  .arg(t / 1000 % 60, 2, 10, QLatin1Char('0'));
                }
                QString osd = QStringLiteral("演示副本 · %1x · %2")
                                  .arg(rate, 0, 'g', 3).arg(timeStr);
                if (!p.caseLabel.isEmpty())
                    osd += QStringLiteral(" · 案件 %1").arg(p.caseLabel);
                QFont f = painter.font();
                f.setPixelSize(22);
                f.setBold(true);
                painter.setFont(f);
                const QRect osdRect(videoRect.left() + 12, videoRect.bottom() - 40,
                                    videoRect.width() - 24, 32);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 140));
                painter.drawRoundedRect(osdRect.adjusted(-6, -2, 6, 2), 6, 6);
                painter.setPen(QColor(Theme::Accent));
                painter.drawText(osdRect, Qt::AlignVCenter | Qt::AlignLeft, osd);
            }
        }
        const QByteArray bytes(reinterpret_cast<const char *>(canvas.constBits()),
                               canvas.sizeInBytes());
        qint64 written = 0;
        while (written < bytes.size()) {
            const qint64 w = proc.write(bytes.constData() + written,
                                        bytes.size() - written);
            if (w < 0) { errMsg = QStringLiteral("ffmpeg 管道写入失败"); break; }
            written += w;
            if (m_cancelled) break;
        }
        if (!errMsg.isEmpty() || m_cancelled)
            break;
        ++k;
        if (k % 5 == 0 || k == totalFrames)
            emit progress(int(k), int(totalFrames));
    }

    // ---- 收尾 ----

    proc.closeWriteChannel();
    if (cancelled || m_cancelled) {
        proc.kill();
        proc.waitForFinished(3000);
        QFile::remove(p.outputPath);   // 半成品清理（P4 惯例）
        av_packet_free(&pkt);
        av_frame_free(&fr);
        sws_freeContext(sws);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        emit finished(false, QStringLiteral("已取消"));
        return;
    }
    proc.waitForFinished(-1);
    const bool ok = !errMsg.isEmpty() ? false
                    : (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
    if (!ok && errMsg.isEmpty())
        errMsg = QStringLiteral("ffmpeg 编码失败：")
                 + QString::fromLocal8Bit(proc.readAllStandardError()).right(500);
    av_packet_free(&pkt);
    av_frame_free(&fr);
    sws_freeContext(sws);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    if (!ok) {
        QFile::remove(p.outputPath);
        fail(errMsg);
        return;
    }
    emit progress(int(totalFrames), int(totalFrames));
    emit finished(true, p.outputPath);
}
