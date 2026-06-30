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
    lock.unlock();
    emit polygonsChanged();
}

void PolygonModel::removePolygon(int index)
{
    QWriteLocker lock(&m_lock);
    if (index < 0 || index >= m_polygons.size())
        return;
    m_polygons.removeAt(index);
    lock.unlock();
    emit polygonRemoved(index);
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
