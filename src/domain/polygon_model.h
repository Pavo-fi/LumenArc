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
    /// @brief 返回当前所有 ROI ID 的副本（与 polygons() 顺序一致）
    QVector<int> roiIds() const;
    /// @brief 带 ROI ID 整体恢复（切换视频/加载 .vla 时保持 roiId 与分析数据对齐）
    void restorePolygons(const QVector<QPolygon> &polygons, const QVector<int> &roiIds);
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
