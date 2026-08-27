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

/// @class SpectralGateStream
/// @brief 流式谱门控降噪（播放链路专用，v1.16.1 P-54b）
///
/// 与离线版的差异只在噪声谱估计：流式无法全局采样分位数，改用 afftdn 式
/// 自适应底噪跟踪（每频点：低于估计→立即下跟；高于→慢速上浮 ~0.4%/帧）。
/// 增益公式/时频平滑/OLA 与离线版一致。
///
/// 内容对齐保证（取证关键）：输出样本 p 恒为输入样本 p 的降噪版（流重索引），
/// 计算滞后 ∈ [kWin-kHop, kWin) 样本由播放端 1s 设备缓冲吸收——
/// 稳态零音画偏移；seek/开关切换时 reset() 重建状态。
class SpectralGateStream
{
public:
    SpectralGateStream() = default;

    /// 配置（采样率/声道数/强度任一变化即内部重建）
    void configure(int sampleRate, int channels, double strength);
    bool isConfigured() const { return m_configured; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    double strength() const { return m_strength; }

    /// seek/换流：清输入缓冲/OLA 累加器/底噪估计（底噪由后续帧快速重建）
    void reset();

    /// 追加交错 int16 输入（totalSamples 含所有声道），已定稿样本追加到 out。
    /// 定稿前沿 = 已完整覆盖（COLA）的前缀；稳态滞后 ≈ kWin-kHop ~ kWin 样本。
    void feed(const int16_t *interleaved, int totalSamples, QVector<int16_t> &out);

    /// 收尾：零填充把尾巴全部推出（累计输出长度守恒 = 累计输入）。
    /// 播放停止无需调；测试/导出场景用。
    void flush(QVector<int16_t> &out);

    static constexpr int kWin = 2048;   ///< 窗长（48k 下 42.7ms，23.4Hz/格）
    static constexpr int kHop = 512;    ///< N/4：Hann² COLA 定和

private:
    struct ChannelState {
        QVector<float> inBuf;           ///< 待处理输入（队首对齐全局 frameStart）
        QVector<double> acc;            ///< OLA 幅度累加器（kWin，对齐 frameStart）
        QVector<double> noise;          ///< 自适应底噪谱（kBins）
        bool noiseInit = false;
        QVector<double> prevGain;       ///< 时向平滑状态
    };
    void processFrame(QVector<int16_t> &out, qint64 maxEmit);

    int m_sampleRate = 48000;
    int m_channels = 1;
    double m_strength = 1.0;
    bool m_configured = false;
    QVector<ChannelState> m_ch;
    QVector<double> m_norm;             ///< 各声道共享的 w² 累加（同窗同步）
    qint64 m_fed = 0;                   ///< 累计真实输入帧数（每声道）
    qint64 m_emitted = 0;               ///< 累计定稿输出帧数

    // FFT 上下文（跨声道复用 scratch）
    void *m_ftx = nullptr;              ///< AVTXContext*
    void *m_itx = nullptr;
    void *m_ffn = nullptr;              ///< av_tx_fn（void* 免头文件泄 ffmpeg）
    void *m_ifn = nullptr;
    void ensureTx();                    ///< 懒建；configure/参数变化时释放重建
    void releaseTx();
};

#endif // AUDIO_DENOISE_H
