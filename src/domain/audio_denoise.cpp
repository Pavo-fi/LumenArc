#include "audio_denoise.h"

extern "C" {
#include <libavutil/tx.h>
}

constexpr double kPi = 3.14159265358979323846;
#include <algorithm>
#include <cmath>
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
