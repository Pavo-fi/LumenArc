/**
 * @file task_registry.cpp
 * @brief 分析任务注册表实现（P1a）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "task_registry.h"

TaskRegistry &TaskRegistry::instance()
{
    static TaskRegistry reg;
    return reg;
}

void TaskRegistry::registerTask(const AnalysisTaskDesc &desc)
{
    for (auto &existing : m_tasks) {
        if (existing.taskId == desc.taskId) {
            existing = desc;   // 同 id 重新注册 = 覆盖（仅构造期发生）
            return;
        }
    }
    m_tasks.append(desc);
}

QVector<AnalysisTaskDesc> TaskRegistry::tasks() const
{
    return m_tasks;
}

const AnalysisTaskDesc *TaskRegistry::find(const QString &taskId) const
{
    for (const auto &t : m_tasks)
        if (t.taskId == taskId)
            return &t;
    return nullptr;
}
