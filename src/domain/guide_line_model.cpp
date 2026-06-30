/**
 * @file guide_line_model.cpp
 * @brief 辅助线模型实现
 * @date 2026-06-24
 * @version 0.5
 */
#include "guide_line_model.h"

GuideLineModel::GuideLineModel(QObject *parent)
    : QObject(parent)
{
}

void GuideLineModel::addLine(const GuideLine &line)
{
    QWriteLocker lock(&m_lock);
    m_lines.append(line);
    lock.unlock();
    emit linesChanged();
}

void GuideLineModel::removeLine(int index)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_lines.size())
        return;
    m_lines.removeAt(index);
    lock.unlock();
    emit lineRemoved(index);
    emit linesChanged();
}

void GuideLineModel::updateLine(int index, const GuideLine &line)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_lines.size())
        return;
    m_lines[index] = line;
    lock.unlock();
    emit linesChanged();
}

void GuideLineModel::clearLines()
{
    QWriteLocker lock(&m_lock);
    m_lines.clear();
    lock.unlock();
    emit linesChanged();
}

QVector<GuideLine> GuideLineModel::lines() const
{
    QReadLocker lock(&m_lock);
    return m_lines;
}

int GuideLineModel::lineCount() const
{
    QReadLocker lock(&m_lock);
    return m_lines.size();
}
