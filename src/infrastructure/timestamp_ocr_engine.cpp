/**
 * @file timestamp_ocr_engine.cpp
 * @brief OSD 时间戳 OCR 引擎实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "timestamp_ocr_engine.h"

// 取证日志门控（2026-09-28）：校时排查用的 calib_debug.log 只在显式开启时写，
// 默认**不往程序目录写文件**（避免用户机器上留调试产物）。
// 需要排查时设环境变量 LUMENARC_CALIB_DEBUG=1。
static bool calibDebugEnabled()
{
    static const bool on =
        qEnvironmentVariableIsSet("LUMENARC_CALIB_DEBUG");
    return on;
}
#include "tool_paths.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>

// OCR 是用户等待的前处理任务：恢复正常优先级（BELOW_NORMAL 在后台负载下
// 会被饿死）。防主窗口饿殍改由线程数限制承担（OMP_NUM_THREADS=1，见 run()）

namespace {

qint64 jsonWallStart(const QJsonObject &first)
{
    // impliedStartMs = 流内 rel 0 的墙钟（排序语义，design §5.3.3）；
    // 老协议无此字段时退化为 wallMs - relMs
    if (first.contains(QStringLiteral("impliedStartMs")))
        return static_cast<qint64>(first[QStringLiteral("impliedStartMs")].toDouble());
    return static_cast<qint64>(first[QStringLiteral("wallMs")].toDouble())
        - static_cast<qint64>(first[QStringLiteral("relMs")].toDouble());
}

} // namespace

TimestampOcrEngine::TimestampOcrEngine(QObject *parent)
    : QObject(parent)
{
}

TimestampOcrEngine::~TimestampOcrEngine()
{
    cancel();
}

void TimestampOcrEngine::setPythonExecutable(const QString &path)
{
    m_pythonPath = path;
    m_availability = -1;
}

QString TimestampOcrEngine::pythonExecutable() const
{
    return !m_pythonPath.isEmpty() ? m_pythonPath
                                   : ToolPaths::detectPythonPath();
}

bool TimestampOcrEngine::available(QString *errorDetail)
{
    if (m_availability >= 0) {
        if (errorDetail)
            *errorDetail = m_availError;
        return m_availability == 1;
    }
    m_availability = 0;
    const QString py = pythonExecutable();
    if (py.isEmpty() || !QFile::exists(py)) {
        m_availError = QStringLiteral("python not found");
        if (errorDetail)
            *errorDetail = m_availError;
        return false;
    }
    const QString script = QCoreApplication::applicationDirPath()
        + QStringLiteral("/probe_timestamps.py");
    if (!QFile::exists(script)) {
        m_availError = QStringLiteral("probe_timestamps.py not found");
        if (errorDetail)
            *errorDetail = m_availError;
        return false;
    }
    // rapidocr 导入检测（一次性，缓存）
    QProcess proc;
    proc.setProgram(py);
    proc.setArguments({QStringLiteral("-c"),
                       QStringLiteral("import rapidocr_onnxruntime, cv2, numpy")});
    proc.start();
    if (!proc.waitForFinished(15000) || proc.exitCode() != 0) {
        m_availError = QStringLiteral("rapidocr_onnxruntime not installed");
        if (errorDetail)
            *errorDetail = m_availError;
        return false;
    }
    m_availability = 1;
    if (errorDetail)
        *errorDetail = QString();
    return true;
}

void TimestampOcrEngine::run(const QStringList &paths, const QString &workDir,
                             const QMap<QString, qint64> &trustedDurationsMs,
                             const QString &evidenceDir, bool withSha256,
                             const QStringList &framesOnlyFiles,
                             const QMap<QString, QRectF> &rois)
{
    if (isRunning())
        return;
    QString err;
    if (!available(&err)) {
        failAll(PreprocessError::OcrEngineMissing, err);
        return;
    }
    const QString ffmpeg = ToolPaths::findFfmpegPath();
    if (!QFile::exists(ffmpeg) && ffmpeg == QLatin1String("ffmpeg")) {
        // PATH 兜底可用与否交给脚本报错（C2 不静默）
    }
    QDir().mkpath(workDir);

    // 可信时长表 → JSON（键=normpath 后路径，与脚本一致）
    QJsonObject durObj;
    for (auto it = trustedDurationsMs.begin(); it != trustedDurationsMs.end(); ++it) {
        if (it.value() > 0)
            durObj.insert(QDir::toNativeSeparators(it.key()),
                          static_cast<double>(it.value()));
    }
    const QString durPath = workDir + QStringLiteral("/durations.json");
    {
        QFile f(durPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            failAll(PreprocessError::FileUnreadable,
                    QStringLiteral("cannot write %1").arg(durPath));
            return;
        }
        f.write(QJsonDocument(durObj).toJson(QJsonDocument::Compact));
    }

    // 仅截帧名单 → JSON（键 framesOnly，与脚本约定一致）
    QString framesOnlyPath;
    if (!framesOnlyFiles.isEmpty()) {
        QJsonArray arr;
        for (const QString &p : framesOnlyFiles)
            arr.append(QDir::toNativeSeparators(p));
        QJsonObject fo;
        fo.insert(QStringLiteral("framesOnly"), arr);
        framesOnlyPath = workDir + QStringLiteral("/frames_only.json");
        QFile f(framesOnlyPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(QJsonDocument(fo).toJson(QJsonDocument::Compact));
        else
            framesOnlyPath.clear();   // 写失败则退化为全量 OCR（不静默）
    }

    // P-60 每文件归一化 ROI → JSON（键=normpath 后路径，与脚本一致；
    // 写失败退化为全帧链路，不静默）
    QString roiPath;
    if (!rois.isEmpty()) {
        QJsonObject roiObj;
        for (auto it = rois.begin(); it != rois.end(); ++it) {
            const QRectF &r = it.value();
            if (!r.isValid())
                continue;
            roiObj.insert(QDir::toNativeSeparators(it.key()),
                          QJsonArray{r.x(), r.y(),
                                     r.x() + r.width(), r.y() + r.height()});
        }
        roiPath = workDir + QStringLiteral("/rois.json");
        QFile f(roiPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(QJsonDocument(roiObj).toJson(QJsonDocument::Compact));
        else
            roiPath.clear();
    }

    const QString script = QCoreApplication::applicationDirPath()
        + QStringLiteral("/probe_timestamps.py");
    QStringList args{QStringLiteral("-X"), QStringLiteral("utf8"),
                     script,
                     QStringLiteral("--ffmpeg-path"), ffmpeg,
                     QStringLiteral("--work-dir"), workDir,
                     QStringLiteral("--workers"), QStringLiteral("4"),
                     QStringLiteral("--duration-json"), durPath};
    if (!evidenceDir.isEmpty())
        args << QStringLiteral("--evidence-dir") << evidenceDir;
    if (withSha256)
        args << QStringLiteral("--with-sha256");
    if (!framesOnlyPath.isEmpty())
        args << QStringLiteral("--frames-only-json") << framesOnlyPath;
    if (!roiPath.isEmpty())
        args << QStringLiteral("--roi-json") << roiPath;
    args << paths;

    m_total = paths.size();
    m_cancelled = false;
    m_stdoutBuf.clear();
    m_stderrBuf.clear();

    m_process = new QProcess(this);
    m_process->setProgram(pythonExecutable());
    m_process->setArguments(args);

    // 线程过订阅防护（现场反馈③：OCR 期间主窗口分析被饿殍）：
    // 子进程数学库单线程（多进程×单线程），进程级低于普通优先级
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("OPENBLAS_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("MKL_NUM_THREADS"), QStringLiteral("1"));
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuf += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError,
            this, &TimestampOcrEngine::onReadyReadStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onFinished(code); });

    // 看门狗：每文件 60s 上限 + 5min 基线（规范 C4 必须有超时）
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        if (m_process) {
            m_process->kill();
            failAll(PreprocessError::Timeout, QStringLiteral("ocr watchdog timeout"));
        }
    });
    m_watchdog->start(300000 + m_total * 60000);

    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        failAll(PreprocessError::OcrEngineMissing,
                QStringLiteral("failed to start python"));
    }
}

void TimestampOcrEngine::runTickScan(const QString &videoPath,
                                     const QString &anchorsJson,
                                     const QString &tickOutJson,
                                     const QString &roiJson,
                                     const QString &workDir,
                                     qint64 trustedDurationMs)
{
    if (isRunning() || videoPath.isEmpty())
        return;
    QString err;
    if (!available(&err)) {
        emit tickScanFinished(videoPath, false, err);
        return;
    }
    const QString ffmpeg = ToolPaths::findFfmpegPath();
    QString ffprobe = ToolPaths::findFfprobePath();
    if (ffprobe.isEmpty() || !QFile::exists(ffprobe)) {
        // 同目录推 ffprobe（打包同仓）
        ffprobe = QFileInfo(ffmpeg).absoluteDir().absoluteFilePath(
            QStringLiteral("ffprobe.exe"));
        if (!QFile::exists(ffprobe))
            ffprobe = QFileInfo(ffmpeg).absoluteDir().absoluteFilePath(
                QStringLiteral("ffprobe"));
    }
    const QString script = QCoreApplication::applicationDirPath()
        + QStringLiteral("/probe_timestamps.py");
    QDir().mkpath(workDir);
    const QString durPath = workDir + QStringLiteral("/tick_durations.json");
    {
        QJsonObject o;
        o.insert(QDir::toNativeSeparators(videoPath),
                 static_cast<double>(qMax<qint64>(0, trustedDurationMs)));
        QFile f(durPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    }
    QStringList args{QStringLiteral("-X"), QStringLiteral("utf8"), script,
                     QStringLiteral("--tickscan-out"), tickOutJson,
                     QStringLiteral("--ffmpeg-path"), ffmpeg,
                     QStringLiteral("--ffprobe-path"), ffprobe,
                     QStringLiteral("--work-dir"), workDir,
                     QStringLiteral("--duration-json"), durPath};
    if (!anchorsJson.isEmpty())
        args << QStringLiteral("--tickscan-anchors") << anchorsJson;
    if (!roiJson.isEmpty())
        args << QStringLiteral("--roi-json") << roiJson;
    args << videoPath;

    m_total = 1;
    m_cancelled = false;
    m_tickMode = true;
    m_tickVideo = videoPath;
    m_tickOut = tickOutJson;
    m_stdoutBuf.clear();
    m_stderrBuf.clear();

    m_process = new QProcess(this);
    m_process->setProgram(pythonExecutable());
    m_process->setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("OPENBLAS_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("MKL_NUM_THREADS"), QStringLiteral("1"));
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuf += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError,
            this, &TimestampOcrEngine::onReadyReadStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onFinished(code); });
    // 看门狗（C4）：秒级跳变要解码全片（实测 45 分钟片 ~75 秒），按 3× 时长+C4 基线
    if (!m_watchdog) {
        m_watchdog = new QTimer(this);
        m_watchdog->setSingleShot(true);
        connect(m_watchdog, &QTimer::timeout, this, [this]() {
            if (m_process) {
                m_process->kill();
                if (m_tickMode) {
                    m_tickMode = false;
                    emit tickScanFinished(m_tickVideo, false,
                                          QStringLiteral("tickscan timeout"));
                }
            }
        });
    }
    const qint64 wd = 300000 + qMax<qint64>(600000, trustedDurationMs * 3);
    m_watchdog->start(static_cast<int>(qMin<qint64>(wd, 7200000)));
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        // P1-2（reviewer）：必须复位模式位，否则此后所有 at 模式运行都被误路由到
        // tick 分支（三点结果被吞；残留旧 map 还会被当成新结果应用）
        m_tickMode = false;
        m_tickVideo.clear();
        m_tickOut.clear();
        m_process->deleteLater();
        m_process = nullptr;
        emit tickScanFinished(videoPath, false,
                              QStringLiteral("failed to start python"));
    }
}

void TimestampOcrEngine::runCalibPhoto(const QString &imagePath,
                                       const QRect &monitorBox,
                                       const QRect &beijingBox)
{
    if (isRunning() || imagePath.isEmpty())
        return;
    if (!monitorBox.isValid() || !beijingBox.isValid()) {
        emit calibPhotoFinished(false, {}, {},
                                QStringLiteral("invalid roi"));
        return;
    }

    const QString script = QCoreApplication::applicationDirPath()
        + QStringLiteral("/probe_timestamps.py");
    if (!QFile::exists(script)) {
        emit calibPhotoFinished(false, {}, {},
                                QStringLiteral("probe_timestamps.py not found"));
        return;
    }
    const auto rectStr = [](const QRect &r) {
        return QStringLiteral("%1,%2,%3,%4")
            .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    };
    QStringList args{QStringLiteral("-X"), QStringLiteral("utf8"),
                     script, QStringLiteral("calibphoto"),
                     imagePath, rectStr(monitorBox), rectStr(beijingBox)};

    m_total = 0;
    m_cancelled = false;
    m_calibPhotoMode = true;
    m_stdoutBuf.clear();
    m_stderrBuf.clear();

    m_process = new QProcess(this);
    m_process->setProgram(pythonExecutable());
    m_process->setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("OPENBLAS_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("MKL_NUM_THREADS"), QStringLiteral("1"));
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuf += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError,
            this, &TimestampOcrEngine::onReadyReadStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onFinished(code); });
    // 照片识别（模型懒加载 + 两框双变体）最坏约 1~2 分钟，看门狗 3 分钟
    if (!m_watchdog) {
        m_watchdog = new QTimer(this);
        m_watchdog->setSingleShot(true);
        connect(m_watchdog, &QTimer::timeout, this, [this]() {
            if (m_process) {
                m_process->kill();
                if (m_calibPhotoMode) {
                    m_calibPhotoMode = false;
                    emit calibPhotoFinished(false, {}, {},
                                            QStringLiteral("calibphoto timeout"));
                }
            }
        });
    }
    m_watchdog->start(180000);
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        m_calibPhotoMode = false;
        QProcess *proc = m_process;
        m_process = nullptr;
        if (proc)
            proc->deleteLater();
        emit calibPhotoFinished(false, {}, {},
                                QStringLiteral("python start failed"));
    }
}

void TimestampOcrEngine::runAtPositions(const QString &path,
                                        const QVector<qint64> &positionsMs,
                                        qint64 trustedDurationMs,
                                        const QString &evidenceDir,
                                        const QRectF &roi)
{
    if (isRunning() || path.isEmpty())
        return;
    // 空位置表绝不能静默返回：调用方（重建状态机）在等待完成信号，
    // 静默会永久挂起（v1.2.1 复盘：analyzeCoarse 空 jobs → 死锁最后一环）
    if (positionsMs.isEmpty()) {
        emit atPositionsFailed(QStringLiteral("no positions to sample"));
        return;
    }
    QString err;
    if (!available(&err)) {
        emit atPositionsFailed(err);
        return;
    }
    const QString ffmpeg = ToolPaths::findFfmpegPath();
    const QString workDir = QDir::temp().absoluteFilePath(
        QStringLiteral("lumenarc_at_%1").arg(
            QDateTime::currentMSecsSinceEpoch()));
    QDir().mkpath(workDir);

    // 可信时长（单文件）→ durations.json（与批模式同协议）
    QJsonObject durObj;
    if (trustedDurationMs > 0)
        durObj.insert(QDir::toNativeSeparators(path),
                      static_cast<double>(trustedDurationMs));
    const QString durPath = workDir + QStringLiteral("/durations.json");
    {
        QFile f(durPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            emit atPositionsFailed(QStringLiteral("cannot write %1").arg(durPath));
            return;
        }
        f.write(QJsonDocument(durObj).toJson(QJsonDocument::Compact));
    }

    // 取样位置 → at.json
    QJsonArray posArr;
    for (qint64 p : positionsMs)
        posArr.append(static_cast<double>(p));
    QJsonObject atObj;
    atObj.insert(QDir::toNativeSeparators(path), posArr);
    const QString atPath = workDir + QStringLiteral("/at.json");
    {
        QFile f(atPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            emit atPositionsFailed(QStringLiteral("cannot write %1").arg(atPath));
            return;
        }
        f.write(QJsonDocument(atObj).toJson(QJsonDocument::Compact));
    }

    // 用户框选的时间戳区域（归一化 0~1）→ roi.json
    QString roiPath;
    if (roi.isValid()) {
        QJsonObject roiObj;
        QJsonArray arr{roi.x(), roi.y(), roi.x() + roi.width(),
                       roi.y() + roi.height()};
        roiObj.insert(QDir::toNativeSeparators(path), arr);
        roiPath = workDir + QStringLiteral("/roi.json");
        QFile f(roiPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(QJsonDocument(roiObj).toJson(QJsonDocument::Compact));
        else
            roiPath.clear();
    }

    const QString script = QCoreApplication::applicationDirPath()
        + QStringLiteral("/probe_timestamps.py");
    QStringList args{QStringLiteral("-X"), QStringLiteral("utf8"),
                     script,
                     QStringLiteral("--ffmpeg-path"), ffmpeg,
                     QStringLiteral("--work-dir"), workDir,
                     QStringLiteral("--duration-json"), durPath,
                     QStringLiteral("--at-json"), atPath};
    if (!roiPath.isEmpty())
        args << QStringLiteral("--roi-json") << roiPath;
    if (!evidenceDir.isEmpty())
        args << QStringLiteral("--evidence-dir") << evidenceDir;
    args << path;

    m_total = positionsMs.size();
    m_cancelled = false;
    m_atMode = true;
    m_stdoutBuf.clear();
    m_stderrBuf.clear();

    m_process = new QProcess(this);
    m_process->setProgram(pythonExecutable());
    m_process->setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("OPENBLAS_NUM_THREADS"), QStringLiteral("1"));
    env.insert(QStringLiteral("MKL_NUM_THREADS"), QStringLiteral("1"));
    m_process->setProcessEnvironment(env);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuf += m_process->readAllStandardOutput();
    });
    connect(m_process, &QProcess::readyReadStandardError,
            this, &TimestampOcrEngine::onReadyReadStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onFinished(code); });

    // 看门狗：每位置 90s + 5min 基线（取样含尾部 seek，可慢）
    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, [this]() {
        if (m_process) {
            m_process->kill();
            m_atMode = false;
            emit atPositionsFailed(QStringLiteral("ocr watchdog timeout"));
        }
    });
    m_watchdog->start(300000 + m_total * 90000);

    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        m_atMode = false;
        emit atPositionsFailed(QStringLiteral("failed to start python"));
    }
}

void TimestampOcrEngine::cancel()
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
}

bool TimestampOcrEngine::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void TimestampOcrEngine::onReadyReadStderr()
{
    m_stderrBuf += m_process->readAllStandardError();
    int nl;
    while ((nl = m_stderrBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_stderrBuf.left(nl).trimmed();
        m_stderrBuf.remove(0, nl + 1);
        if (line.startsWith("PROGRESS:")) {
            const QList<QByteArray> p = line.mid(9).split('|');
            if (p.size() >= 2)
                emit ocrProgress(p[0].toInt(), p[1].toInt(), QString());
        } else if (line.startsWith("ERROR:")) {
            const QByteArray rest = line.mid(6);
            const int sep = rest.lastIndexOf(':');
            if (sep > 0)
                emit ocrFailed(QString::fromUtf8(rest.left(sep)),
                               QString::fromUtf8(rest.mid(sep + 1)));
        }
        // WARNING: 忽略（脚本诊断输出）
    }
}

void TimestampOcrEngine::onFinished(int exitCode)
{
    if (m_watchdog)
        m_watchdog->stop();
    const bool cancelled = m_cancelled;
    QProcess *proc = m_process;
    m_process = nullptr;
    if (proc)
        proc->deleteLater();
    if (cancelled)
        return;     // 取消路径由 Coordinator 状态机接管（C1 类型化）

    // ---- v1.12.5 校时照片模式分流：stdout 中 CALIBPHOTO: 单行 JSON ----
    if (m_calibPhotoMode) {
        m_calibPhotoMode = false;
        const QString out = QString::fromUtf8(m_stdoutBuf);
        const int tag = out.lastIndexOf(QStringLiteral("CALIBPHOTO:"));
        if (tag < 0) {
            emit calibPhotoFinished(false, {}, {},
                QStringLiteral("calibphoto no output (exit %1)").arg(exitCode));
            return;
        }
        const QString line = out.mid(tag + 11).split(QLatin1Char('\n')).first().trimmed();
        QJsonParseError jerr{};
        const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &jerr);
        if (!doc.isObject()) {
            emit calibPhotoFinished(false, {}, {},
                QStringLiteral("calibphoto bad json: %1").arg(jerr.errorString()));
            return;
        }
        const QJsonObject root = doc.object();
        if (!root[QStringLiteral("ok")].toBool()) {
            emit calibPhotoFinished(false, {}, {},
                root[QStringLiteral("error")].toString(
                    QStringLiteral("calibphoto failed")));
            return;
        }
        const auto parseLines = [](const QJsonArray &arr) {
            QVector<QPair<QString, double>> lines;
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                lines.append({o[QStringLiteral("text")].toString(),
                              o[QStringLiteral("score")].toDouble()});
            }
            return lines;
        };
        emit calibPhotoFinished(
            true,
            parseLines(root[QStringLiteral("box1")].toArray()),
            parseLines(root[QStringLiteral("box2")].toArray()),
            QString());
        return;
    }

    // P-98：秒级跳变模式分流（probe 把映射表写文件，此处只判成败）
    if (m_tickMode) {
        m_tickMode = false;
        const QString v = m_tickVideo;
        const QString out = m_tickOut;
        m_tickVideo.clear();
        m_tickOut.clear();
        // P2-3（reviewer）：读回 JSON 的 ok/error 字段为准，别用体积启发式
        // （短路径的失败响应 <64B 会被误判成 exit 0）
        bool ok = false;
        QString err;
        QFile mf(out);
        if (exitCode == 0 && mf.exists() && mf.open(QIODevice::ReadOnly)) {
            const QJsonObject root =
                QJsonDocument::fromJson(mf.readAll()).object();
            // 单文件 tickscan：取第一个 entry
            for (auto it = root.begin(); it != root.end(); ++it) {
                const QJsonObject ent = it.value().toObject();
                ok = ent.value(QStringLiteral("ok")).toBool();
                if (!ok)
                    err = ent.value(QStringLiteral("error")).toString();
                break;
            }
        }
        if (!ok && err.isEmpty()) {
            err = QStringLiteral("tickscan exit %1").arg(exitCode);
            const QString tail = QString::fromUtf8(m_stderrBuf.right(400));
            if (!tail.trimmed().isEmpty())
                err += QStringLiteral(" :: ") + tail.trimmed();
        }
        emit tickScanFinished(v, ok, err);
        return;
    }

    if (exitCode != 0 && m_stdoutBuf.trimmed().isEmpty()) {
        if (m_atMode) {
            m_atMode = false;
            emit atPositionsFailed(
                QStringLiteral("probe_timestamps.py exit %1").arg(exitCode));
            return;
        }
        failAll(PreprocessError::OcrAllFailed,
                QStringLiteral("probe_timestamps.py exit %1").arg(exitCode));
        return;
    }
    QJsonParseError jerr{};
    const QJsonDocument doc = QJsonDocument::fromJson(m_stdoutBuf.trimmed(), &jerr);
    if (!doc.isArray()) {
        if (m_atMode) {
            m_atMode = false;
            emit atPositionsFailed(QStringLiteral("bad json: %1").arg(jerr.errorString()));
            return;
        }
        failAll(PreprocessError::OcrAllFailed,
                QStringLiteral("bad json: %1").arg(jerr.errorString()));
        return;
    }

    // ---- 校时取样模式解析 ----
    if (m_atMode) {
        m_atMode = false;
        // TEMP-DEBUG（2026-09-26，校时「算了不落库」现场排查）——把 probe 的
        // 原始 stdout/stderr 落到程序目录，供机外分析；定位后删除本块。
        {
            QFile dbg(QCoreApplication::applicationDirPath()
                      + QStringLiteral("/calib_debug.log"));
            if (calibDebugEnabled()
                && dbg.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                dbg.write(QStringLiteral("\n===== [%1] atPositions exit=%2 =====\nSTDOUT:\n%3\nSTDERR(tail):\n%4\n")
                              .arg(QDateTime::currentDateTime().toString(
                                       QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                              .arg(exitCode)
                              .arg(QString::fromUtf8(m_stdoutBuf))
                              .arg(QString::fromUtf8(m_stderrBuf.right(4000)))
                              .toUtf8());
            }
        }
        QVector<TimeCalibration::Sample> samples;
        const QJsonArray arr = doc.array();
        for (const QJsonValue &fv : arr) {
            const QJsonArray sarr = fv.toObject()[QStringLiteral("samples")].toArray();
            for (const QJsonValue &sv : sarr) {
                const QJsonObject s = sv.toObject();
                const qint64 wall = static_cast<qint64>(
                    s[QStringLiteral("wallMs")].toDouble());
                if (wall <= 0)
                    continue;   // 该位置识别失败：跳过（拟合用成功测点）
                TimeCalibration::Sample smp;
                smp.streamMs = static_cast<qint64>(
                    s[QStringLiteral("relMs")].toDouble());
                smp.wallMs = wall;
                smp.rawText = s[QStringLiteral("text")].toString();
                smp.conf = s[QStringLiteral("conf")].toDouble();
                smp.frameImgPath = QDir::fromNativeSeparators(
                    s[QStringLiteral("frameImg")].toString());
                smp.used = true;
                samples.append(smp);
            }
        }
        if (samples.isEmpty()) {
            emit atPositionsFailed(QStringLiteral("ocr_all_failed"));
            return;
        }
        {
            QFile dbg(QCoreApplication::applicationDirPath()
                      + QStringLiteral("/calib_debug.log"));
            if (calibDebugEnabled()
                && dbg.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
                QString s;
                for (const auto &x : samples)
                    s += QStringLiteral("  [engine] streamMs=%1 wall=%2 conf=%3 text=%4\n")
                             .arg(x.streamMs).arg(x.wallMs).arg(x.conf).arg(x.rawText);
                dbg.write((QStringLiteral("PARSED SAMPLES (%1):\n").arg(samples.size()) + s)
                              .toUtf8());
            }
        }
        emit atPositionsFinished(samples);
        return;
    }

    QVector<OcrResult> results;
    const QJsonArray arr = doc.array();
    results.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        OcrResult r;
        r.filePath = QDir::fromNativeSeparators(
            o[QStringLiteral("file")].toString());
        r.durationMs = static_cast<qint64>(
            o[QStringLiteral("durationMs")].toDouble());
        r.sha256 = o[QStringLiteral("sha256")].toString();
        const QJsonObject first = o[QStringLiteral("first")].toObject();
        if (!first.isEmpty()) {
            r.wallStartMs = jsonWallStart(first);
            r.rawStartText = first[QStringLiteral("text")].toString();
            r.conf = first[QStringLiteral("conf")].toDouble();
            r.source = OcrResult::Ocr;
            r.firstFrameImg = first[QStringLiteral("frameImg")].toString();
            r.startCropImg = first[QStringLiteral("cropImg")].toString();
            r.startFrameRelMs = static_cast<qint64>(
                first[QStringLiteral("relMs")].toDouble());
            // P-60 命中行位置回报（ROI 自学习）
            const QJsonArray rn = first[QStringLiteral("roiNorm")].toArray();
            if (rn.size() == 4)
                r.hitRoi = QRectF(rn[0].toDouble(), rn[1].toDouble(),
                                  rn[2].toDouble() - rn[0].toDouble(),
                                  rn[3].toDouble() - rn[1].toDouble());
        }
        const QJsonObject last = o[QStringLiteral("last")].toObject();
        if (!last.isEmpty()) {
            r.wallEndMs = static_cast<qint64>(
                last[QStringLiteral("wallMs")].toDouble());
            r.rawEndText = last[QStringLiteral("text")].toString();
            r.lastFrameImg = last[QStringLiteral("frameImg")].toString();
            r.endCropImg = last[QStringLiteral("cropImg")].toString();
            r.endFrameRelMs = static_cast<qint64>(
                last[QStringLiteral("relMs")].toDouble());
        }
        if (!o[QStringLiteral("ok")].toBool())
            r.ocrError = o[QStringLiteral("error")].toString(
                QStringLiteral("ocr_all_failed"));
        results.append(r);
    }
    emit ocrFinished(results);
}

void TimestampOcrEngine::failAll(PreprocessError error, const QString &detail)
{
    if (m_watchdog)
        m_watchdog->stop();
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    emit engineError(error, detail);
}
