/**
 * @file region_model.h
 * @brief 线程安全 ROI 区域管理，7色调色板循环
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QObject>
#include <QVector>
#include <QRect>
#include <QReadWriteLock>
#include <QColor>

/**
 * @brief Thread-safe model for managing rectangular ROIs.
 *
 * Replaces the region-management half of the old AnalyzerCore.
 */
class RegionModel : public QObject
{
    Q_OBJECT

public:
    explicit RegionModel(QObject *parent = nullptr);

    /// @brief 添加新的 ROI 区域
    void addRegion(const QRect &rect);
    /// @brief 移除指定索引的区域
    void removeRegion(int index);
    /// @brief 更新指定索引区域的矩形
    void updateRegion(int index, const QRect &rect);
    /// @brief 清空所有区域
    void clearRegions();

    /// @brief 返回当前所有区域的副本
    QVector<QRect> regions() const;
    /// @brief 返回区域数量
    int regionCount() const;

    /// @brief 按索引返回调色板颜色（7色循环）
    static QColor regionColor(int index);

signals:
    void regionsChanged();

private:
    mutable QReadWriteLock m_lock;
    QVector<QRect> m_regions;
};
