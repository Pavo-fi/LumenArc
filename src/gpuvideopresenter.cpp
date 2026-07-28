#include "gpuvideopresenter.h"

#ifdef Q_OS_WIN

#include <QFile>
#include <QDebug>
#include <rhi/qrhi.h>

using Microsoft::WRL::ComPtr;

GpuVideoPresenter::GpuVideoPresenter(QWidget *parent)
    : QRhiWidget(parent)
{
    setApi(Api::Direct3D11);   // M1 仅 D3D11；Qt 创建失败时 QRhiWidget 不渲染（上层回退）
}

GpuVideoPresenter::~GpuVideoPresenter()
{
    dropImports();
}

void GpuVideoPresenter::setFrame(const GpuFrameInfo &info)
{
    m_pending = info;
    m_hasPending = true;
    if (m_engine)
        m_engine->ackFrame();   // 归还配额（与 VideoWidget::onFrameReady 同义）
    update();
}

void GpuVideoPresenter::clearFrame()
{
    m_hasFrame = false;
    m_hasPending = false;
    dropImports();
    update();
}

void GpuVideoPresenter::dropImports()
{
    for (auto &s : m_slots) {
        s.srb.reset();
        s.rhiTex.reset();
        s.mutex.Reset();
        s.tex.Reset();
        s.handle = 0;
    }
}

static QShader loadQsb(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "GpuVideoPresenter: cannot open shader" << path;
        return QShader();
    }
    return QShader::fromSerialized(f.readAll());
}

void GpuVideoPresenter::initialize(QRhiCommandBuffer *)
{
    m_rhi = rhi();
    if (!m_rhi || m_rhi->backend() != QRhi::D3D11)
        return;

    // 取 Qt 的 D3D11 设备，用于导入引擎的共享纹理
    auto *nh = static_cast<const QRhiD3D11NativeHandles *>(m_rhi->nativeHandles());
    if (!nh || !nh->dev)
        return;
    m_dev = static_cast<ID3D11Device *>(nh->dev);

    if (!m_ubuf) {
        m_ubuf.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 64));
        m_ubuf->create();
    }
    if (!m_sampler) {
        m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                          QRhiSampler::None,
                                          QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        m_sampler->create();
    }
    if (!m_pipe) {
        m_pipe.reset(m_rhi->newGraphicsPipeline());
        m_pipe->setTopology(QRhiGraphicsPipeline::Triangles);
        QShader vs = loadQsb(QStringLiteral(":/shaders/video.vert.qsb"));
        QShader fs = loadQsb(QStringLiteral(":/shaders/video.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) {
            m_pipe.reset();
            return;
        }
        m_pipe->setShaderStages({ { QRhiShaderStage::Vertex, vs },
                                  { QRhiShaderStage::Fragment, fs } });
        // 绑定布局：槽位的 srb 各自创建，pipeline 只取布局。
        // probeSrb/probeTex 仅用于提取兼容布局，用后释放
        auto *probeTex = m_rhi->newTexture(QRhiTexture::BGRA8, {1, 1}, 1, {});
        probeTex->create();
        auto *probeSrb = m_rhi->newShaderResourceBindings();
        probeSrb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                     m_ubuf.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      probeTex, m_sampler.get()),
        });
        probeSrb->create();
        m_pipe->setShaderResourceBindings(probeSrb);
        m_pipe->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
        if (!m_pipe->create()) {
            qWarning() << "GpuVideoPresenter: pipeline create failed";
            m_pipe.reset();
        }
        delete probeSrb;
        delete probeTex;
    }
}

bool GpuVideoPresenter::ensureImported(int slot, const GpuFrameInfo &info)
{
    Slot &s = m_slots[slot];
    if (s.handle == info.sharedHandle && s.rhiTex && s.srb)
        return true;

    s.srb.reset();
    s.rhiTex.reset();
    s.mutex.Reset();
    s.tex.Reset();
    s.handle = 0;

    if (!m_dev || !info.sharedHandle)
        return false;

    if (FAILED(m_dev->OpenSharedResource(reinterpret_cast<HANDLE>(info.sharedHandle),
                                         IID_PPV_ARGS(&s.tex))) || !s.tex) {
        qWarning() << "GpuVideoPresenter: OpenSharedResource failed";
        return false;
    }
    if (FAILED(s.tex->QueryInterface(IID_PPV_ARGS(&s.mutex))) || !s.mutex)
        return false;

    QRhiTexture::NativeTexture nt{};
    nt.object = reinterpret_cast<quint64>(s.tex.Get());
    nt.layout = 0;
    s.rhiTex.reset(m_rhi->newTexture(QRhiTexture::BGRA8, info.size, 1, {}));
    if (!s.rhiTex->createFrom(nt)) {
        qWarning() << "GpuVideoPresenter: texture import failed";
        return false;
    }

    s.srb.reset(m_rhi->newShaderResourceBindings());
    s.srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 m_ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  s.rhiTex.get(), m_sampler.get()),
    });
    if (!s.srb->create())
        return false;

    s.handle = info.sharedHandle;
    return true;
}

void GpuVideoPresenter::render(QRhiCommandBuffer *cb)
{
    if (!m_rhi || !m_pipe)
        return;

    if (m_hasPending) {
        if (ensureImported(m_pending.slot, m_pending)) {
            m_curSlot = m_pending.slot;
            m_frameSize = m_pending.size;
            m_hasFrame = true;
        }
        m_hasPending = false;
    }

    QRhiResourceUpdateBatch *u = m_rhi->nextResourceUpdateBatch();
    const QMatrix4x4 clip = m_rhi->clipSpaceCorrMatrix();
    u->updateDynamicBuffer(m_ubuf.get(), 0, 64, clip.constData());

    QRhiRenderTarget *rt = renderTarget();
    cb->beginPass(rt, QColor(10, 10, 10), { 1.0f, 0 }, u);

    if (m_hasFrame) {
        Slot &s = m_slots[m_curSlot];
        // 与引擎（生产方）的 keyed-mutex 约定：生产 acquire 0 / release 1，消费反向
        HRESULT hr = s.mutex->AcquireSync(1, 100);
        if (SUCCEEDED(hr)) {
            cb->setGraphicsPipeline(m_pipe.get());
            // 信箱式视口：保持画面比例居中
            const QSizeF out = rt->pixelSize();
            float vw = out.width(), vh = out.height();
            if (!m_frameSize.isEmpty()) {
                const float fa = float(m_frameSize.width()) / float(m_frameSize.height());
                const float wa = vw / vh;
                if (wa > fa) vw = vh * fa; else vh = vw / fa;
            }
            cb->setViewport(QRhiViewport((out.width() - vw) / 2.0f,
                                         (out.height() - vh) / 2.0f, vw, vh));
            cb->setShaderResources(s.srb.get());
            cb->draw(3);
            s.mutex->ReleaseSync(0);
        }
    }

    cb->endPass();
}

#endif // Q_OS_WIN
