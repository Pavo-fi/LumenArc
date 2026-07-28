#ifndef GPUVIDEOPRESENTER_H
#define GPUVIDEOPRESENTER_H

/**
 * @file gpuvideopresenter.h
 * @brief GPU 零拷贝视频显示组件（实验，v1.6）
 *
 * QRhiWidget（固定 D3D11 后端）：导入引擎发来的 keyed-mutex 共享 BGRA 纹理，
 * 直接采样上屏——消除 GPU→CPU 回传 + CPU sws + QWidget 光栅。
 * 任何一步失败都保持无帧状态，由上层回退到 QImage/QWidget 路径（兼容性红线）。
 */

#include "infrastructure/ivideo_engine.h"

#ifdef Q_OS_WIN

#include <QRhiWidget>
#include <QSize>
#include <memory>
#include <wrl/client.h>
#include <d3d11.h>

class QRhi;
class QRhiGraphicsPipeline;
class QRhiShaderResourceBindings;
class QRhiSampler;
class QRhiBuffer;
class QRhiTexture;

class GpuVideoPresenter : public QRhiWidget
{
    Q_OBJECT
public:
    explicit GpuVideoPresenter(QWidget *parent = nullptr);
    ~GpuVideoPresenter() override;

    void setEngine(IVideoEngine *engine) { m_engine = engine; }

    /// 引擎（工作线程）→ 本组件（UI 线程，QueuedConnection）的最新 GPU 帧
    void setFrame(const GpuFrameInfo &info);
    void clearFrame();
    bool hasFrame() const { return m_hasFrame; }
    QSize frameSize() const { return m_frameSize; }

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;

private:
    struct Slot {
        quint64 handle = 0;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutex;
        std::unique_ptr<QRhiTexture> rhiTex;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
    };

    bool ensureImported(int slot, const GpuFrameInfo &info);
    void dropImports();

    IVideoEngine *m_engine = nullptr;
    QRhi *m_rhi = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> m_dev;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipe;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiBuffer> m_ubuf;
    Slot m_slots[2];
    GpuFrameInfo m_pending;
    bool m_hasPending = false;   // 收到新帧待导入/切换（UI 线程独占）
    bool m_hasFrame = false;
    int m_curSlot = 0;
    QSize m_frameSize;
};

#endif // Q_OS_WIN
#endif // GPUVIDEOPRESENTER_H
