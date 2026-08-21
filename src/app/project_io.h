/**
 * @file project_io.h
 * @brief 工程读写服务（P-31 T1）：.vla/CSV/时间戳 ROI 记忆/徽标文案 收口
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 从 MainWindow 纯移动（行为冻结）：对话框/控件操作留在 UI 层，本服务只做
 * 路径分流、参数组装与文件读写（R1：app 层不 include Widgets）。
 */
#pragma once

#include <QObject>
#include <QMutex>
#include <QRect>
#include <QRectF>
#include <QPolygon>
#include <QVector>
#include "domain/analysis_snapshot.h"
#include "domain/time_calibration.h"
#include "domain/timeline_model.h"

class CaseManager;

class ProjectIO : public QObject
{
    Q_OBJECT

public:
    /// cases = 案件管理器（可空 → 独立模式行为）；model = 目标 TimelineModel
    /// （loadVla 经它装载并发 dataReplaced；可空 → 内部临时模型，仅取字段）
    explicit ProjectIO(CaseManager *cases, TimelineModel *model,
                       QObject *parent = nullptr);

    /// .vla 保存参数包（MainWindow 收集 UI 侧字段后整体移交）
    struct VlaSaveRequest {
        QVector<QRect> regions;
        TimeCalibration calibration;
        QRect magnifierRect;
        QVector<ChartLabel> labels;
        QRect pinnedRect;
        SnapshotFusionData fusion;
        QVector<QPolygon> polygons;
        QVector<GuideLine> guideLines;
        QVector<int> regionRoiIds;
        QVector<int> polygonRoiIds;
        AbRegionData abRegion;               // P-68 入 .vla（拍板 Q5）
        speedplan::SpeedPlan speedPlan;
    };

    /// .vla 加载结果字段包（还原应用由 UI 层执行）
    struct LoadedVla {
        QVector<QRect> regions;
        TimeCalibration calibration;
        QRect magnifierRect;
        QVector<ChartLabel> labels;
        QRect pinnedRect;
        SnapshotFusionData fusion;
        QVector<QPolygon> polygons;
        QVector<GuideLine> guideLines;
        QVector<int> regionRoiIds;
        QVector<int> polygonRoiIds;
        AbRegionData abRegion;
        speedplan::SpeedPlan speedPlan;
    };

    /// 保存默认路径分流（v1.3.0）：空视频 → analysis_result.vla；
    /// 直接加载 .vla → 覆写原文件（v1.2.2 行为保持）；普通视频 → 案件
    /// videos/V###.vla（入案）/源旁 .vla（独立）
    QString suggestSavePath(const QString &currentVideoPath) const;

    /// 前台同步保存（含目录创建）
    bool saveVlaNow(const QString &path, const VlaSaveRequest &req);
    /// 后台异步保存（QtConcurrent；UI 线程外仅触 TimelineModel——
    /// 请求字段全部值拷贝，线程安全，同旧版 saveCurrentVlaAsync 语义）
    void saveVlaAsync(const QString &path, const VlaSaveRequest &req);

    /// .vla 加载（经成员模型装载，成功发 dataReplaced）
    bool loadVla(const QString &path, LoadedVla *out);

    /// 案件校时徽标文案（纯函数：effective 校时的来源/点数/rate/分段标注）
    static QString calibrationBadgeSummary(const TimeCalibration &cal);

    /// 标签 CSV 导出（含 CSV 转义与校时时间列；labels 为空返回 false）
    static bool exportLabelsCsv(const QString &path,
                                const QVector<ChartLabel> &labels,
                                const TimeCalibration &calibration);

    /// 时间戳框选记忆（v1.3.0 随案分流：入案 case.json / 独立 QSettings）
    QRectF savedTimestampRoi(const QString &videoPath) const;
    void saveTimestampRoi(const QString &videoPath, const QRectF &norm);

signals:
    /// 后台保存完成（false = 写失败，C2 不静默）
    void vlaSaved(const QString &path, bool ok);

private:
    static QRectF readTimestampRoiRegistry(const QString &videoPath);
    void runSaveLoop(TimelineModel *m);   // 保存工作线程（单飞+尾追）

    CaseManager *m_cases = nullptr;      // 不持有
    TimelineModel *m_model = nullptr;    // 不持有；空 → 惰性临时模型
    TimelineModel *model();

    // 保存合并（P-59：校时落盘偶发丢失实锤——在途旧请求与最新请求并发，
    // 提交顺序倒置时旧请求后落盘盖掉新校时）：单飞+尾追——保存中来的新
    // 请求只留最新，在途完成后拾取再写一次；最新请求必胜
    QMutex m_saveMutex;
    bool m_saveRunning = false;
    bool m_savePending = false;
    QString m_savePendingPath;
    VlaSaveRequest m_savePendingReq;
};
