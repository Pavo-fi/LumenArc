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

// 规范§8：数据系列颜色只从 DataPalette 顺序取色（第 7 条起循环）。
// 域层不可 include theme.h（lumenarc_roi_model_test 仅链 Core+Gui），
// 此板镜像 Theme::DataPalette 六色同序（改动必须两边同步）；矩形/多边形统一同板（规范§14）。
/// 与白色混合 pct%（提亮），用于把 6 个基色扩成 12 个互不相同的档位。
static QColor lighten(const QColor &c, int pct)
{
    return QColor(c.red()   + (255 - c.red())   * pct / 100,
                  c.green() + (255 - c.green()) * pct / 100,
                  c.blue()  + (255 - c.blue())  * pct / 100);
}

static const QList<QColor> &dataPalette()
{
    // v2.5t：12 色档位，**12 个区域之后才循环**（用户 2026-11-05：
    // 颜色不要太快重复）。
    //   前 6 个 = Okabe-Ito 基础色（青绿 bluish green 已摘出，专职给「音量」曲线，
    //   否则区域 3 与音量撞色）；
    //   后 6 个 = 同色相提亮 45% 的变体，与同色号基础色相距 6 个槽位，
    //   相邻区域永远不同色，且不引入任何新的色相。
    static const QList<QColor> colors = [] {
        const QList<QColor> base = {
            QColor(86, 180, 233),   // 天蓝  sky blue
            QColor(230, 159, 0),    // 橙    orange
            QColor(213, 94, 0),     // 朱红  vermillion
            QColor(204, 121, 167),  // 紫红  reddish purple
            QColor(240, 228, 66),   // 黄    yellow
            QColor(0, 114, 178),    // 深蓝  blue
        };
        QList<QColor> out = base;
        for (const QColor &c : base)
            out.append(lighten(c, 45));
        return out;
    }();
    return colors;
}

QColor RoiModel::regionColor(int index)
{
    return dataPalette()[index % dataPalette().size()];
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
    // 规范§8：与矩形同一 DataPalette 顺序取色，偏移 +1 避免与同号矩形撞色，六色循环
    return dataPalette()[(index + 1) % dataPalette().size()];
}
