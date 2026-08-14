/**
 * @file roi_model.h
 * @brief 统一 ROI 模型（矩形 + 多边形），v1.5.0 首批提交（Q-18 拍板）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-15
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 合并 RegionModel + PolygonModel（技术债：双模型复制 + roiId 跨模型冲突）。
 * 行为冻结纯内部重构：
 *  - 矩形 API 保留原 RegionModel 签名（addRegion/roiIdAt/roiIds/...）
 *  - 多边形 API 保留原 PolygonModel 签名，方法名加 polygon 前缀
 *    （polygonRoiIdAt/polygonRoiIds/findPolygonIndexByRoiId，避免同名冲突）
 *  - 信号不变：regionsChanged/regionRemoved/polygonsChanged/polygonRemoved
 *  - roiId 改为【统一序列】：矩形与多边形共享一个递增器，不再各自从 1 起
 *    （消除跨模型 roiId 冲突；DataEntry 本就有 type 区分，无歧义）
 */
#pragma once

#include <QObject>
#include <QVector>
#include <QRect>
#include <QPolygon>
#include <QReadWriteLock>
#include <QColor>

/**
 * @brief Thread-safe unified ROI model: rectangles + polygons.
 *
 * Replaces RegionModel and PolygonModel (one instance now serves both).
 */
class RoiModel : public QObject
{
    Q_OBJECT

public:
    explicit RoiModel(QObject *parent = nullptr);

    // ==================== 矩形 ROI（原 RegionModel API 原名） ====================

    /// @brief 添加新的 ROI 区域
    void addRegion(const QRect &rect);
    /// @brief 移除指定索引的区域
    void removeRegion(int index);
    /// @brief 更新指定索引区域的矩形
    void updateRegion(int index, const QRect &rect);
    /// @brief 清空所有矩形区域（不动多边形；roiId 序列不重置——统一序列）
    void clearRegions();

    /// @brief 返回当前所有矩形区域的副本
    QVector<QRect> regions() const;
    /// @brief 返回矩形区域数量
    int regionCount() const;
    /// @brief 返回指定索引的 ROI ID（矩形表内）
    int roiIdAt(int index) const;
    /// @brief 返回当前所有矩形 ROI ID 的副本（与 regions() 顺序一致）
    QVector<int> roiIds() const;
    /// @brief 带 ROI ID 整体恢复（切换视频/加载 .vla 时保持 roiId 与分析数据对齐）
    void restoreRegions(const QVector<QRect> &regions, const QVector<int> &roiIds);
    /// @brief 在矩形表内查找指定 ROI ID 的索引，未找到返回 -1
    int findIndexByRoiId(int roiId) const;

    /// @brief 按索引返回调色板颜色（7色循环，Okabe-Ito）
    static QColor regionColor(int index);

    // ==================== 多边形 ROI（原 PolygonModel API，加 polygon 前缀） ====================

    void addPolygon(const QPolygon &polygon);
    void removePolygon(int index);
    void updatePolygon(int index, const QPolygon &polygon);
    /// @brief 清空所有多边形区域（不动矩形；roiId 序列不重置——统一序列）
    void clearPolygons();

    QVector<QPolygon> polygons() const;
    int polygonCount() const;
    /// @brief 返回指定索引的 ROI ID（多边形表内）
    int polygonRoiIdAt(int index) const;
    /// @brief 返回当前所有多边形 ROI ID 的副本（与 polygons() 顺序一致）
    QVector<int> polygonRoiIds() const;
    /// @brief 带 ROI ID 整体恢复（切换视频/加载 .vla 时保持 roiId 与分析数据对齐）
    void restorePolygons(const QVector<QPolygon> &polygons, const QVector<int> &roiIds);
    /// @brief 在多边形表内查找指定 ROI ID 的索引，未找到返回 -1
    int findPolygonIndexByRoiId(int roiId) const;

    /// @brief 按索引返回调色板颜色（7色循环）
    static QColor polygonColor(int index);

signals:
    void regionsChanged();
    /// Emitted after a specific region is removed, with its former index and ROI ID.
    void regionRemoved(int index, int roiId);
    void polygonsChanged();
    void polygonRemoved(int index, int roiId);

private:
    /// 统一序列下取下一个 ROI ID（矩形/多边形共享，消除跨模型冲突）。
    /// 内部加锁调用：调用方必须已持有写锁。
    int allocRoiIdLocked();

    mutable QReadWriteLock m_lock;
    QVector<QRect> m_rects;
    QVector<QPolygon> m_polys;
    QVector<int> m_rectRoiIds;
    QVector<int> m_polyRoiIds;
    int m_nextRoiId = 1;
};
