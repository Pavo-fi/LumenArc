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
#include "python_analysis_engine.h"

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
                                   : PythonAnalysisEngine::detectPythonPath();
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
                             const QStringList &framesOnlyFiles)
{
    if (isRunning())
        return;
    QString err;
    if (!available(&err)) {
        failAll(PreprocessError::OcrEngineMissing, err);
        return;
    }
    const QString ffmpeg = PythonAnalysisEngine::findFfmpegPath();
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

void TimestampOcrEngine::runAtPositions(const QString &path,
                                        const QVector<qint64> &positionsMs,
                                        qint64 trustedDurationMs,
                                        const QString &evidenceDir,
                                        const QRectF &roi)
{
    if (isRunning() || path.isEmpty() || positionsMs.isEmpty())
        return;
    QString err;
    if (!available(&err)) {
        emit atPositionsFailed(err);
        return;
    }
    const QString ffmpeg = PythonAnalysisEngine::findFfmpegPath();
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
