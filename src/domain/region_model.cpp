/**
 * @file region_model.cpp
 * @brief ROI 区域模型实现，线程安全增删改查
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "region_model.h"

RegionModel::RegionModel(QObject *parent)
    : QObject(parent)
{
}

void RegionModel::addRegion(const QRect &rect)
{
    QWriteLocker lock(&m_lock);
    m_regions.append(rect);
    m_roiIds.append(m_nextRoiId++);
    lock.unlock();
    emit regionsChanged();
}

void RegionModel::removeRegion(int index)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_regions.size())
        return;
    int roiId = m_roiIds[index];
    m_regions.removeAt(index);
    m_roiIds.removeAt(index);
    lock.unlock();
    emit regionRemoved(index, roiId);
    emit regionsChanged();
}

void RegionModel::updateRegion(int index, const QRect &rect)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_regions.size())
        return;
    m_regions[index] = rect;
    lock.unlock();
    emit regionsChanged();
}

void RegionModel::clearRegions()
{
    QWriteLocker lock(&m_lock);
    m_regions.clear();
    m_roiIds.clear();
    m_nextRoiId = 1;
    lock.unlock();
    emit regionsChanged();
}

QVector<QRect> RegionModel::regions() const
{
    QReadLocker lock(&m_lock);
    return m_regions;
}

int RegionModel::regionCount() const
{
    QReadLocker lock(&m_lock);
    return m_regions.size();
}

int RegionModel::roiIdAt(int index) const
{
    QReadLocker lock(&m_lock);
    if (index < 0 || index >= m_roiIds.size())
        return -1;
    return m_roiIds[index];
}

QVector<int> RegionModel::roiIds() const
{
    QReadLocker lock(&m_lock);
    return m_roiIds;
}

void RegionModel::restoreRegions(const QVector<QRect> &regions, const QVector<int> &roiIds)
{
    QWriteLocker lock(&m_lock);
    m_regions = regions;
    m_roiIds = roiIds;
    // 下一个 ID 取最大值+1，保证后续新增区域不与恢复 ID 冲突
    m_nextRoiId = 1;
    for (int id : m_roiIds)
        m_nextRoiId = qMax(m_nextRoiId, id + 1);
    lock.unlock();
    emit regionsChanged();
}

int RegionModel::findIndexByRoiId(int roiId) const
{
    QReadLocker lock(&m_lock);
    return m_roiIds.indexOf(roiId);
}

QColor RegionModel::regionColor(int index)
{
    // Okabe-Ito 色盲友好调色板（与图表数据线/标签共用一套视觉语言）
    static const QList<QColor> colors = {
        QColor(213, 94, 0),     // 朱红 vermillion
        QColor(230, 159, 0),    // 橙 orange
        QColor(240, 228, 66),   // 黄 yellow
        QColor(0, 158, 115),    // 青绿 bluish green
        QColor(86, 180, 233),   // 天蓝 sky blue
        QColor(0, 114, 178),    // 蓝 blue
        QColor(204, 121, 167)   // 紫红 reddish purple
    };
    return colors[index % colors.size()];
}
