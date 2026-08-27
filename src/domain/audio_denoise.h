#ifndef AUDIO_DENOISE_H
#define AUDIO_DENOISE_H

#include <QVector>

/// @file audio_denoise.h
/// @brief 谱门控降噪（P-54：libav 引擎原生降噪，替代已退役的 Python 谱减法）
///
/// 算法（就地处理 PCM，分析显示链路专用）：
///   1. STFT：N=2048 / hop=512（N/4，Hann² 满足 COLA 定和）；
///   2. 噪声谱估计：等距采样 ≤2000 帧，每频点取幅度的 25 分位数
///      （对连续底噪稳健，无需纯噪声段先验）；
///   3. 谱减增益：g = (|X| - α·N) / (|X| + eps)，夹取 [0.02, 1]
///      （α = strength，1.0=标准谱减，≥2.0 过减=强降噪；下限防"空洞感"）；
///   4. 增益掩码平滑：时间向快攻慢释（prev×0.6 下限）+ 频率向 3 点平滑，
///      抑制音乐噪声（musical noise）；
///   5. ISTFT + 重叠相加，按 Σw² 逐样本归一，长度不变。
/// 相位保持原值（只塑形幅度），语音/事件瞬态可懂度损失最小。
///
/// 注意：仅用于分析显示（语谱图/音量曲线更干净）；播放音频与原始证据
/// 数据不受任何影响（取证红线：原始 PCM 不落盘改写）。
void spectralGateDenoise(QVector<float> &pcm, int sampleRate, double strength);

#endif // AUDIO_DENOISE_H
