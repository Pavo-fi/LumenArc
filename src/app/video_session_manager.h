/**
 * @file video_session_manager.h
 * @brief 视频会话管理（P-31 T2-A）：VideoStateManager 归属 + 打开编排数据面
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 职责（方案 §3.2，T2 分两阶段落地——本文件为 A 阶段）：
 *  - 持有 VideoStateManager（逐视频内存状态库）；
 *  - 保存现场装配（VideoState 组装 → saveState）；
 *  - 打开决策数据面 OpenPlan：内存现场/缓存 .vla 路径与入案判定/sidecar
 *    校时提取（不碰 widget，widget 应用分支留在 MainWindow——行为冻结）。
 */
#pragma once

#include <QObject>
#include <QString>
#include "videostatemanager.h"
#include "domain/time_calibration.h"

class VideoSessionManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoSessionManager(QObject *parent = nullptr);

    VideoStateManager *stateManager() const { return m_states; }

    /// 保存当前视频现场（VideoState 由 UI 层收集，值拷贝装配）
    void saveCurrentState(const QString &videoPath, const VideoState &state);

    bool hasState(const QString &videoPath) const;
    void removeState(const QString &videoPath);
    /// 键迁移（v1.3.0 M3 任务13：重定位后内存状态跟随新路径）
    void migrateKey(const QString &oldPath, const QString &newPath);
    void clear();

    /// 打开决策（数据面）：内存现场优先 → 案件/源旁缓存探测。
    /// currentCalibration：当前校时（sidecar 仅在无有效校时时评估，Q-4）
    struct OpenPlan {
        VideoState memoryState;
        bool hasMemoryState = false;
        QString cacheVlaPath;         ///< 存在的缓存 .vla（案件 videos/ 或源旁）
        bool cacheIsCaseVideo = false; ///< true=案件权威缓存，静默加载
    };
    OpenPlan planOpen(const QString &videoPath, class CaseManager *cases) const;

    /// sidecar 校时提取（拼接产物 .lumencal.json；Q-4 缺口警告外发）
    static bool loadSidecarCalibration(const QString &videoPath,
                                       TimeCalibration *outCal,
                                       QString *outWarning);

private:
    VideoStateManager *m_states = nullptr;   // 持有（parent 本对象）
};
