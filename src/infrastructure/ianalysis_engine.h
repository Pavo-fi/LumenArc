/**
 * @file ianalysis_engine.h
 * @brief 离线亮度分析引擎抽象接口
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <QObject>
#include <QVector>
#include <QRect>
#include "domain/analysis_snapshot.h"

/**
 * @brief Abstract interface for offline luminance analysis engines.
 */
class IAnalysisEngine : public QObject
{
    Q_OBJECT

public:
    explicit IAnalysisEngine(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IAnalysisEngine() = default;

    virtual void startAnalysis(const QString &videoPath, const QVector<QRect> &regions) = 0;
    virtual void cancelAnalysis() = 0;
    virtual bool isRunning() const = 0;

signals:
    void progressUpdated(int analyzed, int total, qreal percent);
    void analysisFinished(const AnalysisSnapshot &result);
    void analysisFailed(const QString &error);
};
