/**
 * @file sitemapeditordialog.h
 * @brief P-74 监控点位图编辑器：底图导入/拖放布点/扇面调节/图框出图
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 方案：docs/SITEMAP_EDITOR_DESIGN_CN.md（拍板：不画比例尺/扇形扇面可调/
 * 一案一张）。画布与成品图共用 sitemaprender::drawPoints（所见即所得）。
 */

#pragma once

#include <QDialog>
#include <QImage>
#include <QColor>
#include <QHash>
#include <QStringList>
#include <QVector>

#include "domain/site_map.h"

/// 物理机位分组（P-74 返修 2026-08-24：机位编号认不出/重复机位重复显示）——
/// 同机位标签的 V###/P###（原件+各次拼接产物）聚为一个布点单位。
struct CamGroup {
    QString key;                 ///< 组键=机位标签（无标签退化为 id）
    QStringList memberIds;
    QStringList memberFiles;     ///< 成员文件名（识别提示）
};

class CaseManager;
class QListWidget;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class SiteMapCanvas;

class SiteMapEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SiteMapEditorDialog(CaseManager *cm, QWidget *parent = nullptr);

private:
    void importBase();
    void exportFramed();
    void syncLaneColors();
    void markOrphans();
    void saveData();
    void rebuildGroups();        ///< 机位分组重建（侧栏/色板/标签表）
    void renameGroup();          ///< 机位改名（组内全部成员同改）

    CaseManager *m_cm;
    SiteMapData m_data;
    QImage m_base;
    SiteMapCanvas *m_canvas = nullptr;
    QListWidget *m_laneList = nullptr;
    QLabel *m_hint = nullptr;
    QDoubleSpinBox *m_heading = nullptr;
    QDoubleSpinBox *m_spread = nullptr;
    QDoubleSpinBox *m_radius = nullptr;
    QDoubleSpinBox *m_fontScale = nullptr;   // 标注字号 %（v1.15.3）
    QPushButton *m_delBtn = nullptr;
    QHash<QString, QColor> m_laneColor;
    QVector<CamGroup> m_groups;
};
