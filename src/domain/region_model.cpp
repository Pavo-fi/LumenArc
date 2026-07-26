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

int RegionModel::findIndexByRoiId(int roiId) const
{
    QReadLocker lock(&m_lock);
    return m_roiIds.indexOf(roiId);
}

QColor RegionModel::regionColor(int index)
{
    static const QList<QColor> colors = {
        QColor(255, 0, 0),     // 赤
        QColor(255, 127, 0),   // 橙
        QColor(255, 255, 0),   // 黄
        QColor(0, 255, 0),     // 绿
        QColor(0, 255, 255),   // 青
        QColor(0, 0, 255),     // 蓝
        QColor(139, 0, 255)    // 紫
    };
    return colors[index % colors.size()];
}
