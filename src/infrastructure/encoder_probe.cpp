/**
 * @file encoder_probe.cpp
 * @brief 编码器探测与选择实现（v1.7.0 M1）
 */
#include "encoder_probe.h"
#include "tool_paths.h"

#include <QProcess>
#include <QMutex>
#include <QFile>

namespace encoder_probe {

static QStringList g_cached;
static bool g_cachedValid = false;
static QMutex g_mutex;

QStringList availableEncoders()
{
    QMutexLocker lock(&g_mutex);
    if (g_cachedValid)
        return g_cached;

    const QString ffmpeg = ToolPaths::findFfmpegPath();
    QProcess proc;
    proc.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    QStringList found;
    if (proc.waitForStarted(5000) && proc.waitForFinished(15000)) {
        const QByteArray out = proc.readAllStandardOutput() + proc.readAllStandardError();
        for (const QByteArray &line : out.split('\n')) {
            const QByteArray t = line.trimmed();
            // 行格式：" V....D h264_nvenc           NVIDIA ... (codec h264)"
            // 编码器名 = 第 8 列起第一个 token（不能用 lastIndexOf：
            // 行尾是 (codec h264) 会取错）
            for (const QByteArray &name : {QByteArray("h264_nvenc"),
                                           QByteArray("h264_qsv"),
                                           QByteArray("libx264")}) {
                if (t.contains(name)) {
                    if (!found.contains(QString::fromLatin1(name)))
                        found << QString::fromLatin1(name);
                    break;
                }
            }
        }
    }

    // 试编码验证：NVIDIA 驱动 API 版本不可用时 NVENC 存在但不可用
    // （历史实发 HANDOVER 6.6.5）——逐项实测，失败剔除
    QStringList usable;
    for (const QString &enc : found) {
        if (probeEncoderWorks(enc))
            usable << enc;
    }
    if (usable.isEmpty())
        usable << QStringLiteral("libx264");   // 软编永远可用（兜底语义）

    g_cached = usable;
    g_cachedValid = true;
    return g_cached;
}

bool probeEncoderWorks(const QString &encoderName)
{
    const QString ffmpeg = ToolPaths::findFfmpegPath();
    QProcess proc;
    // 1 帧 testsrc → null：验证编码器可初始化和完成一帧编码。
    // 注意尺寸 ≥320x240：NVENC H.264 最小分辨率 145x65，64x64 会
    // 报 Frame Dimension 错误（实测 RTX 5080）——探测参数须覆盖硬编下限。
    proc.start(ffmpeg, {
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("testsrc2=size=320x240:rate=1:duration=1"),
        QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-c:v"), encoderName,
        QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-"),
    });
    if (!proc.waitForStarted(5000))
        return false;
    if (!proc.waitForFinished(20000))
        return false;
    if (proc.exitCode() != 0)
        return false;
    // 部分硬编失败表现为成功退出但 stderr 报错（NVENC 尺寸/驱动问题）——
    // loglevel error 下正常编码 stderr 应为空，出现 Error 即判不可用
    const QByteArray err = proc.readAllStandardError();
    return !err.contains("Error");
}

QString selectBestEncoder()
{
    const QStringList usable = availableEncoders();
    for (const QString &pref : {QStringLiteral("h264_nvenc"),
                                QStringLiteral("h264_qsv"),
                                QStringLiteral("libx264")}) {
        if (usable.contains(pref))
            return pref;
    }
    return usable.isEmpty() ? QStringLiteral("libx264") : usable.first();
}

EncoderArgs encoderArgsFor(const QString &encoderName)
{
    EncoderArgs a;
    a.encoder = encoderName.isEmpty() ? QStringLiteral("libx264") : encoderName;
    if (a.encoder == QStringLiteral("h264_nvenc")) {
        // 等价性评审校准值（M1-4 实测后回填；CRF18 基准 → CQ 19 起档）
        a.qualityFlag = QStringLiteral("-cq");
        a.qualityValue = 19;
        a.presetFlag = QStringLiteral("-preset");
        a.presetValue = QStringLiteral("p4");   // 平衡档 ≈ veryfast 语义
    } else if (a.encoder == QStringLiteral("h264_qsv")) {
        a.qualityFlag = QStringLiteral("-global_quality");
        a.qualityValue = 18;
        a.presetFlag = QStringLiteral("-preset");
        a.presetValue = QStringLiteral("veryfast");
    } else {
        a.qualityFlag = QStringLiteral("-crf");
        a.qualityValue = 18;
        a.presetFlag = QStringLiteral("-preset");
        a.presetValue = QStringLiteral("veryfast");
    }
    return a;
}

}  // namespace encoder_probe
