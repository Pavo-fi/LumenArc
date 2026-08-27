#include "audio_denoise.h"

extern "C" {
#include <libavutil/tx.h>
}

constexpr double kPi = 3.14159265358979323846;
#include <algorithm>
#include <cmath>
#include <cstring>
#include <QtGlobal>

void spectralGateDenoise(QVector<float> &pcm, int sampleRate, double strength)
{
    if (strength <= 0.0 || pcm.size() < 8192)
        return;
    (void)sampleRate;   // 窗长/跳步固定（24kHz 约定）；采样率仅影响频点物理含义

    constexpr int N = 2048;
    constexpr int kHop = N / 4;        // 512：Hann² COLA 定和
    constexpr int kBins = N / 2 + 1;   // 1025
    constexpr double kFloor = 0.02;    // 增益下限 ≈ -34dB（防"空洞感"）
    constexpr double kEps = 1e-12;

    const int nFrames = 1 + (pcm.size() - N) / kHop;

    // 周期 Hann（分析与合成同窗）
    QVector<double> w(N);
    for (int i = 0; i < N; ++i)
        w[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / N);

    AVTXContext *ftx = nullptr, *itx = nullptr;
    av_tx_fn ffn = nullptr, ifn = nullptr;
    // 实测（txprobe）：本 avutil 版本逆变换恒为非归一（scale 参数被忽略，
    // 输出 = N·x）——归一化改为手动：OLA 累加时除 N。
    if (av_tx_init(&ftx, &ffn, AV_TX_DOUBLE_FFT, 0, N, nullptr, 0) < 0)
        return;
    if (av_tx_init(&itx, &ifn, AV_TX_DOUBLE_FFT, 1, N, nullptr, 0) < 0) {
        av_tx_uninit(&ftx);
        return;
    }

    QVector<double> in(2 * N), out(2 * N);

    auto stftMags = [&](int f, QVector<double> &mags) {
        const int s = f * kHop;
        for (int i = 0; i < N; ++i) {
            in[2 * i] = double(pcm[s + i]) * w[i];
            in[2 * i + 1] = 0.0;
        }
        ffn(ftx, out.data(), in.data(), sizeof(AVComplexDouble));
        for (int k = 0; k < kBins; ++k)
            mags[k] = std::hypot(out[2 * k], out[2 * k + 1]);
    };

    // ---- Pass 1：等距采样 ≤2000 帧估计噪声谱（每频点幅度 25 分位）----
    const int stride = qMax(1, nFrames / 2000);
    QVector<QVector<float>> sampMags;
    sampMags.reserve(nFrames / stride + 1);
    QVector<double> m(kBins);
    for (int f = 0; f < nFrames; f += stride) {
        stftMags(f, m);
        QVector<float> row(kBins);
        for (int k = 0; k < kBins; ++k)
            row[k] = float(m[k]);
        sampMags.append(row);
    }
    QVector<double> noise(kBins, 0.0);
    {
        QVector<float> col(sampMags.size());
        for (int k = 0; k < kBins; ++k) {
            for (int r = 0; r < sampMags.size(); ++r)
                col[r] = sampMags[r][k];
            const int nth = qBound(0, int(col.size() * 0.25), col.size() - 1);
            std::nth_element(col.begin(), col.begin() + nth, col.end());
            noise[k] = col[nth];
        }
    }

    // ---- Pass 2：全帧增益掩码 + ISTFT + 重叠相加 ----
    QVector<float> y(pcm.size(), 0.0f);
    QVector<double> norm(pcm.size(), 0.0);
    QVector<double> gain(kBins, 1.0), prevGain(kBins, 1.0), sm(kBins, 1.0);

    for (int f = 0; f < nFrames; ++f) {
        const int s = f * kHop;
        for (int i = 0; i < N; ++i) {
            in[2 * i] = double(pcm[s + i]) * w[i];
            in[2 * i + 1] = 0.0;
        }
        ffn(ftx, out.data(), in.data(), sizeof(AVComplexDouble));

        for (int k = 0; k < kBins; ++k) {
            const double mag = std::hypot(out[2 * k], out[2 * k + 1]);
            const double g = (mag - strength * noise[k]) / (mag + kEps);
            gain[k] = qBound(kFloor, g, 1.0);
        }
        // 时间向平滑：快攻慢释（上升立即、下降按 0.6/帧 ≈ 21ms 缓释）
        for (int k = 0; k < kBins; ++k) {
            const double sg = qMax(gain[k], prevGain[k] * 0.6);
            prevGain[k] = sg;
            gain[k] = sg;
        }
        // 频率向 3 点平滑（抑音乐噪声）
        for (int k = 0; k < kBins; ++k) {
            const double a = (k > 0) ? gain[k - 1] : gain[k];
            const double b = gain[k];
            const double c = (k + 1 < kBins) ? gain[k + 1] : gain[k];
            sm[k] = 0.25 * a + 0.5 * b + 0.25 * c;
        }
        // 应用增益（保持相位；实信号共轭对称；bin0/Nyquist 为纯实不镜像）
        for (int k = 0; k < kBins; ++k) {
            const double g = sm[k];
            const double re = out[2 * k] * g, im = out[2 * k + 1] * g;
            in[2 * k] = re;
            in[2 * k + 1] = im;
            if (k > 0 && k < kBins - 1) {
                in[2 * (N - k)] = re;
                in[2 * (N - k) + 1] = -im;
            }
        }
        ifn(itx, out.data(), in.data(), sizeof(AVComplexDouble));
        // OLA：w·ifft/N 累加；norm 记 w²（COLA 定和时二者相除还原 x）
        for (int i = 0; i < N; ++i) {
            y[s + i] += float(out[2 * i] * w[i] / N);
            norm[s + i] += w[i] * w[i];
        }
    }

    for (int i = 0; i < pcm.size(); ++i) {
        if (norm[i] > 1e-8)
            pcm[i] = y[i] / float(norm[i]);
        // norm==0 的尾部（不足一帧）：保留原样
    }

    av_tx_uninit(&ftx);
    av_tx_uninit(&itx);
}
// ============================================================================
// SpectralGateStream：流式谱门控（播放链路）
// 与离线版共用：增益公式、增益下限、时频平滑参数、OLA 结构；
// 差异仅在噪声谱估计（流式=afftdn 式自适应底噪跟踪）。
// ============================================================================

void SpectralGateStream::configure(int sampleRate, int channels, double strength)
{
    if (m_configured && m_sampleRate == sampleRate && m_channels == channels
        && qAbs(m_strength - strength) < 1e-9)
        return;
    releaseTx();
    m_sampleRate = sampleRate;
    m_channels = qBound(1, channels, 2);
    m_strength = qBound(0.0, strength, 10.0);
    m_ch.clear();
    m_ch.resize(m_channels);
    m_configured = true;
    reset();
}

void SpectralGateStream::reset()
{
    constexpr int kBins = kWin / 2 + 1;
    for (auto &ch : m_ch) {
        ch.inBuf.clear();
        ch.acc = QVector<double>(kWin, 0.0);
        ch.noise = QVector<double>(kBins, 0.0);
        ch.noiseInit = false;
        ch.prevGain = QVector<double>(kBins, 1.0);
    }
    m_norm = QVector<double>(kWin, 0.0);
    m_fed = 0;
    m_emitted = 0;
}

void SpectralGateStream::ensureTx()
{
    if (m_ftx)
        return;
    AVTXContext **ftx = reinterpret_cast<AVTXContext **>(&m_ftx);
    AVTXContext **itx = reinterpret_cast<AVTXContext **>(&m_itx);
    av_tx_fn *ffn = reinterpret_cast<av_tx_fn *>(&m_ffn);
    av_tx_fn *ifn = reinterpret_cast<av_tx_fn *>(&m_ifn);
    if (av_tx_init(ftx, ffn, AV_TX_DOUBLE_FFT, 0, kWin, nullptr, 0) < 0) {
        av_tx_uninit(ftx);
        return;
    }
    // 逆变换 scale 参数实测被忽略（见离线版注记）——OLA 手动除 kWin
    if (av_tx_init(itx, ifn, AV_TX_DOUBLE_FFT, 1, kWin, nullptr, 0) < 0) {
        av_tx_uninit(itx);
        av_tx_uninit(ftx);
    }
}

void SpectralGateStream::releaseTx()
{
    if (m_ftx) {
        AVTXContext *p = reinterpret_cast<AVTXContext *>(m_ftx);
        av_tx_uninit(&p);
        m_ftx = nullptr;
    }
    if (m_itx) {
        AVTXContext *p = reinterpret_cast<AVTXContext *>(m_itx);
        av_tx_uninit(&p);
        m_itx = nullptr;
    }
    m_ffn = nullptr;
    m_ifn = nullptr;
}

// 处理一帧（窗 = 各声道 inBuf 队首 kWin 样本）：
// OLA 累加 → 立刻收割 acc[0..kHop) 定稿样本（收割在读出后才左移）→ 左移对齐
void SpectralGateStream::processFrame(QVector<int16_t> &out, qint64 maxEmit)
{
    constexpr int kBins = kWin / 2 + 1;
    constexpr double kFloor = 0.02;
    constexpr double kEps = 1e-12;
    constexpr double kRiseRate = 0.004;   // 底噪慢速上浮（≈翻倍 4 秒）

    auto ffn = reinterpret_cast<av_tx_fn>(m_ffn);
    auto ifn = reinterpret_cast<av_tx_fn>(m_ifn);
    AVTXContext *ftx = reinterpret_cast<AVTXContext *>(m_ftx);
    AVTXContext *itx = reinterpret_cast<AVTXContext *>(m_itx);

    // 周期 Hann（与离线版同式）
    QVector<double> w(kWin);
    for (int i = 0; i < kWin; ++i)
        w[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / kWin);

    QVector<double> in(2 * kWin), fftout(2 * kWin);
    QVector<double> gain(kBins), sm(kBins);
    QVector<QVector<double>> chanOut(m_channels, QVector<double>(kWin));

    for (int c = 0; c < m_channels; ++c) {
        ChannelState &ch = m_ch[c];
        for (int i = 0; i < kWin; ++i) {
            in[2 * i] = double(ch.inBuf[i]) * w[i];
            in[2 * i + 1] = 0.0;
        }
        ffn(ftx, fftout.data(), in.data(), sizeof(AVComplexDouble));

        for (int k = 0; k < kBins; ++k) {
            const double mag = std::hypot(fftout[2 * k], fftout[2 * k + 1]);
            // 自适应底噪跟踪：低于估计→立即下跟；高于→慢速上浮
            if (!ch.noiseInit)
                ch.noise[k] = mag;
            else if (mag < ch.noise[k])
                ch.noise[k] = mag;
            else
                ch.noise[k] += (mag - ch.noise[k]) * kRiseRate;
            // 运行最小值→典型底噪标定：Rayleigh 分布下运行最小值远低于分位
            // 噪声谱（实测不标定时降噪力度不足，×0.65）；×4 ≈ 25 分位水平
            const double ne = ch.noise[k] * 4.0;
            const double g = (mag - m_strength * ne) / (mag + kEps);
            gain[k] = qBound(kFloor, g, 1.0);
        }
        ch.noiseInit = true;
        // 时向快攻慢释 + 频向 3 点平滑（与离线版同参数）
        for (int k = 0; k < kBins; ++k) {
            const double sg = qMax(gain[k], ch.prevGain[k] * 0.6);
            ch.prevGain[k] = sg;
            gain[k] = sg;
        }
        for (int k = 0; k < kBins; ++k) {
            const double a = (k > 0) ? gain[k - 1] : gain[k];
            const double b = gain[k];
            const double cc = (k + 1 < kBins) ? gain[k + 1] : gain[k];
            sm[k] = 0.25 * a + 0.5 * b + 0.25 * cc;
        }
        for (int k = 0; k < kBins; ++k) {
            const double g = sm[k];
            const double re = fftout[2 * k] * g, im = fftout[2 * k + 1] * g;
            in[2 * k] = re;
            in[2 * k + 1] = im;
            if (k > 0 && k < kBins - 1) {
                in[2 * (kWin - k)] = re;
                in[2 * (kWin - k) + 1] = -im;
            }
        }
        ifn(itx, fftout.data(), in.data(), sizeof(AVComplexDouble));
        for (int i = 0; i < kWin; ++i)
            chanOut[c][i] = fftout[2 * i];   // 未归一（逆变换恒 N·x，收割处除）
    }

    // OLA 累加
    for (int c = 0; c < m_channels; ++c)
        for (int i = 0; i < kWin; ++i)
            m_ch[c].acc[i] += chanOut[c][i] * w[i] / kWin;
    for (int i = 0; i < kWin; ++i)
        m_norm[i] += w[i] * w[i];

    // 收割 acc[0..kHop)：本帧处理后这 kHop 个样本的全部覆盖帧已齐 → 定稿
    const qint64 n = qMin<qint64>(kHop, qMax<qint64>(0, maxEmit));
    for (qint64 e = 0; e < n; ++e) {
        for (int c = 0; c < m_channels; ++c) {
            const double v = (m_norm[e] > 1e-8)
                ? m_ch[c].acc[e] / m_norm[e] : 0.0;
            const long s16 = std::lround(qBound(-1.0, v, 1.0) * 32767.0);
            out.append(int16_t(s16));
        }
        ++m_emitted;
    }

    // 左移 kHop 对齐下一帧（acc/norm/inBuf 队首同步前进）
    for (int c = 0; c < m_channels; ++c) {
        ChannelState &ch = m_ch[c];
        std::memmove(ch.acc.data(), ch.acc.data() + kHop,
                     (kWin - kHop) * sizeof(double));
        std::fill(ch.acc.end() - kHop, ch.acc.end(), 0.0);
        ch.inBuf.remove(0, kHop);
    }
    std::memmove(m_norm.data(), m_norm.data() + kHop, (kWin - kHop) * sizeof(double));
    std::fill(m_norm.end() - kHop, m_norm.end(), 0.0);
}

void SpectralGateStream::feed(const int16_t *interleaved, int totalSamples,
                              QVector<int16_t> &out)
{
    if (!m_configured || m_strength <= 0.0) {
        for (int i = 0; i < totalSamples; ++i)
            out.append(interleaved[i]);   // 旁路直通（调用方通常已拦，双保险）
        return;
    }
    ensureTx();
    if (!m_ftx || !m_itx) {               // FFT 初始化失败：直通保声音
        for (int i = 0; i < totalSamples; ++i)
            out.append(interleaved[i]);
        return;
    }

    // 反交错入各声道缓冲
    const int frames = totalSamples / m_channels;
    for (int c = 0; c < m_channels; ++c)
        m_ch[c].inBuf.reserve(m_ch[c].inBuf.size() + frames);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < m_channels; ++c)
            m_ch[c].inBuf.append(float(interleaved[i * m_channels + c]) / 32768.0f);
    m_fed += frames;

    while (m_ch[0].inBuf.size() >= kWin)
        processFrame(out, m_fed - m_emitted);
    // 末尾不足一窗的样本留在缓冲（等后续输入或 flush）
}

void SpectralGateStream::flush(QVector<int16_t> &out)
{
    if (!m_configured || m_strength <= 0.0)
        return;
    ensureTx();
    if (!m_ftx || !m_itx)
        return;
    // 零填充驱动剩余帧，直至真实样本全部定稿（长度守恒）
    while (m_emitted < m_fed) {
        if (m_ch[0].inBuf.size() < kWin) {
            const int pad = kWin - m_ch[0].inBuf.size();
            for (int c = 0; c < m_channels; ++c)
                m_ch[c].inBuf.append(QVector<float>(pad, 0.0f));
        }
        processFrame(out, m_fed - m_emitted);
    }
}
