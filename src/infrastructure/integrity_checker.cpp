#include "infrastructure/integrity_checker.h"

#include <QProcess>
#include <QFileInfo>

namespace {
const auto NAL_ERR_A = QByteArrayLiteral("Invalid NAL unit size");
const auto NAL_ERR_B = QByteArrayLiteral("Error splitting the input into NAL units");
} // namespace

NalIntegrityChecker::NalIntegrityChecker(QObject *parent)
    : QObject(parent)
{
}

NalIntegrityChecker::~NalIntegrityChecker()
{
    if (m_proc) {
        m_cancelled = true;
        m_proc->kill();
        m_proc->deleteLater();
    }
}

void NalIntegrityChecker::check(const QStringList &files, const QString &ffmpegPath)
{
    m_queue = files;
    m_results.clear();
    m_ffmpeg = ffmpegPath;
    m_idx = 0;
    m_cancelled = false;
    startNext();
}

void NalIntegrityChecker::cancel()
{
    m_cancelled = true;
    if (m_proc)
        m_proc->kill();
}

void NalIntegrityChecker::startNext()
{
    if (m_cancelled)
        return;
    if (m_idx >= m_queue.size()) {
        emit finished(m_results);
        return;
    }
    if (m_ffmpeg.isEmpty() || !QFileInfo::exists(m_ffmpeg)) {
        emit failed(QStringLiteral("ffmpeg 不可用：%1").arg(m_ffmpeg));
        return;
    }
    m_lastStderr.clear();
    m_proc = new QProcess(this);
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &NalIntegrityChecker::onProcFinished);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this]() {
        m_lastStderr += m_proc->readAllStandardError();
    });
    // -c copy: 不重编码；-bsf h264_mp4toannexb: 强制解析 NAL 长度（快检核心）
    m_proc->start(m_ffmpeg,
                  {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"),
                   QStringLiteral("-i"), m_queue[m_idx],
                   QStringLiteral("-map"), QStringLiteral("0:v:0"),
                   QStringLiteral("-c"), QStringLiteral("copy"),
                   QStringLiteral("-bsf:v"), QStringLiteral("h264_mp4toannexb"),
                   QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    emit fileChecked(m_queue[m_idx], m_idx, m_queue.size());
}

void NalIntegrityChecker::onProcFinished(int)
{
    QProcess *proc = m_proc;
    m_proc = nullptr;
    if (proc)
        proc->deleteLater();
    if (m_cancelled)
        return;
    qint64 errs = 0;
    int pos = 0;
    while ((pos = m_lastStderr.indexOf(NAL_ERR_A, pos)) >= 0) {
        ++errs;
        pos += NAL_ERR_A.size();
    }
    pos = 0;
    while ((pos = m_lastStderr.indexOf(NAL_ERR_B, pos)) >= 0) {
        ++errs;
        pos += NAL_ERR_B.size();
    }
    m_results.append({m_queue[m_idx], errs});
    ++m_idx;
    startNext();
}