/**
 * @file ui/app_state.cpp
 * @brief 前端 V2 骨架：应用状态实现
 */
#include "ui/app_state.h"

namespace ui {

QString stageName(Stage s)
{
    switch (s) {
    case Stage::Ingest:  return QStringLiteral("接入");
    case Stage::Prepare: return QStringLiteral("整备");
    case Stage::Align:   return QStringLiteral("校时");
    case Stage::Analyze: return QStringLiteral("分析");
    case Stage::Produce: return QStringLiteral("产出");
    case Stage::Deliver: return QStringLiteral("交付");
    default:             return QString();
    }
}

AppState::AppState(QObject *parent)
    : QObject(parent)
{
}

void AppState::setStage(Stage s)
{
    if (m_stage == s)
        return;
    m_stage = s;
    emit changed();
}

void AppState::setCaseOpen(bool v)
{
    if (m_caseOpen == v) return;
    m_caseOpen = v;
    emit changed();
}

void AppState::setVideoCount(int n)
{
    n = qMax(0, n);
    if (m_videoCount == n) return;
    m_videoCount = n;
    emit changed();
}

void AppState::setUncalibratedCount(int n)
{
    n = qMax(0, n);
    if (m_uncalibratedCount == n) return;
    m_uncalibratedCount = n;
    emit changed();
}

void AppState::setAnalysisCount(int n)
{
    n = qMax(0, n);
    if (m_analysisCount == n) return;
    m_analysisCount = n;
    emit changed();
}

void AppState::setOutputCount(int n)
{
    n = qMax(0, n);
    if (m_outputCount == n) return;
    m_outputCount = n;
    emit changed();
}

void AppState::setIntegrityOk(bool v)
{
    if (m_integrityOk == v) return;
    m_integrityOk = v;
    emit changed();
}

GateState AppState::gate(Stage s) const
{
    // 独立模式（无案件）也能走完整主线；只有交付阶段要求案件
    switch (s) {
    case Stage::Ingest:
        return m_videoCount > 0 ? GateState::Ready : GateState::NotStarted;

    case Stage::Prepare:
        return m_videoCount > 0 ? GateState::Ready : GateState::NotStarted;

    case Stage::Align:
        if (m_videoCount == 0)
            return GateState::NotStarted;
        return m_uncalibratedCount > 0 ? GateState::NeedsAttention : GateState::Ready;

    case Stage::Analyze:
        if (m_videoCount == 0)
            return GateState::NotStarted;
        return m_analysisCount > 0 ? GateState::Ready : GateState::NotStarted;

    case Stage::Produce:
        return m_analysisCount > 0 ? GateState::Ready : GateState::NotStarted;

    case Stage::Deliver:
        if (m_outputCount == 0)
            return GateState::NotStarted;
        return m_integrityOk ? GateState::Ready : GateState::NeedsAttention;

    default:
        return GateState::NotStarted;
    }
}

QString AppState::gateHint(Stage s) const
{
    switch (s) {
    case Stage::Ingest:
        return QStringLiteral("先打开一段视频或新建案件");
    case Stage::Prepare:
        return QStringLiteral("先接入素材");
    case Stage::Align:
        return QStringLiteral("还有 %1 路未校时，先做校时").arg(m_uncalibratedCount);
    case Stage::Analyze:
        return m_videoCount == 0 ? QStringLiteral("先接入素材")
                                 : QStringLiteral("还没有分析结果");
    case Stage::Produce:
        return QStringLiteral("先完成至少一条分析");
    case Stage::Deliver:
        return m_outputCount == 0 ? QStringLiteral("还没有产物")
                                  : QStringLiteral("完整性校验未通过");
    default:
        return QString();
    }
}

} // namespace ui
