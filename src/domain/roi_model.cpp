/**
 * @file roi_model.cpp
 * @brief 统一 ROI 模型实现（矩形 + 多边形），线程安全增删改查
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-15
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "roi_model.h"

RoiModel::RoiModel(QObject *parent)
    : QObject(parent)
{
}

int RoiModel::allocRoiIdLocked()
{
    return m_nextRoiId++;
}

// ==================== 矩形 ROI ====================

void RoiModel::addRegion(const QRect &rect)
{
    QWriteLocker lock(&m_lock);
    m_rects.append(rect);
    m_rectRoiIds.append(allocRoiIdLocked());
    lock.unlock();
    emit regionsChanged();
}

void RoiModel::removeRegion(int index)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_rects.size())
        return;
    int roiId = m_rectRoiIds[index];
    m_rects.removeAt(index);
    m_rectRoiIds.removeAt(index);
    lock.unlock();
    emit regionRemoved(index, roiId);
    emit regionsChanged();
}

void RoiModel::updateRegion(int index, const QRect &rect)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_rects.size())
        return;
    m_rects[index] = rect;
    lock.unlock();
    emit regionsChanged();
}

void RoiModel::clearRegions()
{
    QWriteLocker lock(&m_lock);
    m_rects.clear();
    m_rectRoiIds.clear();
    // 注意：不重置 m_nextRoiId——统一序列需与多边形共存（行为冻结下的
    // 唯一语义变化：清矩形后新增矩形 id 从当前序列继续，不再回到 1）
    lock.unlock();
    emit regionsChanged();
}

QVector<QRect> RoiModel::regions() const
{
    QReadLocker lock(&m_lock);
    return m_rects;
}

int RoiModel::regionCount() const
{
    QReadLocker lock(&m_lock);
    return m_rects.size();
}

int RoiModel::roiIdAt(int index) const
{
    QReadLocker lock(&m_lock);
    if (index < 0 || index >= m_rectRoiIds.size())
        return -1;
    return m_rectRoiIds[index];
}

QVector<int> RoiModel::roiIds() const
{
    QReadLocker lock(&m_lock);
    return m_rectRoiIds;
}

void RoiModel::restoreRegions(const QVector<QRect> &regions, const QVector<int> &roiIds)
{
    QWriteLocker lock(&m_lock);
    m_rects = regions;
    m_rectRoiIds = roiIds;
    // 下一个 ID 取【两张表】最大值+1，保证后续新增不与恢复 ID 冲突
    m_nextRoiId = 1;
    for (int id : m_rectRoiIds)
        m_nextRoiId = qMax(m_nextRoiId, id + 1);
    for (int id : m_polyRoiIds)
        m_nextRoiId = qMax(m_nextRoiId, id + 1);
    lock.unlock();
    emit regionsChanged();
}

int RoiModel::findIndexByRoiId(int roiId) const
{
    QReadLocker lock(&m_lock);
    return m_rectRoiIds.indexOf(roiId);
}

QColor RoiModel::regionColor(int index)
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

// ==================== 多边形 ROI ====================

void RoiModel::addPolygon(const QPolygon &polygon)
{
    QWriteLocker lock(&m_lock);
    m_polys.append(polygon);
    m_polyRoiIds.append(allocRoiIdLocked());
    lock.unlock();
    emit polygonsChanged();
}

void RoiModel::removePolygon(int index)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_polys.size())
        return;
    int roiId = m_polyRoiIds[index];
    m_polys.removeAt(index);
    m_polyRoiIds.removeAt(index);
    lock.unlock();
    emit polygonRemoved(index, roiId);
    emit polygonsChanged();
}

void RoiModel::updatePolygon(int index, const QPolygon &polygon)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_polys.size())
        return;
    m_polys[index] = polygon;
    lock.unlock();
    emit polygonsChanged();
}

void RoiModel::clearPolygons()
{
    QWriteLocker lock(&m_lock);
    m_polys.clear();
    m_polyRoiIds.clear();
    // 同 clearRegions：不重置 m_nextRoiId（统一序列）
    lock.unlock();
    emit polygonsChanged();
}

QVector<QPolygon> RoiModel::polygons() const
{
    QReadLocker lock(&m_lock);
    return m_polys;
}

int RoiModel::polygonCount() const
{
    QReadLocker lock(&m_lock);
    return m_polys.size();
}

int RoiModel::polygonRoiIdAt(int index) const
{
    QReadLocker lock(&m_lock);
    if (index < 0 || index >= m_polyRoiIds.size())
        return -1;
    return m_polyRoiIds[index];
}

QVector<int> RoiModel::polygonRoiIds() const
{
    QReadLocker lock(&m_lock);
    return m_polyRoiIds;
}

void RoiModel::restorePolygons(const QVector<QPolygon> &polygons, const QVector<int> &roiIds)
{
    QWriteLocker lock(&m_lock);
    m_polys = polygons;
    m_polyRoiIds = roiIds;
    // 下一个 ID 取【两张表】最大值+1，保证后续新增多边形不与恢复 ID 冲突
    m_nextRoiId = 1;
    for (int id : m_rectRoiIds)
        m_nextRoiId = qMax(m_nextRoiId, id + 1);
    for (int id : m_polyRoiIds)
        m_nextRoiId = qMax(m_nextRoiId, id + 1);
    lock.unlock();
    emit polygonsChanged();
}

int RoiModel::findPolygonIndexByRoiId(int roiId) const
{
    QReadLocker lock(&m_lock);
    return m_polyRoiIds.indexOf(roiId);
}

QColor RoiModel::polygonColor(int index)
{
    static const QList<QColor> colors = {
        QColor(255, 100, 100),   // 浅红 (Light red)
        QColor(255, 180, 80),    // 杏黄 (Apricot)
        QColor(255, 220, 100),   // 金色 (Gold)
        QColor(100, 220, 130),   // 薄荷绿 (Mint)
        QColor(100, 200, 255),   // 天蓝 (Sky blue)
        QColor(160, 130, 220),   // 薰衣草 (Lavender)
        QColor(220, 130, 180)    // 玫瑰 (Rose)
    };
    return colors[index % colors.size()];
}
