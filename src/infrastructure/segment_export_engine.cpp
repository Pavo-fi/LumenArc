#include "segment_export_engine.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <algorithm>
}

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QThread>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDir>







#include "infrastructure/tool_paths.h"
#include "theme.h"

namespace {
constexpr const char *kErrPrefix = "[SEGMENT_EXPORT] ";

/// 编码器探测（P-68④ 实锤：LGPL 版 ffmpeg 无 libx264；按 ffmpeg 二进制路径缓存）
/// 优先 libx264（画质/体积最优）→ libopenh264（LGPG 版自带）→ h264_mf（Windows 保底）
QString pickH264Encoder(const QString &ffmpegPath)
{
    static QHash<QString, QString> cache;
    const auto it = cache.constFind(ffmpegPath);
    if (it != cache.constEnd())
        return it.value();
    QString enc = QStringLiteral("h264_mf");
    QProcess probe;
    probe.start(ffmpegPath, {QStringLiteral("-hide_banner"),
                             QStringLiteral("-encoders")});
    if (probe.waitForFinished(8000)) {
        const QString out = QString::fromLocal8Bit(probe.readAllStandardOutput());
        if (out.contains(QStringLiteral("libx264")))
            enc = QStringLiteral("libx264");
        else if (out.contains(QStringLiteral("libopenh264")))
            enc = QStringLiteral("libopenh264");
    }
    cache.insert(ffmpegPath, enc);
    return enc;
}
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
    QVector<QPair<qint64, qint64>> ranges;
    QVector<double> rates;
    for (int i = 0; i < plan.segmentCount(); ++i) {
        qint64 s, e;
        plan.segmentBounds(i, &s, &e);
        ranges.append({s, e});
        rates.append(plan.segmentRate(i));
    }
    return buildAudioFilterChainRanges(rates, ranges, inputLabel);
}

QString SegmentExportEngine::buildAudioFilterChainRanges(
    const QVector<double> &rates,
    const QVector<QPair<qint64, qint64>> &streamRanges,
    const QString &inputLabel)
{
    const int n = streamRanges.size();
    QStringList parts;
    for (int i = 0; i < n; ++i) {
        const double r = (i < rates.size()) ? rates.at(i) : 1.0;
        QString chain = QStringLiteral("[%1]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS")
                            .arg(inputLabel)
                            .arg(streamRanges[i].first / 1000.0, 0, 'f', 3)
                            .arg(streamRanges[i].second / 1000.0, 0, 'f', 3);
        for (const QString &at : atempoChain(r))
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

/// 放大镜 PIP 绘制（单路/多机共用）：cell 右下角嵌入放大内容（38% 宽，
/// 上限 45% 高），Accent 描边 + 「放大镜 ×N」角标（真机反馈：导出画面
/// 应包含放大镜画面）
/// v1.15.3 主界面同款放大镜源区域标记：金色四角括号（衬影+主体）+ 倍率徽章。
/// 样式复制自 OverlayWidget::drawMagnifierIndicator（引擎层不依赖 widget）。
static void drawMagnifierBrackets(QPainter &painter, const QRect &rect,
                                  qreal zoom, int penWidth, int fontPx)
{
    if (!rect.isValid() || rect.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor accent(Theme::Accent);
    const int arm = qBound(8, qMin(rect.width(), rect.height()) / 4, 40);
    const int w = qMax(1, penWidth);
    auto brackets = [&](const QPoint &off, const QColor &color, int width) {
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::SquareCap));
        const int l = rect.left() + off.x(), t = rect.top() + off.y();
        const int r = rect.right() + off.x(), b = rect.bottom() + off.y();
        painter.drawLine(l, t, l + arm, t);
        painter.drawLine(l, t, l, t + arm);
        painter.drawLine(r, t, r - arm, t);
        painter.drawLine(r, t, r, t + arm);
        painter.drawLine(l, b, l + arm, b);
        painter.drawLine(l, b, l, b - arm);
        painter.drawLine(r, b, r - arm, b);
        painter.drawLine(r, b, r, b - arm);
    };
    brackets(QPoint(w, w), QColor(0, 0, 0, 160), w);
    brackets(QPoint(0, 0), accent, w);
    if (zoom > 0.0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 160));
        painter.drawRect(QRect(rect.left(), rect.top(), 64, 18));
        painter.setPen(accent);
        QFont f = painter.font();
        f.setPixelSize(qMax(10, fontPx));
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(QRect(rect.left() + 2, rect.top(), 60, 18),
                         Qt::AlignVCenter | Qt::AlignLeft,
                         QStringLiteral("×%1").arg(zoom, 0, 'f', 1));
    }
    painter.restore();
}

static void drawPipImage(QPainter &painter, const QRect &cell,
                         const QImage &content, qreal zoomForBadge)
{
    if (content.isNull() || cell.width() < 120)
        return;
    int w = int(cell.width() * 0.38);
    int h = int(qreal(w) * content.height() / qMax(1, content.width()));
    const int maxH = int(cell.height() * 0.45);
    if (h > maxH) {
        h = maxH;
        w = int(qreal(h) * content.width() / qMax(1, content.height()));
    }
    const QRect dst(cell.right() - w - 8, cell.bottom() - h - 8, w, h);
    painter.drawImage(dst, content);
    painter.setPen(QPen(QColor(Theme::Accent), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(dst);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 160));
    painter.drawRect(QRect(dst.left(), dst.top(), 76, 18));
    painter.setPen(QColor(Theme::Accent));
    QFont f = painter.font();
    f.setPixelSize(12);
    painter.setFont(f);
    painter.drawText(QRect(dst.left() + 4, dst.top(), 72, 18),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("放大镜 ×%1").arg(zoomForBadge, 0, 'f', 1));
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
    // 合成导出 P1 双模式参数校验：segments 非空时 plan/sourcePath 豁免
    if (p.evidenceCopy) {
        if (p.segments.isEmpty() || p.outputPath.isEmpty()) {
            emit finished(false, QLatin1String(kErrPrefix)
                             + QStringLiteral("参数非法（证据模式需片段序列与输出路径）"));
            return;
        }
        for (const auto &s : p.segments)
            if (s.isLanes()) {
                emit finished(false, QLatin1String(kErrPrefix)
                                 + QStringLiteral("证据模式仅支持单视频片段（宫格需重编码，请用演示模式）"));
                return;
            }
    } else if (!p.segments.isEmpty()) {
        if (p.outputPath.isEmpty() || p.canvas.isEmpty() || p.outFps <= 0.0) {
            emit finished(false, QLatin1String(kErrPrefix)
                             + QStringLiteral("参数非法（路径/画布/帧率）"));
            return;
        }
        for (const auto &s : p.segments) {
            if (s.inMs < 0 || s.outMs <= s.inMs || s.rate < 0.24 || s.rate > 8.01) {
                emit finished(false, QLatin1String(kErrPrefix)
                                 + QStringLiteral("片段参数非法（区间/速率）"));
                return;
            }
            if (s.isLanes()) {
                // 多通道段校验：2~4 路；合并轨暂不支持（同多机选段导出口径）；
                // 每路须已校时或临时路（有 tempOffset 映射），否则墙钟无义
                if (s.lanes.size() < 2 || s.lanes.size() > 4) {
                    emit finished(false, QLatin1String(kErrPrefix)
                                     + QStringLiteral("多通道段须 2~4 路机位"));
                    return;
                }
                for (const auto &l : s.lanes) {
                    if (l.isMerged()) {
                        emit finished(false, QLatin1String(kErrPrefix)
                                         + QStringLiteral("合并轨暂不支持合成导出（同多机播放窗口径）"));
                        return;
                    }
                    if (!l.calibrated && !l.temporary) {
                        emit finished(false, QLatin1String(kErrPrefix)
                                         + QStringLiteral("机位「%1」未校时，无法对齐同屏").arg(l.displayName));
                        return;
                    }
                }
            } else if (s.sourcePath.isEmpty()) {
                emit finished(false, QLatin1String(kErrPrefix)
                                 + QStringLiteral("片段参数非法（源路径为空）"));
                return;
            }
        }
    } else if (!p.plan.isValid() || p.outputPath.isEmpty()
        || (p.lanes.isEmpty() && p.sourcePath.isEmpty())
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
    if (p.evidenceCopy) {       // 合成导出 P1：证据模式（无损直拷）
        runEvidenceCopy();
        return;
    }
    if (!p.segments.isEmpty()) {   // 合成导出 P1：多段序列模式
        runCompose();
        return;
    }
    if (!p.lanes.isEmpty()) {   // P-68 第 10 条：多机模式（墙钟域 plan）
        runMultiCam();
        return;
    }
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
    dec->pkt_timebase = tb;   // 帧时间戳与 tb 同基（引擎测试对照验证）

    // v1.15.3 冻结根因（湛江遂溪 D15 实测）：部分 DVR 流的包时间戳从
    // start_time 起算（D15=62585s，非 0），而 aMs/bMs 是流内毫秒（从 0 起）——
    // 直接比较恒错，curPtsMs 恒压过 target，主循环永不拉新帧 → 全产物首帧。
    // 解法：seek 与帧位置都按 start_time 归一到流内毫秒（对齐
    // engine_test catchup-bench 的 startPtsUs+startMs 模型）。
    const qint64 startMs = (fmt->start_time != AV_NOPTS_VALUE)
        ? fmt->start_time / 1000 : 0;   // µs → ms；D15 = 62585001
    const qint64 seekUs = startMs * 1000LL + p.plan.aMs * 1000LL;   // 绝对µs
    if (avformat_seek_file(fmt, -1, INT64_MIN, seekUs, seekUs,
                           AVSEEK_FLAG_BACKWARD) < 0)
        av_seek_frame(fmt, vstream,
                      av_rescale_q(startMs + p.plan.aMs, AVRational{1, 1000}, tb),
                      AVSEEK_FLAG_BACKWARD);
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
    const QString encoder = pickH264Encoder(ffmpeg);
    args << QStringLiteral("-c:v") << encoder
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (encoder == QStringLiteral("libx264"))
        args << QStringLiteral("-preset") << QStringLiteral("medium")
             << QStringLiteral("-crf") << QStringLiteral("18");
    else
        args << QStringLiteral("-b:v") << QStringLiteral("8M");
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
            // v1.15.3 冻结修：流内位置以 packet 时间戳为准（stream time_base），
            // 不用 fr->best_effort_timestamp——部分 DVR HEVC 流的帧时间戳不随
            // 帧推进（湛江遂溪 D15 实测），用 fr 时间会致 curPtsMs 恒压过
            // target → 全产物复用首帧。与 engine_test catchup-bench 同源。
            const int64_t pktMs = (pkt->dts != AV_NOPTS_VALUE)
                ? av_rescale_q(pkt->dts, tb, AVRational{1, 1000}) - startMs
                : (pkt->pts != AV_NOPTS_VALUE
                       ? av_rescale_q(pkt->pts, tb, AVRational{1, 1000}) - startMs
                       : AV_NOPTS_VALUE);
            if (avcodec_send_packet(dec, pkt) < 0) { av_packet_unref(pkt); continue; }
            av_packet_unref(pkt);
            while (avcodec_receive_frame(dec, fr) == 0) {
                const double ms = (pktMs != AV_NOPTS_VALUE)
                    ? double(pktMs) : -1.0;
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
            // v1.15.3 拍板：放大镜导出 = 主界面同款左右 50% 并列——
            // 左半边原图（源区域金色四角括号标记），右半边放大视图。
            if (p.magnifierPip && !p.magnifierSrcRect.isNull()
                && videoRect.width() >= 240) {
                const int halfW = videoRect.width() / 2 - 3;
                const QRect leftHalf(videoRect.x(), videoRect.y(),
                                     halfW, videoRect.height());
                const QRect rightHalf(videoRect.x() + halfW + 6, videoRect.y(),
                                      videoRect.width() - halfW - 6,
                                      videoRect.height());
                // 左侧：原图（等比居中）
                QImage scaledL = curFrame.scaled(leftHalf.size(),
                                                 Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
                const QRect dispL(
                    leftHalf.x() + (leftHalf.width() - scaledL.width()) / 2,
                    leftHalf.y() + (leftHalf.height() - scaledL.height()) / 2,
                    scaledL.width(), scaledL.height());
                painter.drawImage(dispL, scaledL);
                // 源区域金色四角括号（主界面同款：衬影+主体+倍率徽章）
                {
                    const QRect src = p.magnifierSrcRect.intersected(
                        QRect(0, 0, curFrame.width(), curFrame.height()));
                    if (src.isValid() && !src.isEmpty()) {
                        const double sx = double(dispL.width())
                                          / curFrame.width();
                        const double sy = double(dispL.height())
                                          / curFrame.height();
                        const QRect mk(dispL.x() + int(src.x() * sx),
                                       dispL.y() + int(src.y() * sy),
                                       qMax(1, int(src.width() * sx)),
                                       qMax(1, int(src.height() * sy)));
                        drawMagnifierBrackets(painter, mk, p.magnifierZoom,
                                              qMax(1, dispL.height() / 540),
                                              qMax(10, dispL.height() / 36));
                    }
                }
                // 右侧：放大视图（同源裁剪 → 旋转 → 等比填满右半）
                QImage crop = curFrame.copy(p.magnifierSrcRect.intersected(
                    QRect(0, 0, curFrame.width(), curFrame.height())));
                if (p.magnifierRotation != 0)
                    crop = crop.transformed(QTransform().rotate(p.magnifierRotation));
                QImage scaledR = crop.scaled(rightHalf.size(),
                                             Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation);
                painter.drawImage(
                    rightHalf.x() + (rightHalf.width() - scaledR.width()) / 2,
                    rightHalf.y() + (rightHalf.height() - scaledR.height()) / 2,
                    scaledR);
            } else {
                painter.drawImage(vx, vy, scaled);
            }

            const double outMs = double(k) * 1000.0 / p.outFps;
            // 光标对齐图表区坐标（原误按画布全宽算，右端溢出 pad 错位；
            // 顶部加三角柄读作「光标」——真机反馈误认为静态时间轴）
            auto drawCursor = [&](const QRect &rc) {
                const int cx = outDur > 0.0
                    ? rc.x() + int(outMs / outDur * rc.width()) : rc.x();
                painter.setPen(QPen(QColor(Theme::Accent), 2));
                painter.drawLine(cx, rc.top(), cx, rc.bottom());
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(Theme::Accent));
                const QPoint tri[3] = { {cx - 6, rc.top()},
                                        {cx + 6, rc.top()},
                                        {cx, rc.top() + 8} };
                painter.drawPolygon(tri, 3);
            };
            if (!chartStrip.isNull()) {
                painter.drawImage(chartRect, chartStrip);
                // 标签竖标（选段内；输出域位置随变速 warp）
                for (const auto &lb : p.labels) {
                    if (lb.timeMs < p.plan.aMs || lb.timeMs > p.plan.bMs)
                        continue;
                    const double lo = p.plan.outputMsAtSourceMs(lb.timeMs);
                    const int lx = chartRect.x()
                        + int(lo / outDur * chartRect.width());
                    painter.setPen(QPen(lb.color, 2));
                    painter.drawLine(lx, chartRect.top(), lx, chartRect.bottom());
                    painter.setPen(lb.color);
                    QFont f = painter.font();
                    f.setPixelSize(11);
                    painter.setFont(f);
                    painter.drawText(QRect(lx + 3, chartRect.top() + 2, 200, 14),
                                     Qt::AlignLeft | Qt::AlignTop, lb.text);
                }
                drawCursor(chartRect);
            }
            if (!specStrip.isNull()) {
                painter.drawImage(specRect, specStrip);
                drawCursor(specRect);
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
                QString osd = QStringLiteral("%1x · %2")
                                  .arg(rate, 0, 'g', 3).arg(timeStr);
                if (!p.caseLabel.isEmpty())
                    osd += QStringLiteral(" · 案件 %1").arg(p.caseLabel);
                // 标签 OSD：播到标签时刻起烧录其内容，显示 5 秒后隐去
                // （多标签重叠取最新一个）
                const ChartLabel *activeLb = nullptr;
                for (const auto &lb : p.labels) {
                    if (lb.timeMs < p.plan.aMs || lb.timeMs > p.plan.bMs)
                        continue;
                    const double lo = p.plan.outputMsAtSourceMs(lb.timeMs);
                    if (outMs >= lo && outMs < lo + 5000.0)
                        activeLb = &lb;
                }
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
                // 标签 OSD 行（主行上方，标签色；显示 5 秒隐去）
                if (activeLb) {
                    const QRect lbRect(videoRect.left() + 12,
                                       osdRect.top() - 38,
                                       videoRect.width() - 24, 30);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(0, 0, 0, 160));
                    painter.drawRoundedRect(lbRect.adjusted(-6, -2, 6, 2), 6, 6);
                    painter.setPen(activeLb->color);
                    painter.drawText(lbRect, Qt::AlignVCenter | Qt::AlignLeft,
                                     QStringLiteral("🏷 %1").arg(activeLb->text));
                    painter.setPen(QColor(Theme::Accent));
                }
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
        // 背压（P-68④ 实测隐患）：QProcess 写缓冲由管道写入线程异步排空，
        // 生产者（解码+合成）快于消费者（x264 编码）时缓冲无限膨胀——
        // 1080p 一帧 8.3MB，长选段可吃光内存。超阈值即等排空。
        while (proc.bytesToWrite() > 256ll * 1024 * 1024 && !m_cancelled) {
            if (!proc.waitForBytesWritten(5000))
                break;
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

// =============================================================================
// 多机模式（P-68 第 10 条）：墙钟域分段变速 + 瓦片宫格 + 覆盖条
// =============================================================================
namespace {

/// 单路顺序解码器（多机导出每路一个；只前进不后退——导出映射单调递增）
struct SeqDecoder {
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    SwsContext *sws = nullptr;
    AVPacket *pkt = nullptr;
    AVFrame *fr = nullptr;
    int vstream = -1;
    AVRational tb {0, 1};
    QImage cur;
    double curPtsMs = -1.0;
    bool eof = false;

    bool open(const QString &path, qint64 startMs, QString *err)
    {
        if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0) {
            *err = QStringLiteral("无法打开");
            return false;
        }
        if (avformat_find_stream_info(fmt, nullptr) < 0) {
            *err = QStringLiteral("流信息失败");
            return false;
        }
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vstream = int(i);
                break;
            }
        if (vstream < 0) { *err = QStringLiteral("无视频流"); return false; }
        const AVCodec *codec = avcodec_find_decoder(fmt->streams[vstream]->codecpar->codec_id);
        dec = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!dec || avcodec_parameters_to_context(dec, fmt->streams[vstream]->codecpar) < 0
            || avcodec_open2(dec, codec, nullptr) < 0) {
            *err = QStringLiteral("解码器失败");
            return false;
        }
        tb = fmt->streams[vstream]->time_base;
        pkt = av_packet_alloc();
        fr = av_frame_alloc();
        if (startMs > 0) {
            av_seek_frame(fmt, vstream, av_rescale_q(startMs, AV_TIME_BASE_Q, tb),
                          AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(dec);
        }
        return true;
    }

    /// 推进解码至覆盖 targetMs（返回 false = 取消）
    bool pullTo(double targetMs, qint64 discardBeforeMs, volatile bool *cancelled)
    {
        while ((curPtsMs < 0.0 || curPtsMs < targetMs - 0.5) && !eof) {
            if (*cancelled)
                return false;
            const int rr = av_read_frame(fmt, pkt);
            if (rr < 0) {
                av_packet_unref(pkt);
                avcodec_send_packet(dec, nullptr);
                eof = true;
                break;
            }
            if (pkt->stream_index != vstream) { av_packet_unref(pkt); continue; }
            if (avcodec_send_packet(dec, pkt) < 0) { av_packet_unref(pkt); continue; }
            av_packet_unref(pkt);
            while (avcodec_receive_frame(dec, fr) == 0) {
                const int64_t pts = (fr->best_effort_timestamp != AV_NOPTS_VALUE)
                                        ? fr->best_effort_timestamp : fr->pts;
                if (pts == AV_NOPTS_VALUE) { av_frame_unref(fr); continue; }
                const double ms = double(pts) * tb.num * 1000.0 / tb.den;
                if (ms < discardBeforeMs - 1.0) { av_frame_unref(fr); continue; }
                sws = sws_getCachedContext(sws, fr->width, fr->height,
                                           AVPixelFormat(fr->format),
                                           fr->width, fr->height, AV_PIX_FMT_RGBA,
                                           SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (sws) {
                    QImage img(fr->width, fr->height, QImage::Format_RGBA8888);
                    uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
                    int stride[4] = {int(img.bytesPerLine()), 0, 0, 0};
                    sws_scale(sws, fr->data, fr->linesize, 0, fr->height, dst, stride);
                    cur = img;
                    curPtsMs = ms;
                }
                av_frame_unref(fr);
                if (curPtsMs >= targetMs - 0.5)
                    return true;
            }
        }
        return true;
    }

    void close()
    {
        if (pkt) av_packet_free(&pkt);
        if (fr) av_frame_free(&fr);
        if (sws) sws_freeContext(sws);
        if (dec) avcodec_free_context(&dec);
        if (fmt) avformat_close_input(&fmt);
        fmt = nullptr; dec = nullptr; sws = nullptr; pkt = nullptr; fr = nullptr;
    }
};

} // namespace

void SegmentExportEngine::runMultiCam()
{
    const Params &p = m_params;
    auto fail = [this](const QString &msg) {
        emit finished(false, QLatin1String(kErrPrefix) + msg);
    };
    const int n = p.lanes.size();
    // ---- 每路解码器（打不开的路标记但继续——其他路照常，格内画错误占位）----
    QVector<SeqDecoder *> decs(n, nullptr);
    QVector<QString> laneErr(n);
    for (int i = 0; i < n; ++i) {
        const SyncLaneData &l = p.lanes[i];
        if (!l.calibrated && !l.temporary) {
            laneErr[i] = QStringLiteral("未校时");
            continue;
        }
        // 该路选段内首个有画面时刻对应的流内起点
        const qint64 ws = qMax(syncLaneWallStart(l), p.plan.aMs);
        const qint64 streamStart = qMax<qint64>(0, syncStreamOf(l, ws));
        auto *d = new SeqDecoder;
        QString err;
        if (!d->open(l.path, streamStart, &err)) {
            laneErr[i] = err;
            d->close();
            delete d;
            continue;
        }
        decs[i] = d;
    }
    bool anyOk = false;
    for (auto *d : decs)
        if (d) anyOk = true;
    if (!anyOk) {
        fail(QStringLiteral("多机：所有机位均无法解码"));
        return;
    }
    // ---- 音轨判定：audioLane 全程覆盖选段才带音 ----
    bool withAudio = false;
    QString audioNote;
    QVector<QPair<qint64, qint64>> audioRanges;
    QVector<double> audioRates;
    if (p.audioLane >= 0 && p.audioLane < n && decs[p.audioLane]) {
        const SyncLaneData &al = p.lanes[p.audioLane];
        bool covered = true;
        for (int i = 0; i < p.plan.segmentCount(); ++i) {
            qint64 ws, we;
            p.plan.segmentBounds(i, &ws, &we);
            const qint64 ss = syncStreamOf(al, ws);
            const qint64 se = syncStreamOf(al, we);
            if (ss < 0 || se > al.durationMs + 500 || se <= ss) {
                covered = false;
                break;
            }
            audioRanges.append({ss, se});
            audioRates.append(p.plan.segmentRate(i));
        }
        if (covered)
            withAudio = true;
        else
            audioNote = QStringLiteral("（音轨机位未全程覆盖选段，产物无音轨）");
    }
    // ---- ffmpeg 子进程 ----
    const QString ffmpeg = ToolPaths::findFfmpegPath();
    QStringList args;
    args << QStringLiteral("-y")
         << QStringLiteral("-f") << QStringLiteral("rawvideo")
         << QStringLiteral("-pix_fmt") << QStringLiteral("rgba")
         << QStringLiteral("-s") << QStringLiteral("%1x%2").arg(p.canvas.width()).arg(p.canvas.height())
         << QStringLiteral("-r") << QString::number(p.outFps, 'f', 3)
         << QStringLiteral("-i") << QStringLiteral("pipe:0");
    if (withAudio)
        args << QStringLiteral("-i") << p.lanes[p.audioLane].path;
    if (withAudio) {
        args << QStringLiteral("-filter_complex")
             << buildAudioFilterChainRanges(audioRates, audioRanges, QStringLiteral("1:a"))
             << QStringLiteral("-map") << QStringLiteral("0:v")
             << QStringLiteral("-map") << QStringLiteral("[aout]");
    } else {
        args << QStringLiteral("-map") << QStringLiteral("0:v");
    }
    const QString encoder = pickH264Encoder(ffmpeg);
    args << QStringLiteral("-c:v") << encoder
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (encoder == QStringLiteral("libx264"))
        args << QStringLiteral("-preset") << QStringLiteral("medium")
             << QStringLiteral("-crf") << QStringLiteral("18");
    else
        args << QStringLiteral("-b:v") << QStringLiteral("8M");
    if (withAudio)
        args << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-b:a") << QStringLiteral("128k")
             << QStringLiteral("-shortest");
    args << p.outputPath;

    QProcess proc;
    proc.setProgram(ffmpeg);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        for (auto *d : decs) { if (d) { d->close(); delete d; } }
        fail(QStringLiteral("ffmpeg 启动失败：") + proc.errorString());
        return;
    }
    // ---- 布局：上宫格 + 下覆盖条 ----
    const int W = p.canvas.width(), H = p.canvas.height();
    const int pad = 8;
    const int stripH = n * 26 + 44;
    const QRect gridRect(pad, pad, W - 2 * pad, H - stripH - 2 * pad);
    const int cols = (n <= 2) ? n : 2;
    const int rows = (n + cols - 1) / cols;
    const int cellW = gridRect.width() / cols;
    const int cellH = gridRect.height() / rows;
    const QRect stripRect(pad, gridRect.bottom() + 1 + pad, W - 2 * pad, stripH - pad);

    const qint64 totalFrames = p.plan.outputFrameCount(p.outFps);
    const double outDur = p.plan.outputDurationMs();
    QString errMsg;
    bool cancelled = false;
    qint64 k = 0;

    while (k < totalFrames) {
        if (m_cancelled) { cancelled = true; break; }
        const double wall = p.plan.sourceMsAtOutputFrame(k, p.outFps);

        const double outMs = double(k) * 1000.0 / p.outFps;

        QImage canvas(p.canvas, QImage::Format_RGBA8888);
        canvas.fill(Qt::black);
        {
            QPainter painter(&canvas);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            // ---- 瓦片宫格 ----
            for (int i = 0; i < n; ++i) {
                const int cx = gridRect.x() + (i % cols) * cellW;
                const int cy = gridRect.y() + (i / cols) * cellH;
                const QRect cell(cx + 2, cy + 2, cellW - 4, cellH - 4);
                const SyncLaneData &l = p.lanes[i];
                QImage frame;
                QString placeholder;
                if (!laneErr[i].isEmpty()) {
                    placeholder = laneErr[i];
                } else {
                    const qint64 stream = syncStreamOf(l, qint64(wall));
                    if (!syncLaneCovers(l, qint64(wall))) {
                        placeholder = QStringLiteral("该时刻无画面");
                    } else {
                        SeqDecoder *d = decs[i];
                        const qint64 discardBefore =
                            syncStreamOf(l, qMax(syncLaneWallStart(l), p.plan.aMs));
                        if (d && d->pullTo(stream, discardBefore, &m_cancelled)
                            && !d->cur.isNull() && d->curPtsMs >= stream - 1500.0)
                            frame = d->cur;
                        else
                            placeholder = QStringLiteral("该时刻无画面");
                    }
                }
                painter.fillRect(cell, QColor(10, 11, 14));
                if (!frame.isNull()) {
                    QImage scaled = frame.scaled(cell.size(), Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
                    painter.drawImage(cell.x() + (cell.width() - scaled.width()) / 2,
                                      cell.y() + (cell.height() - scaled.height()) / 2,
                                      scaled);
                } else {
                    painter.setPen(QColor(Theme::TextMuted));
                    painter.drawText(cell, Qt::AlignCenter, placeholder);
                }
                // 机位名角标
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0, 0, 0, 140));
                painter.drawRect(QRect(cell.left(), cell.top(), 110, 22));
                painter.setPen(Theme::DataPalette[i % Theme::DataPalette.size()]);
                painter.drawText(QRect(cell.left() + 6, cell.top(), 200, 22),
                                 Qt::AlignVCenter | Qt::AlignLeft, l.displayName);
                // 逐路放大镜 PIP（瓦片缩放状态随导出快照入格）
                if (i < p.laneZooms.size() && p.laneZooms[i].zoom > 1.0
                    && !frame.isNull()) {
                    const auto &zs = p.laneZooms[i];
                    const double half = 0.5 / zs.zoom;
                    const double cx = qBound(half, zs.center.x(), 1.0 - half);
                    const double cy = qBound(half, zs.center.y(), 1.0 - half);
                    const QRect src(int((cx - half) * frame.width()),
                                    int((cy - half) * frame.height()),
                                    int(2 * half * frame.width()),
                                    int(2 * half * frame.height()));
                    drawPipImage(painter, cell,
                                 frame.copy(src), zs.zoom);
                }
                painter.setPen(QPen(QColor(Theme::Border), 1));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(cell);
            }
            // ---- 覆盖条（每路行：色块=该路有画面的墙钟区，随分速 warp）----
            painter.fillRect(stripRect, QColor(Theme::BgPanel));
            const int labelW = 110;
            const int barX = stripRect.x() + labelW;
            const int barW = stripRect.width() - labelW - 8;
            for (int i = 0; i < n; ++i) {
                const int ry = stripRect.y() + 6 + i * 26;
                const SyncLaneData &l = p.lanes[i];
                painter.setPen(QColor(Theme::TextSecond));
                QFont f = painter.font();
                f.setPixelSize(12);
                painter.setFont(f);
                painter.drawText(QRect(stripRect.x() + 4, ry, labelW - 8, 20),
                                 Qt::AlignVCenter | Qt::AlignLeft, l.displayName);
                painter.fillRect(QRect(barX, ry + 3, barW, 14), QColor(Theme::BgCard));
                if (laneErr[i].isEmpty()) {
                    const qint64 c1 = qMax(syncLaneWallStart(l), p.plan.aMs);
                    const qint64 c2 = qMin(syncLaneWallEnd(l), p.plan.bMs);
                    if (c2 > c1 && outDur > 0.0) {
                        const int x1 = barX + int(p.plan.outputMsAtSourceMs(c1) / outDur * barW);
                        const int x2 = barX + int(p.plan.outputMsAtSourceMs(c2) / outDur * barW);
                        painter.fillRect(QRect(x1, ry + 3, qMax(2, x2 - x1), 14),
                                         Theme::DataPalette[i % Theme::DataPalette.size()]);
                    }
                }
            }
            // 游标（贯穿宫格+覆盖条）
            const int cursorX = outDur > 0.0 ? int(outMs / outDur * W) : 0;
            painter.setPen(QPen(QColor(Theme::Accent), 2));
            painter.drawLine(cursorX, pad, cursorX, stripRect.bottom());
            // ---- OSD ----
            if (p.burnOsd) {
                const double rate = p.plan.rateAtOutputMs(outMs);
                QString osd = QStringLiteral("%1x · %2")
                                  .arg(rate, 0, 'g', 3)
                                  .arg(QDateTime::fromMSecsSinceEpoch(qint64(wall))
                                           .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
                if (!p.caseLabel.isEmpty())
                    osd += QStringLiteral(" · 案件 %1").arg(p.caseLabel);
                QFont f = painter.font();
                f.setPixelSize(22);
                f.setBold(true);
                painter.setFont(f);
                const QRect osdRect(gridRect.left() + 8, gridRect.bottom() - 36,
                                    gridRect.width() - 16, 30);
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
        // 背压（同单路版注释：防管道写缓冲无限膨胀）
        while (proc.bytesToWrite() > 256ll * 1024 * 1024 && !m_cancelled) {
            if (!proc.waitForBytesWritten(5000))
                break;
        }
        if (!errMsg.isEmpty() || m_cancelled)
            break;
        ++k;
        if (k % 5 == 0 || k == totalFrames)
            emit progress(int(k), int(totalFrames));
    }

    proc.closeWriteChannel();
    for (auto *d : decs) { if (d) { d->close(); delete d; } }
    if (cancelled || m_cancelled) {
        proc.kill();
        proc.waitForFinished(3000);
        QFile::remove(p.outputPath);
        emit finished(false, QStringLiteral("已取消"));
        return;
    }
    proc.waitForFinished(-1);
    const bool ok = errMsg.isEmpty()
        && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (!ok) {
        if (errMsg.isEmpty())
            errMsg = QStringLiteral("ffmpeg 编码失败：")
                     + QString::fromLocal8Bit(proc.readAllStandardError()).right(500);
        QFile::remove(p.outputPath);
        fail(errMsg);
        return;
    }
    emit progress(int(totalFrames), int(totalFrames));
    emit finished(true, p.outputPath + audioNote);
}


// ---------------------------------------------------------------------------
// 合成导出 P1：多段序列模式（2026-09-03 拍板，取代分段导出入口）
// ---------------------------------------------------------------------------

qint64 SegmentExportEngine::composeSegOutFrames(const Params::ComposeSeg &seg,
                                                double outFps)
{
    if (seg.outMs <= seg.inMs || seg.rate <= 0.0 || outFps <= 0.0)
        return 0;
    const double outMs = double(seg.outMs - seg.inMs) / seg.rate;
    return qMax<qint64>(1, llround(outMs / 1000.0 * outFps));
}

QString SegmentExportEngine::buildAudioFilterChainMulti(
    const QVector<double> &rates,
    const QVector<QPair<qint64, qint64>> &streamRanges,
    const QStringList &inputLabels)
{
    const int n = streamRanges.size();
    QStringList parts;
    for (int i = 0; i < n; ++i) {
        const double r = (i < rates.size()) ? rates.at(i) : 1.0;
        const qint64 sMs = streamRanges[i].first, eMs = streamRanges[i].second;
        const double outDurS = (eMs - sMs) / 1000.0 / qMax(0.01, r);
        QString chain;
        const QString label = (i < inputLabels.size()) ? inputLabels.at(i) : QString();
        if (label.isEmpty()) {
            // 该段无音轨：anullsrc 补等长静音（长度 = 输出域时长）
            chain = QStringLiteral("anullsrc=r=48000:cl=stereo,atrim=start=0:end=%1,"
                                   "asetpts=PTS-STARTPTS")
                        .arg(outDurS, 0, 'f', 3);
        } else {
            chain = QStringLiteral("[%1]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS")
                        .arg(label)
                        .arg(sMs / 1000.0, 0, 'f', 3)
                        .arg(eMs / 1000.0, 0, 'f', 3);
            for (const QString &at : atempoChain(r))
                chain += QLatin1Char(',') + at;
        }
        // 全分支统一 48k 立体声 s16（异构源 concat 防御：8k 单声道 DVR 也能拼）
        chain += QStringLiteral(",aresample=48000:ocl=stereo:osf=s16[a%1]").arg(i);
        parts << chain;
    }
    QStringList inputs;
    for (int i = 0; i < n; ++i)
        inputs << QStringLiteral("[a%1]").arg(i);
    parts << QStringLiteral("%1concat=n=%2:v=0:a=1[aout]")
                 .arg(inputs.join(QString()), QString::number(n));
    return parts.join(QStringLiteral(";"));
}

void SegmentExportEngine::runCompose()
{
    const Params &p = m_params;
    auto fail = [this](const QString &msg) {
        emit finished(false, QLatin1String(kErrPrefix) + msg);
    };

    // ---- 源清单（去重，首见序）+ 音频探测 ----
    QStringList uniqFiles;
    for (const auto &s : p.segments) {
        if (s.isLanes()) {
            for (const auto &l : s.lanes)
                if (!uniqFiles.contains(l.path))
                    uniqFiles << l.path;
        } else if (!uniqFiles.contains(s.sourcePath)) {
            uniqFiles << s.sourcePath;
        }
    }
    QHash<QString, bool> hasAudioOf;
    for (const QString &f : uniqFiles) {
        bool has = false;
        AVFormatContext *probe = nullptr;
        if (avformat_open_input(&probe, f.toUtf8().constData(), nullptr, nullptr) >= 0) {
            if (avformat_find_stream_info(probe, nullptr) >= 0)
                for (unsigned i = 0; i < probe->nb_streams; ++i)
                    if (probe->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                        has = true;
                        break;
                    }
            avformat_close_input(&probe);
        }
        hasAudioOf.insert(f, has);
    }
    const bool anyAudio = std::any_of(p.segments.begin(), p.segments.end(),
                                      [&](const Params::ComposeSeg &s) {
                                          if (s.isLanes())
                                              return s.audioLane >= 0
                                                  && s.audioLane < s.lanes.size()
                                                  && hasAudioOf.value(
                                                      s.lanes[s.audioLane].path, false);
                                          return hasAudioOf.value(s.sourcePath, false);
                                      });

    // ---- ffmpeg 编码子进程（rawvideo 管道 + 多源音频 concat）----
    const QString ffmpeg = ToolPaths::findFfmpegPath();
    QStringList args;
    args << QStringLiteral("-y")
         << QStringLiteral("-f") << QStringLiteral("rawvideo")
         << QStringLiteral("-pix_fmt") << QStringLiteral("rgba")
         << QStringLiteral("-s")
         << QStringLiteral("%1x%2").arg(p.canvas.width()).arg(p.canvas.height())
         << QStringLiteral("-r") << QString::number(p.outFps, 'f', 3)
         << QStringLiteral("-i") << QStringLiteral("pipe:0");
    if (anyAudio)
        for (const QString &f : uniqFiles)
            args << QStringLiteral("-i") << f;
    if (anyAudio) {
        QVector<double> rates;
        QVector<QPair<qint64, qint64>> ranges;
        QStringList labels;
        for (const auto &s : p.segments) {
            rates << s.rate;
            if (s.isLanes()) {
                // 宫格段：音轨=主听路。两端覆盖才映射（部分覆盖的细分留 P2，
                // 未覆盖退化为整段静音——标签空走 anullsrc，长度=输出域）
                QString label;
                QPair<qint64, qint64> rng{s.inMs, s.outMs};
                if (s.audioLane >= 0 && s.audioLane < s.lanes.size()) {
                    const SyncLaneData &al = s.lanes[s.audioLane];
                    if (hasAudioOf.value(al.path, false)
                        && syncLaneCovers(al, s.inMs) && syncLaneCovers(al, s.outMs)) {
                        const qint64 ss = syncStreamOf(al, s.inMs);
                        const qint64 se = syncStreamOf(al, s.outMs);
                        if (ss >= 0 && se > ss) {
                            rng = {ss, se};
                            label = QStringLiteral("%1:a")
                                        .arg(uniqFiles.indexOf(al.path) + 1);
                        }
                    }
                }
                ranges.append(rng);
                labels << label;
            } else {
                ranges.append({s.inMs, s.outMs});
                labels << (hasAudioOf.value(s.sourcePath, false)
                               ? QStringLiteral("%1:a").arg(uniqFiles.indexOf(s.sourcePath) + 1)
                               : QString());
            }
        }
        args << QStringLiteral("-filter_complex")
             << buildAudioFilterChainMulti(rates, ranges, labels)
             << QStringLiteral("-map") << QStringLiteral("0:v")
             << QStringLiteral("-map") << QStringLiteral("[aout]");
    } else {
        args << QStringLiteral("-map") << QStringLiteral("0:v");
    }
    const QString encoder = pickH264Encoder(ffmpeg);
    args << QStringLiteral("-c:v") << encoder
         << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
    if (encoder == QStringLiteral("libx264"))
        args << QStringLiteral("-preset") << QStringLiteral("medium")
             << QStringLiteral("-crf") << QStringLiteral("18");
    else
        args << QStringLiteral("-b:v") << QStringLiteral("8M");
    if (anyAudio)
        args << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-b:a") << QStringLiteral("128k")
             << QStringLiteral("-shortest");
    args << p.outputPath;

    QProcess proc;
    proc.setProgram(ffmpeg);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        fail(QStringLiteral("ffmpeg 启动失败：") + proc.errorString());
        return;
    }

    // ---- 布局：多段模式无图表面板，视频区满幅 ----
    QRect videoRect, chartRect, specRect;
    layoutRects(p.canvas, false, false, &videoRect, &chartRect, &specRect);

    qint64 totalFrames = 0;
    for (const auto &s : p.segments)
        totalFrames += composeSegOutFrames(s, p.outFps);
    qint64 doneFrames = 0;
    QString errMsg;
    bool cancelled = false;

    // ---- 逐段解码-合成-写管道 ----
    for (int si = 0; si < p.segments.size() && !cancelled && errMsg.isEmpty(); ++si) {
        const Params::ComposeSeg &seg = p.segments.at(si);
        if (seg.isLanes()) {
            // ---- P1.5 多通道宫格段：逐路 SeqDecoder + 宫格绘制 ----
            // （瓦片画法与 runMultiCam 孪生——刻意不共改其冻结路径；多段模式无覆盖条）
            const int ln = seg.lanes.size();
            QVector<SeqDecoder *> decs(ln, nullptr);
            QVector<QString> laneErr(ln);
            for (int i = 0; i < ln; ++i) {
                const SyncLaneData &l = seg.lanes[i];
                const qint64 ws = qMax(syncLaneWallStart(l), seg.inMs);
                const qint64 streamStart = qMax<qint64>(0, syncStreamOf(l, ws));
                auto *d = new SeqDecoder;
                QString err;
                if (!d->open(l.path, streamStart, &err)) {
                    laneErr[i] = err;
                    d->close();
                    delete d;
                    continue;
                }
                decs[i] = d;
            }
            bool anyOk = false;
            for (auto *d : decs)
                if (d) anyOk = true;
            if (!anyOk) {
                for (auto *d : decs) if (d) { d->close(); delete d; }
                errMsg = QStringLiteral("多通道段 %1：所有机位均无法解码").arg(si + 1);
                break;
            }
            const int cols = (ln <= 2) ? ln : 2;
            const int rows = (ln + cols - 1) / cols;
            const int cellW = videoRect.width() / cols;
            const int cellH = videoRect.height() / rows;
            int calLane = -1;   // 校正时间基准路：首条已校时路
            for (int i = 0; i < ln; ++i)
                if (seg.lanes[i].calibrated) { calLane = i; break; }

            const qint64 segFrames = composeSegOutFrames(seg, p.outFps);
            for (qint64 k = 0; k < segFrames; ++k) {
                if (m_cancelled) { cancelled = true; break; }
                const double wall = seg.inMs + double(k) * seg.rate * 1000.0 / p.outFps;

                QImage canvas(p.canvas, QImage::Format_RGBA8888);
                canvas.fill(Qt::black);
                {
                    QPainter painter(&canvas);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform);
                    // ---- 瓦片宫格 ----
                    for (int i = 0; i < ln; ++i) {
                        const int cx = videoRect.x() + (i % cols) * cellW;
                        const int cy = videoRect.y() + (i / cols) * cellH;
                        const QRect cell(cx + 2, cy + 2, cellW - 4, cellH - 4);
                        const SyncLaneData &l = seg.lanes[i];
                        QImage frame;
                        QString placeholder;
                        if (!laneErr[i].isEmpty()) {
                            placeholder = laneErr[i];
                        } else if (!syncLaneCovers(l, qint64(wall))) {
                            placeholder = QStringLiteral("该时刻无画面");
                        } else {
                            SeqDecoder *d = decs[i];
                            const qint64 stream = syncStreamOf(l, qint64(wall));
                            const qint64 discardBefore = syncStreamOf(
                                l, qMax(syncLaneWallStart(l), seg.inMs));
                            if (d && d->pullTo(stream, discardBefore, &m_cancelled)
                                && !d->cur.isNull() && d->curPtsMs >= stream - 1500.0)
                                frame = d->cur;
                            else
                                placeholder = QStringLiteral("该时刻无画面");
                        }
                        painter.fillRect(cell, QColor(10, 11, 14));
                        if (!frame.isNull()) {
                            QImage scaled = frame.scaled(cell.size(), Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation);
                            painter.drawImage(cell.x() + (cell.width() - scaled.width()) / 2,
                                              cell.y() + (cell.height() - scaled.height()) / 2,
                                              scaled);
                        } else {
                            painter.setPen(QColor(Theme::TextMuted));
                            painter.drawText(cell, Qt::AlignCenter, placeholder);
                        }
                        painter.setPen(Qt::NoPen);
                        painter.setBrush(QColor(0, 0, 0, 140));
                        painter.drawRect(QRect(cell.left(), cell.top(), 130, 22));
                        painter.setPen(QColor(Theme::DataPalette[i % Theme::DataPalette.size()]));
                        painter.drawText(QRect(cell.left() + 6, cell.top(), 240, 22),
                                         Qt::AlignVCenter | Qt::AlignLeft, l.displayName);
                        painter.setPen(QPen(QColor(Theme::Border), 1));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(cell);
                    }
                    // ---- 信息角标（校正时间=基准路墙钟→北京时间）----
                    if (p.burnOsd) {
                        QString timeStr;
                        if (calLane >= 0) {
                            const SyncLaneData &cl = seg.lanes[calLane];
                            const qint64 bj = cl.cal.beijingMsOf(
                                syncStreamOf(cl, qint64(wall)));
                            timeStr = QDateTime::fromMSecsSinceEpoch(bj).toString(
                                          QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                        } else {
                            timeStr = QStringLiteral("未校时 · 墙钟 %1s")
                                          .arg(qint64(wall) / 1000);
                        }
                        QString osd = QStringLiteral("%1x · %2")
                                          .arg(seg.rate, 0, 'g', 3).arg(timeStr);
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
                    if (p.demoWatermark) {
                        const QString wm = QStringLiteral("分析演示材料 · 非原始证据");
                        QFont f = painter.font();
                        f.setPixelSize(20);
                        f.setBold(true);
                        painter.setFont(f);
                        const int wpx = painter.fontMetrics().horizontalAdvance(wm);
                        const QRect wmRect(videoRect.right() - wpx - 24,
                                           videoRect.top() + 8, wpx + 24, 30);
                        painter.setPen(Qt::NoPen);
                        painter.setBrush(QColor(0, 0, 0, 150));
                        painter.drawRoundedRect(wmRect, 4, 4);
                        painter.setPen(QColor(255, 96, 96));
                        painter.drawText(wmRect, Qt::AlignCenter, wm);
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
                while (proc.bytesToWrite() > 256ll * 1024 * 1024 && !m_cancelled)
                    if (!proc.waitForBytesWritten(5000))
                        break;
                ++doneFrames;
                if ((doneFrames & 7) == 0)
                    emit progress(int(doneFrames), int(totalFrames));
            }
            for (auto *d : decs)
                if (d) { d->close(); delete d; }
            continue;   // 下一片段
        }
        AVFormatContext *fmt = nullptr;
        if (avformat_open_input(&fmt, seg.sourcePath.toUtf8().constData(),
                                nullptr, nullptr) < 0) {
            errMsg = QStringLiteral("无法打开源文件：%1").arg(seg.sourcePath);
            break;
        }
        if (avformat_find_stream_info(fmt, nullptr) < 0) {
            avformat_close_input(&fmt);
            errMsg = QStringLiteral("流信息读取失败：%1").arg(seg.sourcePath);
            break;
        }
        int vstream = -1;
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                vstream = int(i);
                break;
            }
        if (vstream < 0) {
            avformat_close_input(&fmt);
            errMsg = QStringLiteral("无视频流：%1").arg(seg.sourcePath);
            break;
        }
        const AVCodec *codec = avcodec_find_decoder(
            fmt->streams[vstream]->codecpar->codec_id);
        AVCodecContext *dec = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!dec || avcodec_parameters_to_context(dec, fmt->streams[vstream]->codecpar) < 0
            || avcodec_open2(dec, codec, nullptr) < 0) {
            if (dec) avcodec_free_context(&dec);
            avformat_close_input(&fmt);
            errMsg = QStringLiteral("解码器打开失败：%1").arg(seg.sourcePath);
            break;
        }
        const AVRational tb = fmt->streams[vstream]->time_base;
        dec->pkt_timebase = tb;
        // start_time 归一（v1.15.3 冻结根因同款：DVR 包时间戳非 0 起算）
        const qint64 startMs = (fmt->start_time != AV_NOPTS_VALUE)
            ? fmt->start_time / 1000 : 0;
        const qint64 seekUs = startMs * 1000LL + seg.inMs * 1000LL;
        if (avformat_seek_file(fmt, -1, INT64_MIN, seekUs, seekUs,
                               AVSEEK_FLAG_BACKWARD) < 0)
            av_seek_frame(fmt, vstream,
                          av_rescale_q(startMs + seg.inMs, AVRational{1, 1000}, tb),
                          AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);

        SwsContext *sws = nullptr;
        AVPacket *pkt = av_packet_alloc();
        AVFrame *fr = av_frame_alloc();
        QImage curFrame;
        double curPtsMs = -1.0;
        bool eof = false;

        auto pullNext = [&]() -> bool {
            while (!eof) {
                if (m_cancelled) return false;
                if (av_read_frame(fmt, pkt) < 0) {
                    av_packet_unref(pkt);
                    avcodec_send_packet(dec, nullptr);
                    eof = true;
                    continue;
                }
                if (pkt->stream_index != vstream) { av_packet_unref(pkt); continue; }
                // 以包时间戳为准（HEVC 帧时间戳不随帧推进机型防御，同 run()）
                const int64_t pktMs = (pkt->dts != AV_NOPTS_VALUE)
                    ? av_rescale_q(pkt->dts, tb, AVRational{1, 1000}) - startMs
                    : (pkt->pts != AV_NOPTS_VALUE
                           ? av_rescale_q(pkt->pts, tb, AVRational{1, 1000}) - startMs
                           : AV_NOPTS_VALUE);
                if (avcodec_send_packet(dec, pkt) < 0) { av_packet_unref(pkt); continue; }
                av_packet_unref(pkt);
                while (avcodec_receive_frame(dec, fr) == 0) {
                    const double ms = (pktMs != AV_NOPTS_VALUE) ? double(pktMs) : -1.0;
                    if (ms < seg.inMs - 1.0) { av_frame_unref(fr); continue; }
                    sws = sws_getCachedContext(sws, fr->width, fr->height,
                                               AVPixelFormat(fr->format),
                                               fr->width, fr->height, AV_PIX_FMT_RGBA,
                                               SWS_BILINEAR, nullptr, nullptr, nullptr);
                    if (!sws) { av_frame_unref(fr); continue; }
                    QImage img(fr->width, fr->height, QImage::Format_RGBA8888);
                    uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
                    int dstStride[4] = {int(img.bytesPerLine()), 0, 0, 0};
                    sws_scale(sws, fr->data, fr->linesize, 0, fr->height, dst, dstStride);
                    curFrame = img;
                    curPtsMs = ms;
                    av_frame_unref(fr);
                    return true;
                }
            }
            return false;
        };

        // 段校正时间（无校正 → 流内时间回落）
        const TimeCalibration cal = p.calibrationByPath.value(seg.sourcePath);

        const qint64 segFrames = composeSegOutFrames(seg, p.outFps);
        for (qint64 k = 0; k < segFrames; ++k) {
            if (m_cancelled) { cancelled = true; break; }
            const double target = seg.inMs + double(k) * seg.rate * 1000.0 / p.outFps;
            while ((curPtsMs < 0.0 || curPtsMs < target - 0.5) && !eof) {
                if (!pullNext())
                    break;
            }
            if (curFrame.isNull()) {
                if (!pullNext() || curFrame.isNull()) {
                    errMsg = QStringLiteral("源解码失败（段 %1 起点后无可用帧）").arg(si + 1);
                    break;
                }
            }

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

                if (p.burnOsd) {
                    QString timeStr;
                    if (cal.isValid()) {
                        const qint64 wall = cal.beijingMsOf(qint64(target));
                        timeStr = QDateTime::fromMSecsSinceEpoch(wall).toString(
                                      QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    } else {
                        const qint64 t = qint64(target);
                        timeStr = QStringLiteral("%1:%2:%3")
                                      .arg(t / 3600000, 2, 10, QLatin1Char('0'))
                                      .arg(t / 60000 % 60, 2, 10, QLatin1Char('0'))
                                      .arg(t / 1000 % 60, 2, 10, QLatin1Char('0'));
                    }
                    QString osd = QStringLiteral("%1x · %2")
                                      .arg(seg.rate, 0, 'g', 3).arg(timeStr);
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
                if (p.demoWatermark) {
                    // 演示片强制角标（右上，红字黑底，不可关——取证自保拍板）
                    const QString wm = QStringLiteral("分析演示材料 · 非原始证据");
                    QFont f = painter.font();
                    f.setPixelSize(20);
                    f.setBold(true);
                    painter.setFont(f);
                    const int wpx = painter.fontMetrics().horizontalAdvance(wm);
                    const QRect wmRect(videoRect.right() - wpx - 24,
                                       videoRect.top() + 8, wpx + 24, 30);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor(0, 0, 0, 150));
                    painter.drawRoundedRect(wmRect.adjusted(-6, -2, 6, 2), 6, 6);
                    painter.setPen(QColor(255, 96, 96));
                    painter.drawText(wmRect, Qt::AlignCenter, wm);
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
            while (proc.bytesToWrite() > 256ll * 1024 * 1024 && !m_cancelled)
                if (!proc.waitForBytesWritten(5000))
                    break;
            ++doneFrames;
            if ((doneFrames & 7) == 0)
                emit progress(int(doneFrames), int(totalFrames));
        }

        if (sws) sws_freeContext(sws);
        av_packet_free(&pkt);
        av_frame_free(&fr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
    }

    proc.closeWriteChannel();
    proc.waitForFinished(-1);
    const bool procOk = (proc.exitStatus() == QProcess::NormalExit
                         && proc.exitCode() == 0);

    if (cancelled || m_cancelled) {
        QFile::remove(p.outputPath);
        emit finished(false, QStringLiteral("已取消"));
        return;
    }
    if (!errMsg.isEmpty()) {
        QFile::remove(p.outputPath);
        fail(errMsg);
        return;
    }
    if (!procOk) {
        QFile::remove(p.outputPath);
        fail(QStringLiteral("ffmpeg 编码失败：")
             + QString::fromLocal8Bit(proc.readAllStandardError()).right(300));
        return;
    }
    emit progress(int(totalFrames), int(totalFrames));
    emit finished(true, p.outputPath + (anyAudio ? QString() : QStringLiteral("（无音轨）")));
}

// ---------------------------------------------------------------------------
// 合成导出 P1：证据模式（无损直拷 + 侧车清单 JSON；像素零改动）
// ---------------------------------------------------------------------------

QJsonObject SegmentExportEngine::buildEvidenceManifest(
    const QString &appVersion, const QString &caseNo,
    const QString &opName, const QString &opOrg,
    const QVector<Params::ComposeSeg> &segs, const QStringList &sourceSha256,
    const QString &outFileName, const QString &outSha256, qint64 outBytes)
{
    QJsonArray arr;
    for (int i = 0; i < segs.size(); ++i) {
        QJsonObject o;
        o.insert(QStringLiteral("source"), segs[i].sourcePath);
        o.insert(QStringLiteral("source_name"), QFileInfo(segs[i].sourcePath).fileName());
        o.insert(QStringLiteral("in_ms"), double(segs[i].inMs));
        o.insert(QStringLiteral("out_ms"), double(segs[i].outMs));
        o.insert(QStringLiteral("source_sha256"),
                 i < sourceSha256.size() ? sourceSha256.at(i) : QString());
        arr.append(o);
    }
    QJsonObject op;
    op.insert(QStringLiteral("name"), opName);
    op.insert(QStringLiteral("org"), opOrg);
    QJsonObject out;
    out.insert(QStringLiteral("file"), outFileName);
    out.insert(QStringLiteral("sha256"), outSha256);
    out.insert(QStringLiteral("bytes"), double(outBytes));
    QJsonObject root;
    root.insert(QStringLiteral("tool"), QStringLiteral("LumenArc"));
    root.insert(QStringLiteral("tool_version"), appVersion);
    root.insert(QStringLiteral("kind"), QStringLiteral("evidence_segment_export"));
    root.insert(QStringLiteral("created_utc"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("case_no"), caseNo);
    root.insert(QStringLiteral("operator"), op);
    root.insert(QStringLiteral("segments"), arr);
    root.insert(QStringLiteral("output"), out);
    root.insert(QStringLiteral("integrity_note"),
                QStringLiteral("无损直拷（-c copy），像素零改动；剪辑点按关键帧对齐，"
                               "实际出入点可能落在最近关键帧。"));
    return root;
}

namespace {
/// 流式 SHA-256（8MB 块，可取消）；失败/取消返回空串
QString sha256OfFile(const QString &path, volatile bool *cancelled)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        if (cancelled && *cancelled)
            return QString();
        h.addData(f.read(8ll * 1024 * 1024));
    }
    return QString::fromLatin1(h.result().toHex());
}
} // namespace

void SegmentExportEngine::runEvidenceCopy()
{
    const Params &p = m_params;
    auto fail = [this](const QString &msg) {
        emit finished(false, QLatin1String(kErrPrefix) + msg);
    };
    const QString ffmpeg = ToolPaths::findFfmpegPath();
    const QFileInfo outInfo(p.outputPath);
    QDir outDir = outInfo.dir();
    const QString tmpDir = outInfo.absolutePath() + QStringLiteral("/.la_evidence_tmp_%1")
        .arg(QDateTime::currentMSecsSinceEpoch());
    if (!outDir.mkpath(tmpDir)) {
        fail(QStringLiteral("临时目录创建失败"));
        return;
    }
    auto cleanup = [&]() {
        QDir d(tmpDir);
        d.removeRecursively();
    };

    // ---- 逐段直拷（关键帧对齐，侧车已声明）----
    QStringList parts;
    for (int i = 0; i < p.segments.size(); ++i) {
        const auto &seg = p.segments.at(i);
        const QString tmp = QStringLiteral("%1/part%2.mp4")
                                .arg(tmpDir).arg(i, 3, 10, QLatin1Char('0'));
        QStringList args;
        args << QStringLiteral("-y")
             << QStringLiteral("-ss") << QString::number(seg.inMs / 1000.0, 'f', 3)
             << QStringLiteral("-to") << QString::number(seg.outMs / 1000.0, 'f', 3)
             << QStringLiteral("-i") << seg.sourcePath
             << QStringLiteral("-map") << QStringLiteral("0")
             << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero")
             << tmp;
        QProcess proc;
        proc.setProgram(ffmpeg);
        proc.setArguments(args);
        proc.start();
        if (!proc.waitForStarted(10000)) {
            cleanup();
            fail(QStringLiteral("ffmpeg 启动失败：") + proc.errorString());
            return;
        }
        while (!proc.waitForFinished(500)) {
            if (m_cancelled) {
                proc.kill();
                proc.waitForFinished(3000);
                cleanup();
                QFile::remove(p.outputPath);
                emit finished(false, QStringLiteral("已取消"));
                return;
            }
        }
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            const QString tail = QString::fromLocal8Bit(proc.readAllStandardError()).right(300);
            cleanup();
            fail(QStringLiteral("直拷失败（段 %1）：%2").arg(i + 1).arg(tail));
            return;
        }
        parts << tmp;
        emit progress(70 * (i + 1) / (p.segments.size() + 1), 100);
    }

    // ---- 多段 concat（同参数流直拷合并）----
    if (parts.size() > 1) {
        const QString listFile = tmpDir + QStringLiteral("/concat.txt");
        QFile lf(listFile);
        if (!lf.open(QIODevice::WriteOnly | QIODevice::Text)) {
            cleanup();
            fail(QStringLiteral("concat 清单写入失败"));
            return;
        }
        for (const QString &pt : parts) {
            QString esc = pt;
            esc.replace(QLatin1Char('\''), QLatin1String("'\\''"));
            lf.write(QStringLiteral("file '%1'\n").arg(esc).toUtf8());
        }
        lf.close();
        QProcess proc;
        proc.setProgram(ffmpeg);
        proc.setArguments({QStringLiteral("-y"), QStringLiteral("-f"),
                           QStringLiteral("concat"), QStringLiteral("-safe"),
                           QStringLiteral("0"), QStringLiteral("-i"), listFile,
                           QStringLiteral("-c"), QStringLiteral("copy"),
                           p.outputPath});
        proc.start();
        while (!proc.waitForFinished(500)) {
            if (m_cancelled) {
                proc.kill();
                proc.waitForFinished(3000);
                cleanup();
                QFile::remove(p.outputPath);
                emit finished(false, QStringLiteral("已取消"));
                return;
            }
        }
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
            const QString tail = QString::fromLocal8Bit(proc.readAllStandardError()).right(300);
            cleanup();
            fail(QStringLiteral("拼接失败：") + tail);
            return;
        }
    } else if (!QFile::rename(parts.first(), p.outputPath)) {
        cleanup();
        fail(QStringLiteral("产物落盘失败（重命名）"));
        return;
    }
    emit progress(75, 100);

    // ---- 完整性哈希（源 + 产物）----
    QStringList srcHashes;
    for (const auto &seg : p.segments) {
        const QString h = sha256OfFile(seg.sourcePath, &m_cancelled);
        if (m_cancelled) {
            cleanup();
            QFile::remove(p.outputPath);
            emit finished(false, QStringLiteral("已取消"));
            return;
        }
        srcHashes << h;
        emit progress(75 + 20 * srcHashes.size() / (p.segments.size() + 1), 100);
    }
    const QString outHash = sha256OfFile(p.outputPath, &m_cancelled);
    emit progress(98, 100);

    // ---- 侧车清单 ----
    const QJsonObject manifest = buildEvidenceManifest(
        QStringLiteral(APP_VERSION), p.caseLabel,
        p.operatorName, p.operatorOrg,
        p.segments, srcHashes, outInfo.fileName(), outHash,
        outInfo.exists() ? outInfo.size() : 0);
    const QString sidecar = p.outputPath + QStringLiteral(".forensic.json");
    QFile sf(sidecar);
    if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        sf.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
        sf.close();
    }
    cleanup();
    emit progress(100, 100);
    emit finished(true, p.outputPath);
}
