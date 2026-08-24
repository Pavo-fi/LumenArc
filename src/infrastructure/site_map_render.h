/**
 * @file site_map_render.h
 * @brief P-74 监控点位图：共用绘制（编辑器画布与成品图同函数，所见即所得）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include "domain/site_map.h"
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPainter>

namespace sitemaprender {

/// 点位层：在 baseRect（底图显示矩形，像素）内绘制全部点位
/// selected：选中点位下标（-1=无，编辑器态金框）；selectedLane 不适用此版本
void drawPoints(QPainter &p, const SiteMapData &d, const QRectF &baseRect,
                const QHash<QString, QColor> &laneColor, int selected = -1);

/// 标准图框成品图：2480×1754（A4 横向 150dpi）白底 + 双图框 + 右下标题栏
/// （案件编号/图名/制图/审核/日期/图号）+ 底图适配嵌入 + 点位层
QImage renderFramed(const SiteMapData &d, const QImage &base,
                    const QHash<QString, QColor> &laneColor,
                    const QString &caseNo, const QString &drawer,
                    const QString &reviewer, const QString &dateText);

} // namespace sitemaprender
