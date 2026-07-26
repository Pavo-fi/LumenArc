/**
 * @file guide_line_model.h
 * @brief 辅助线模型：线程安全的辅助线管理
 * @date 2026-06-24
 * @version 0.5
 */
#pragma once

#include <QObject>
#include <QVector>
#include <QReadWriteLock>
#include "guide_line.h"

/**
 * @brief 线程安全的辅助线模型
 */
class GuideLineModel : public QObject
{
    Q_OBJECT

public:
    explicit GuideLineModel(QObject *parent = nullptr);

    void addLine(const GuideLine &line);
    void removeLine(int index);
    void updateLine(int index, const GuideLine &line);
    void clearLines();

    QVector<GuideLine> lines() const;
    int lineCount() const;

signals:
    void linesChanged();
    void lineRemoved(int index);

private:
    mutable QReadWriteLock m_lock;
    QVector<GuideLine> m_lines;
};
