/**
 * @file polygon_model.cpp
 * @brief 多边形ROI模型实现
 * @date 2026-06-24
 * @version 0.5
 */
#include "polygon_model.h"

PolygonModel::PolygonModel(QObject *parent)
    : QObject(parent)
{
}

void PolygonModel::addPolygon(const QPolygon &polygon)
{
    QWriteLocker lock(&m_lock);
    m_polygons.append(polygon);
    m_roiIds.append(m_nextRoiId++);
    lock.unlock();
    emit polygonsChanged();
}

void PolygonModel::removePolygon(int index)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_polygons.size())
        return;
    int roiId = m_roiIds[index];
    m_polygons.removeAt(index);
    m_roiIds.removeAt(index);
    lock.unlock();
    emit polygonRemoved(index, roiId);
    emit polygonsChanged();
}

void PolygonModel::updatePolygon(int index, const QPolygon &polygon)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_polygons.size())
        return;
    m_polygons[index] = polygon;
    lock.unlock();
    emit polygonsChanged();
}

void PolygonModel::clearPolygons()
{
    QWriteLocker lock(&m_lock);
    m_polygons.clear();
    m_roiIds.clear();
    m_nextRoiId = 1;
    lock.unlock();
    emit polygonsChanged();
}

QVector<QPolygon> PolygonModel::polygons() const
{
    QReadLocker lock(&m_lock);
    return m_polygons;
}

int PolygonModel::polygonCount() const
{
    QReadLocker lock(&m_lock);
    return m_polygons.size();
}

int PolygonModel::roiIdAt(int index) const
{
    QReadLocker lock(&m_lock);
    if (index < 0 || index >= m_roiIds.size())
        return -1;
    return m_roiIds[index];
}

int PolygonModel::findIndexByRoiId(int roiId) const
{
    QReadLocker lock(&m_lock);
    return m_roiIds.indexOf(roiId);
}

QColor PolygonModel::polygonColor(int index)
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
