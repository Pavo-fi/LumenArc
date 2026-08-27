// denoise_test_main.cpp — P-54 谱门控降噪（libav 引擎原生）单测
// 合成信号验证：白噪底噪有效压制、主导纯音保留、长度不变、0 强度旁路。
#include "domain/audio_denoise.h"

constexpr double kPi = 3.14159265358979323846;
#include <cmath>
#include <cstdio>
#include <random>

static double segRms(const QVector<float> &pcm, int from, int to)
{
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += double(pcm[i]) * pcm[i];
    return std::sqrt(s / (to - from));
}

int main()
{
    constexpr int sr = 24000;
    constexpr double noiseAmp = 0.05;
    constexpr double toneAmp = 0.30;
    constexpr double toneHz = 440.0;

    QVector<float> pcm(sr * 3);
    std::mt19937 rng(42);
    std::normal_distribution<double> gauss(0.0, noiseAmp);
    for (int i = 0; i < pcm.size(); ++i) {
        const double n = gauss(rng);
        pcm[i] = float(n);
        if (i >= sr)   // 第 2 秒起叠加 440Hz 纯音
            pcm[i] += float(toneAmp * std::sin(2.0 * kPi * toneHz * i / sr));
    }
    const QVector<float> orig = pcm;

    // 1) strength=0 → 完全旁路
    {
        QVector<float> t = orig;
        spectralGateDenoise(t, sr, 0.0);
        if (t != orig) {
            fprintf(stderr, "FAIL: strength=0 should bypass\n");
            return 1;
        }
        printf("[1] strength=0 bypass OK\n");
    }

    // 2) strength=1.5：底噪段显著压制
    pcm = orig;
    spectralGateDenoise(pcm, sr, 1.5);
    if (pcm.size() != orig.size()) {
        fprintf(stderr, "FAIL: length changed\n");
        return 1;
    }
    const double nb = segRms(orig, int(0.1 * sr), int(0.9 * sr));
    const double na = segRms(pcm,  int(0.1 * sr), int(0.9 * sr));
    printf("[2] noise-seg RMS: %.5f -> %.5f (x%.3f)\n", nb, na, na / nb);
    if (!(na < 0.5 * nb)) {
        fprintf(stderr, "FAIL: noise not reduced enough (x%.3f)\n", na / nb);
        return 1;
    }

    // 3) 纯音段能量保留（440Hz 为主导峰，增益应 ≈1）
    const double tb = segRms(orig, int(1.2 * sr), int(2.8 * sr));
    const double ta = segRms(pcm,  int(1.2 * sr), int(2.8 * sr));
    printf("[3] tone-seg RMS: %.5f -> %.5f (x%.3f)\n", tb, ta, ta / tb);
    if (!(ta > 0.70 * tb && ta < 1.30 * tb)) {
        fprintf(stderr, "FAIL: tone not preserved (x%.3f)\n", ta / tb);
        return 1;
    }

    // 4) 纯音频点保留度：降噪后与原始纯音的相关性（相位应保持）
    {
        double dot = 0.0, eRef = 0.0, eOut = 0.0;
        for (int i = int(1.2 * sr); i < int(2.8 * sr); ++i) {
            const double ref = toneAmp * std::sin(2.0 * kPi * toneHz * i / sr);
            dot += ref * pcm[i];
            eRef += ref * ref;
            eOut += double(pcm[i]) * pcm[i];
        }
        const double corr = dot / std::sqrt(eRef * eOut + 1e-12);
        printf("[4] tone correlation: %.4f\n", corr);
        if (!(corr > 0.90)) {
            fprintf(stderr, "FAIL: tone correlation too low (%.4f)\n", corr);
            return 1;
        }
    }

    printf("ALL PASS (offline)\n");

    // ================= 流式 SpectralGateStream =================
    // 合成 int16 单声道：前 1s 纯白噪，后 2s 叠加 440Hz
    QVector<int16_t> src(sr * 3);
    {
        std::mt19937 rng2(7);
        std::normal_distribution<double> g2(0.0, 0.05);
        for (int i = 0; i < src.size(); ++i) {
            double v = g2(rng2);
            if (i >= sr)
                v += 0.30 * std::sin(2.0 * kPi * 440.0 * i / sr);
            src[i] = int16_t(qBound(-1.0, v, 1.0) * 32767.0);
        }
    }

    auto runStream = [&](int chunk) {
        SpectralGateStream st;
        st.configure(sr, 1, 1.5);
        QVector<int16_t> out;
        for (int i = 0; i < src.size(); i += chunk) {
            const int n = qMin(chunk, src.size() - i);
            st.feed(src.constData() + i, n, out);
        }
        st.flush(out);
        return out;
    };

    // [5] 块大小不变性：4096 与 137 样本两种喂法输出逐位一致
    const QVector<int16_t> a = runStream(4096);
    const QVector<int16_t> b = runStream(137);
    if (a != b) {
        fprintf(stderr, "FAIL: stream chunk-size dependent\n");
        return 1;
    }
    printf("[5] stream chunk-invariance OK\n");

    // [6] flush 后长度守恒
    if (a.size() != src.size()) {
        fprintf(stderr, "FAIL: stream length %d != input %d\n", a.size(), src.size());
        return 1;
    }
    printf("[6] stream length preserved (%d)\n", a.size());

    // [7] 流式降噪有效：底噪段 RMS 显著下降
    auto rms16 = [](const QVector<int16_t> &v, int from, int to) {
        double s = 0.0;
        for (int i = from; i < to; ++i) {
            const double x = v[i] / 32767.0;
            s += x * x;
        }
        return std::sqrt(s / (to - from));
    };
    const double sb = rms16(src, int(0.1 * sr), int(0.9 * sr));
    const double sa = rms16(a,   int(0.1 * sr), int(0.9 * sr));
    printf("[7] stream noise RMS: %.5f -> %.5f (x%.3f)\n", sb, sa, sa / sb);
    if (!(sa < 0.6 * sb)) {
        fprintf(stderr, "FAIL: stream noise not reduced\n");
        return 1;
    }

    // [8] 定稿滞后有界：flush 前输出长度 <= 输入，且差额 ∈ [0, kWin]
    {
        SpectralGateStream st;
        st.configure(sr, 1, 1.5);
        QVector<int16_t> out;
        for (int i = 0; i < src.size(); i += 1000)
            st.feed(src.constData() + i, qMin(1000, src.size() - i), out);
        const int lag = src.size() - out.size();
        printf("[8] pre-flush lag: %d samples (win=%d)\n", lag, SpectralGateStream::kWin);
        if (lag < 0 || lag > SpectralGateStream::kWin) {
            fprintf(stderr, "FAIL: lag out of bounds (%d)\n", lag);
            return 1;
        }
    }

    printf("ALL PASS (stream)\n");
    return 0;
}
