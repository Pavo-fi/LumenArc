/**
 * @file ui/app_state.h
 * @brief 前端 V2 骨架：应用状态（视图只订阅，不持有领域状态）
 *
 * 六阶段主线 I1：接入 → 整备 → 校时 → 分析 → 产出 → 交付。
 * 闸门（GateState）是**派生值**，不是各处手写 setEnabled。
 */
#pragma once

#include <QObject>
#include <QString>

namespace ui {

/// 办案主线六阶段
enum class Stage {
    Ingest = 0,   ///< 接入
    Prepare,      ///< 整备
    Align,        ///< 校时
    Analyze,      ///< 分析
    Produce,      ///< 产出
    Deliver,      ///< 交付
    Count
};

/// 中文阶段名（UI 用）
QString stageName(Stage s);

/// 阶段闸门三态
enum class GateState {
    NotStarted = 0,   ///< 未开始
    NeedsAttention,   ///< 有隐患（黄点）
    Ready             ///< 已完成（绿点）
};

/**
 * @brief 前端唯一真相。
 *
 * 业务侧通过 setXxx() 推送事实；视图只读 + 订阅 changed()。
 * 禁止视图自己存一份领域状态（见 docs/FRONTEND_V2_IA_CN.md §5 两条铁律）。
 */
class AppState : public QObject
{
    Q_OBJECT

public:
    explicit AppState(QObject *parent = nullptr);

    // ---- 当前阶段 ----
    Stage stage() const { return m_stage; }
    void setStage(Stage s);

    // ---- 事实输入（业务侧推送）----
    void setCaseOpen(bool v);
    void setVideoCount(int n);
    void setUncalibratedCount(int n);
    void setAnalysisCount(int n);
    void setOutputCount(int n);
    void setIntegrityOk(bool v);

    bool caseOpen() const { return m_caseOpen; }
    int  videoCount() const { return m_videoCount; }
    int  uncalibratedCount() const { return m_uncalibratedCount; }
    int  analysisCount() const { return m_analysisCount; }
    int  outputCount() const { return m_outputCount; }
    bool integrityOk() const { return m_integrityOk; }

    // ---- 派生闸门 ----
    GateState gate(Stage s) const;
    /// 未就绪原因（给「为什么不行 + 一键去补」用）
    QString gateHint(Stage s) const;

signals:
    void changed();

private:
    Stage m_stage = Stage::Ingest;

    bool m_caseOpen = false;
    int  m_videoCount = 0;
    int  m_uncalibratedCount = 0;
    int  m_analysisCount = 0;
    int  m_outputCount = 0;
    bool m_integrityOk = false;
};

} // namespace ui
