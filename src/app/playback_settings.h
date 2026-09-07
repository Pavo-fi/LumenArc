/**
 * @file playback_settings.h
 * @brief 播放参数 SSOT（v1.17.0 P-77，R5 收口）：播放倍速 + 降噪强度
 *
 * 原 MainWindow::m_currentSpeed（10 处）/ m_noiseReductionStrength（3 处）
 * 散落读写（含 eventFilter Key_Z 直写），统一收口至本组件。
 * 纯数据容器（无信号：当前消费方均为即时读取，无订阅者）。
 */
#pragma once

#include <QObject>

class PlaybackSettings : public QObject
{
public:
    explicit PlaybackSettings(QObject *parent = nullptr) : QObject(parent) {}

    /// 播放倍速（1.0f = 原速；原 MainWindow::m_currentSpeed 语义）
    float speed() const { return m_speed; }
    void setSpeed(float s) { m_speed = s; }

    /// 降噪强度（0.0~5.0，滑杆 value/10；原 m_noiseReductionStrength 语义）
    qreal noiseReductionStrength() const { return m_nr; }
    void setNoiseReductionStrength(qreal v) { m_nr = v; }

private:
    float m_speed = 1.0f;
    qreal m_nr = 0.0;
};