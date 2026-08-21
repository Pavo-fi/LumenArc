/**
 * @file project_io.cpp
 * @brief 工程读写服务实现（P-31 T1；函数体自 MainWindow 纯移动，行为冻结）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "project_io.h"
#include "case_manager.h"
#include "i18n.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QSettings>
#include <QTextStream>
#include <QtConcurrent>

ProjectIO::ProjectIO(CaseManager *cases, TimelineModel *model, QObject *parent)
    : QObject(parent), m_cases(cases), m_model(model)
{
}

TimelineModel *ProjectIO::model()
{
    if (!m_model)
        m_model = new TimelineModel(this);   // 无模型注入：仅取字段的临时装载
    return m_model;
}

QString ProjectIO::suggestSavePath(const QString &currentVideoPath) const
{
    // v1.3.0 路径分流：入案视频默认存案件 videos/V###.vla（仍可另选路径）；
    // 直接加载 .vla 的场景默认覆写原文件（v1.2.2 行为保持）
    QString defaultPath = currentVideoPath;
    if (defaultPath.isEmpty())
        defaultPath = QStringLiteral("analysis_result.vla");
    else if (!defaultPath.endsWith(".vla", Qt::CaseInsensitive))
        defaultPath = m_cases ? m_cases->vlaPathFor(defaultPath) : defaultPath + ".vla";
    return defaultPath;
}

bool ProjectIO::saveVlaNow(const QString &path, const VlaSaveRequest &req)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    return model()->saveToFile(path, req.regions, req.calibration,
                               req.magnifierRect, req.labels, req.pinnedRect,
                               req.fusion, req.polygons, req.guideLines,
                               req.regionRoiIds, req.polygonRoiIds,
                               req.abRegion, req.speedPlan);
}

void ProjectIO::saveVlaAsync(const QString &path, const VlaSaveRequest &req)
{
    // 全部参数为值拷贝（各 model 的 getter 返回副本），后台线程安全
    TimelineModel *m = model();
    {
        QMutexLocker lock(&m_saveMutex);
        m_savePendingPath = path;
        m_savePendingReq = req;      // 覆盖式登记：只留最新请求（合并写）
        m_savePending = true;
        if (m_saveRunning)
            return;                  // 在途保存收尾时会拾取最新请求
        m_saveRunning = true;
    }
    QtConcurrent::run([this, m]() { runSaveLoop(m); });
}

void ProjectIO::runSaveLoop(TimelineModel *m)
{
    for (;;) {
        QString path;
        VlaSaveRequest req;
        {
            QMutexLocker lock(&m_saveMutex);
            if (!m_savePending) {
                m_saveRunning = false;
                return;
            }
            path = m_savePendingPath;
            req = m_savePendingReq;
            m_savePending = false;
        }
        QDir().mkpath(QFileInfo(path).absolutePath());
        const bool ok = m->saveToFile(path, req.regions, req.calibration,
                                      req.magnifierRect, req.labels,
                                      req.pinnedRect, req.fusion, req.polygons,
                                      req.guideLines, req.regionRoiIds,
                                      req.polygonRoiIds,
                                      req.abRegion, req.speedPlan);
        emit vlaSaved(path, ok);   // 工作线程发射 → 槽侧自动队列回 UI 线程
    }
}

bool ProjectIO::loadVla(const QString &path, LoadedVla *out)
{
    if (!out)
        return false;
    return model()->loadFromFile(path, &out->regions, &out->calibration,
                                 &out->magnifierRect, &out->labels, &out->pinnedRect,
                                 &out->fusion, &out->polygons, &out->guideLines,
                                 &out->regionRoiIds, &out->polygonRoiIds,
                                 &out->abRegion, &out->speedPlan);
}

QString ProjectIO::calibrationBadgeSummary(const TimeCalibration &cal)
{
    if (!cal.isEffective())
        return QString();
    QString src;
    switch (cal.source) {
    case TimeCalibration::Source::Manual:    src = lang("手动", "manual"); break;
    case TimeCalibration::Source::Ocr:       src = QStringLiteral("OCR"); break;
    case TimeCalibration::Source::AbsStart:  src = QStringLiteral("absStart"); break;
    case TimeCalibration::Source::Inherited: src = lang("继承", "inherited"); break;
    default: break;
    }
    // 例："OCR 3点, rate=1.000"；分段重建模式标注 piecewise
    QString s = lang("%1 %2点, rate=%3", "%1 %2pts, rate=%3")
        .arg(src).arg(cal.samples.size())
        .arg(cal.effectiveRate(), 0, 'f', 3);
    if (cal.piecewiseMode())
        s += lang("（分段重建）", " (piecewise)");
    return s;
}

bool ProjectIO::exportLabelsCsv(const QString &path,
                                const QVector<ChartLabel> &labels,
                                const TimeCalibration &calibration)
{
    if (labels.isEmpty())
        return false;
    QFile labelsFile(path);
    if (!labelsFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    // RFC 4180: quote fields containing comma, quote or newline（F6）
    auto csvField = [](const QString &s) -> QString {
        if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"'))
            || s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r'))) {
            QString t = s;
            t.replace(QStringLiteral("\""), QStringLiteral("\"\""));
            return QStringLiteral("\"") + t + QStringLiteral("\"");
        }
        return s;
    };
    QTextStream out(&labelsFile);
    out << "Time(ms),Time,Text,Color\n";
    for (const auto &label : labels) {
        QString timeStr;
        if (calibration.dateKnown) {
            timeStr = QDateTime::fromMSecsSinceEpoch(
                          calibration.beijingMsOf(label.timeMs))
                          .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        } else {
            const qint64 ms = qMax<qint64>(0, label.timeMs + calibration.offsetMs);
            const int totalSec = static_cast<int>(ms / 1000);
            timeStr = QStringLiteral("%1:%2:%3")
                          .arg(totalSec / 3600, 2, 10, QChar('0'))
                          .arg((totalSec % 3600) / 60, 2, 10, QChar('0'))
                          .arg(totalSec % 60, 2, 10, QChar('0'));
        }
        out << label.timeMs << ","
            << timeStr << ","
            << csvField(label.text) << ","
            << label.color.name(QColor::HexArgb) << "\n";
    }
    labelsFile.close();
    return true;
}

// ---------------------------------------------------------------------------
// 时间戳区域持久化（v1.2.1：按视频路径 hash，同一摄像头复用；v1.3.0 随案分流）
// ---------------------------------------------------------------------------
QRectF ProjectIO::readTimestampRoiRegistry(const QString &videoPath)
{
    QSettings s("LumenArc", "LumenArc");
    const QByteArray key = "calibration/roi_"
        + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
    return s.value(QString::fromLatin1(key)).toRectF();
}

QRectF ProjectIO::savedTimestampRoi(const QString &videoPath) const
{
    if (videoPath.isEmpty())
        return QRectF();
    // v1.3.0 框选记忆随案（M2 任务9）：入案视频读写 case.json
    if (m_cases && m_cases->isCaseVideo(videoPath)) {
        QRectF roi = m_cases->timestampRoiFor(videoPath);
        if (roi.isValid())
            return roi;
        // 迁移：注册表旧值只读复制一次入案（注册表原值保留一版，拍板§8-12）
        roi = readTimestampRoiRegistry(videoPath);
        if (roi.isValid())
            m_cases->setTimestampRoi(videoPath, roi);
        return roi;
    }
    // 独立模式照旧 QSettings
    return readTimestampRoiRegistry(videoPath);
}

void ProjectIO::saveTimestampRoi(const QString &videoPath, const QRectF &norm)
{
    if (videoPath.isEmpty() || !norm.isValid())
        return;
    // v1.3.0 入案视频框选记忆写 case.json（独立模式照旧 QSettings）
    if (m_cases && m_cases->isCaseVideo(videoPath)) {
        m_cases->setTimestampRoi(videoPath, norm);
        return;
    }
    QSettings s("LumenArc", "LumenArc");
    const QByteArray key = "calibration/roi_"
        + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
    s.setValue(QString::fromLatin1(key), norm);
}
