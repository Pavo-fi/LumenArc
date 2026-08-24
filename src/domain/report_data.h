/**
 * @file report_data.h
 * @brief P-28 报告模块：报告数据模型（聚合产物，渲染器唯一输入）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * ReportService::collect 从案件/状态聚合填充；DocxRenderer 消费。
 * 远期 HtmlRenderer 同吃本结构（渲染器缝，拍板预留）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>

#include "event_calib.h"

/// 逐视频检材行（物理属性 + 校时结论 + 哈希 + 证据）
struct ReportVideoRow {
    QString id;                 ///< V###/P###（检材编号）
    QString cameraLabel;        ///< 监控点位（机位标签）
    QString shootDir;           ///< 拍摄方向（补录，可空）
    QString extractMethod;      ///< 提取方式（补录，可空）
    QString storageMedium;      ///< 存储介质（补录，可空）
    QString fileName;
    QString filePath;           ///< 有效绝对路径
    bool    fileExists = true;  ///< 源文件在位（自检❌项）
    // ---- 物理属性（ffprobe）----
    QString format;             ///< 容器格式
    QString codec;              ///< 视频编码
    qint64  sizeBytes = 0;
    int     width = 0;
    int     height = 0;
    double  fps = 0.0;
    qint64  durationMs = 0;
    // ---- 校时 ----
    bool    hasCalib = false;
    QString calibWayText;       ///< 校时方式中文（未校时/手动标定/OCR 自动识别…）
    double  conf = -1.0;        ///< 0~1；-1 = 不适用
    qint64  wallStartMs = 0;    ///< 记录时间范围（校时后，含北京时间偏移）
    qint64  wallEndMs = 0;
    QString osdSampleText;      ///< 监控显示时间取样（OCR 原文/推算）
    QString formulaText;        ///< 校准公式（人读）
    QString timeDiffText;       ///< 时间差（人读：快/慢 X）
    // ---- 哈希 ----
    QString md5;
    QString sha256;
    // ---- 证据 ----
    QString chartPng;           ///< 亮度曲线整段光栅图（GUI 线程渲染后填）
    QStringList evidencePhotos; ///< 校准证据帧绝对路径
    QVector<eventcalib::EventAnchor> anchors;   ///< P-73 本路锚点全表
};

/// 关键节点行（图表标签 → 分析过程骨架）
struct ReportNodeRow {
    qint64  wallMs = 0;         ///< 节点墙钟（北京时间口径）
    QString sourceLabel;        ///< 来源机位
    QString text;               ///< 标签文字
};

/// 接力对时取证链（P-73，一案可多条）
struct ReportChain {
    QString laneLabel;          ///< 被校时路
    QStringList hopLines;       ///< 逐跳人读行（含事件名/容差）
    QString totalToleranceText; ///< 累积容差人读
};

/// 前处理拼接记录（证据——拍板 2026-08-23：前处理文件也是分析文件，
/// 拼接记录列为证据）
struct ReportConcatRecord {
    QString sessionTs;          ///< 前处理会话时间戳目录名
    QString productFile;        ///< 产物文件名（merged_concat.mp4…）
    QString productId;          ///< 案内编号（P###；未登记为空）
    QString evidenceDir;        ///< 证据目录绝对路径（report.csv/operations.log）
    QStringList logHighlights;  ///< operations.log 关键决策行（素材统计/转码原因…）
    QVector<QVector<QString>> sourceRows;  ///< report.csv 行：[序号/源文件/时长/处理动作]
};

/// 报告聚合数据（渲染器唯一输入）
struct ReportData {
    // ---- 封面/基本情况（CaseMeta + 报告扩展位）----
    QString caseNo;
    QString title;              ///< 火灾名称 = 案件名称
    QString investigator;       ///< 分析人 = 调查员
    QString unit;               ///< 送检/制作单位
    qint64  incidentTimeMs = 0; ///< 案发时间
    QString city, district, locationDetail, description;
    QHash<QString, QString> extraFields;   ///< report/reviewer、report/approver…
    qint64  generatedAtMs = 0;
    QString appVersion;         ///< 分析软件版本号（追溯）

    QVector<ReportVideoRow> videos;
    QVector<ReportNodeRow>  nodes;         ///< 全案件标签（按墙钟升序）
    QVector<ReportChain>    chains;
    QVector<ReportConcatRecord> concatRecords;
    QStringList snapshotPaths;             ///< 案内快照图（关键帧）
    QStringList exportClips;               ///< 案内 exports/ 导出片段名
    QString     sitemapPng;                ///< P-74 点位图成品（空=未绘制）
    QStringList limitationNotes;           ///< 局限性自动行（P-48 错读/容差等）
};
