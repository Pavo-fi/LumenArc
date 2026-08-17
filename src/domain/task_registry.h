/**
 * @file task_registry.h
 * @brief 分析任务注册表（P1a，落地 R8）：任务描述 + 进程级注册中心
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计（DEVELOPMENT_PLAN_V1.8_CN.md §3.1）：
 *  - 新分析功能以「任务描述 + 结果 channel + 展示面板」形式注册（R8），
 *    禁止在 MainWindow/AnalysisController 里加 if(taskType==...) 分支；
 *  - domain 纯数据：不 include Widgets、不 include i18n——显示名以中英
 *    双字符串存储，调用方经 lang() 取用；
 *  - 前置条件以闭包注入（app 层注册时绑定 domain 模型），domain 不持
 *    VideoState 头依赖；
 *  - producedChannels 的 id 与 AnalysisSnapshot 通道字典、.vla v10 META
 *    channels 清单同源（"luminance"/"audio"）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include "analysis_snapshot.h"

/**
 * @brief 单个分析任务的不可变描述。
 *
 * 注册时机：app 层（AnalysisTaskService 构造时）。注册后进程内只读。
 */
struct AnalysisTaskDesc
{
    /// 任务 id（稳定键，与 .vla channel id 对齐：AnalysisChannels::luminance()/audio()）
    QString taskId;
    /// 展示名（中/英；调用方 lang(zh, en) 取用——domain 不 include i18n）
    QString displayNameZh;
    QString displayNameEn;
    /**
     * @brief 前置条件检查：返回空 = 可运行；非空 = 用户可读的失败原因
     *        （如「请先在视频上绘制至少一个 ROI 区域。」）。
     * 闭包由 app 层注册时绑定（捕获 domain 模型指针，禁止捕获 UI 控件）。
     */
    std::function<QString()> preconditionError;
    /// 该任务产出的分析通道 id 列表（合并策略与展示面板路由依据）
    QStringList producedChannels;
};

/**
 * @brief 进程级任务注册中心（两任务起步，够用；线程安全读）。
 *
 * 注册仅发生在构造期（main/服务构造），运行期只读——读侧免锁。
 */
class TaskRegistry
{
public:
    static TaskRegistry &instance();

    /// 注册任务（taskId 重复 = 后注册覆盖前者；仅构造期调用）
    void registerTask(const AnalysisTaskDesc &desc);

    QVector<AnalysisTaskDesc> tasks() const;
    /// 按 id 查找；未注册返回 nullptr
    const AnalysisTaskDesc *find(const QString &taskId) const;

private:
    TaskRegistry() = default;
    QVector<AnalysisTaskDesc> m_tasks;
};
