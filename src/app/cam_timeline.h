/**
 * @file cam_timeline.h
 * @brief 多机时间线对齐数据装配（app 层，v1.3.0 M3 任务14）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §3-M3 任务14。
 * 逐已校时视频读案内 .vla（校时 SSOT，R5/R6：禁止各处自行解析 case.json
 * 之外的重复真源——校时只信 .vla）→ wallMsOf(0)/wallMsOf(maxStream) 得
 * 墙钟块位；视图层（multicamview）只消费本结构，不碰 IO。
 */
#pragma once

#include <QString>
#include <QVector>
#include <QHash>
#include <QSet>
#include <algorithm>
#include "domain/case_model.h"
#include "domain/sync_model.h"

class CaseManager;

/// 单路机位时间线条目（墙钟块位 [wallStartMs, wallEndMs]）
struct CamLane {
    QString videoId;
    QString fileName;
    qint64 wallStartMs = 0;       ///< 块位起点（校时换算的墙钟 epoch 毫秒）
    qint64 wallEndMs = 0;         ///< 块位终点（已分析最大流内时刻的墙钟）
    qint64 streamDurationMs = 0;  ///< .vla 已分析时长（块位宽度的数据源）
    QString calibrationSummary;   ///< 校时徽标缓存文案（tooltip 展示）

    /// P-69 合并轨段块（空=单段传统路；非空=合并轨逐段一块，同行多色）
    struct SegBlock {
        qint64 wallStartMs = 0;
        qint64 wallEndMs = 0;
        int segIdx = 0;
    };
    QVector<SegBlock> segs;
};

/// 装配案件多机时间线：跳过未校时/.vla 缺失/无分析数据的视频；
/// 返回按墙钟起点升序的条目集。
QVector<CamLane> buildCamLanes(const QString &caseDir,
                               const QVector<CaseVideoRef> &videos);

/// 机位清单条目（P-59 机位勾选面板）：案件库存全量（视频 + 前处理产物）
struct CamInventoryItem {
    QString id;                   ///< V###（视频）/ P###（前处理产物）
    QString displayName;          ///< 机位标签（cameraLabel），缺省文件名
    QString path;                 ///< 有效绝对路径（引用制，源文件只读）
    bool fromPreprocess = false;  ///< 前处理会话产物（拼接/转码输出）
    bool pathExists = false;      ///< 源文件在盘（缺失路禁选）
    bool calibrated = false;      ///< 已校时（.vla 实读 SSOT，R5——不看徽标缓存）
    SyncLaneData lane;            ///< calibrated 时已填妥，勾选即可入列
    qint64 analyzedDurationMs = 0; ///< P-69：.vla 已分析最大流内时刻（段时长首选源）
    QString groupKey;             ///< P-69 归组键：cameraLabel 非空用之，缺省=自身 id
                                  /// （前处理产物常以「源机位编号」作标签——v1.13.3 返修：
                                  /// 此前误用 displayName（无标签时退化文件名）致错配/漏配）
};

/// P-69 合并轨分组：同机位标签（displayName）≥2 项且全部已校时+在盘
struct CamMergedGroup {
    QString label;                ///< 机位标签
    QVector<int> memberIdx;       ///< m_inventory 下标（按墙钟起点升序）
};
/// 纯函数（内联于头：sync_test 直接编译验证，免链重依赖）
inline QVector<CamMergedGroup> buildMergedGroups(const QVector<CamInventoryItem> &items)
{
    QHash<QString, QVector<int>> byLabel;
    for (int i = 0; i < items.size(); ++i)
        if (items[i].pathExists)
            byLabel[items[i].groupKey].append(i);
    QVector<CamMergedGroup> out;
    for (auto it = byLabel.constBegin(); it != byLabel.constEnd(); ++it) {
        if (it.value().size() < 2)
            continue;
        // 合并仅模式 A（拍板）：同编号任一文件未校时 → 整组不成立，拒静默
        // 拼半组（C1）——用户须先补齐校时，成员仍可按单路勾选
        bool allCal = true;
        for (int mi : it.value())
            if (!items[mi].calibrated) { allCal = false; break; }
        if (!allCal)
            continue;
        CamMergedGroup g;
        g.label = it.key();
        g.memberIdx = it.value();
        std::sort(g.memberIdx.begin(), g.memberIdx.end(),
                  [&](int a, int b) {
                      return syncLaneWallStart(items[a].lane)
                             < syncLaneWallStart(items[b].lane);
                  });
        out.append(g);
    }
    std::sort(out.begin(), out.end(), [&](const CamMergedGroup &a,
                                          const CamMergedGroup &b) {
        return syncLaneWallStart(items[a.memberIdx.first()].lane)
               < syncLaneWallStart(items[b.memberIdx.first()].lane);
    });
    return out;
}

/// P-69 勾选面板默认勾选决策（纯函数，sync_test 直编验证）：
/// 合并轨组行优先于成员行默认勾选（否则成员被自动勾上 → 组行互斥禁用变灰，
/// 用户根本没机会选合并——v1.13.3 第二轮返修）；非组成员的已校时在盘项补足，
/// 合计不超 cap 路。
struct PickerDefaults {
    QSet<int> groups;    ///< 默认勾的组行下标
    QSet<int> members;   ///< 默认勾的成员（清单项）下标
};
inline PickerDefaults pickerDefaultChecks(const QVector<CamInventoryItem> &items,
                                          const QVector<CamMergedGroup> &groups,
                                          int cap = 4)
{
    PickerDefaults d;
    QSet<int> grouped;
    for (const auto &g : groups)
        for (int mi : g.memberIdx)
            grouped.insert(mi);
    int n = 0;
    for (int gi = 0; gi < groups.size() && n < cap; ++gi) {
        d.groups.insert(gi);
        ++n;
    }
    for (int i = 0; i < items.size() && n < cap; ++i) {
        if (grouped.contains(i))
            continue;
        if (items[i].calibrated && items[i].pathExists) {
            d.members.insert(i);
            ++n;
        }
    }
    return d;
}

/// P-69 段时长预读：容器总时长（ffprobe format=duration；失败 0）
qint64 probeMediaDurationMs(const QString &path);

/// 装配机位勾选清单（P-59）：videos[] + preprocessSessions[].outputRefs[]
/// 全量登记（顺序：视频按入案序，随后前处理产物按会话序）；校时状态逐路
/// 实读案内 .vla（cal.isValid && isEffective）；displayName 优先机位标签。
QVector<CamInventoryItem> buildCamInventory(const CaseManager &cm);
