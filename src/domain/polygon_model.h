/**
 * @file polygon_model.h
 * @brief 多边形ROI模型：线程安全的多边形区域管理
 * @date 2026-06-24
 * @version 0.5
 */
#pragma once

#include <QObject>
#include <QVector>
#include <QPolygon>
#include <QReadWriteLock>
#include <QColor>

/**
 * @brief 线程安全的多边形ROI模型
 */
class PolygonModel : public QObject
{
    Q_OBJECT

public:
    explicit PolygonModel(QObject *parent = nullptr);

    void addPolygon(const QPolygon &polygon);
    void removePolygon(int index);
    void updatePolygon(int index, const QPolygon &polygon);
    void clearPolygons();

    QVector<QPolygon> polygons() const;
    int polygonCount() const;
    int roiIdAt(int index) const;
    int findIndexByRoiId(int roiId) const;

    static QColor polygonColor(int index);

signals:
    void polygonsChanged();
    void polygonRemoved(int index, int roiId);

private:
    mutable QReadWriteLock m_lock;
    QVector<QPolygon> m_polygons;
    QVector<int> m_roiIds;
    int m_nextRoiId = 1;
};
