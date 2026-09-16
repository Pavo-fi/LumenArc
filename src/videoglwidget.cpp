/**
 * @file videoglwidget.cpp
 * @brief GPU 视频面实现（P-29 v1.6.0 Stage 1）
 *
 * 每帧成本：CPU 侧仅纹理上传（1440p ≈11MB，PCIe ~1ms）；
 * 缩放在 GPU 光栅化阶段（双线性），替代 CPU 光栅 ~10ms/帧。
 * 纹理复用：帧尺寸不变 → 纹理对象存续（glTexImage2D 原地更新数据，
 * 不 create/destroy），变尺寸才重建。
 * 行对齐：QImage 转换后行打包（bytesPerLine = w×3），无需 UNPACK_ROW_LENGTH。
 * 降级红线：initializeGL 失败 → glFailed（VideoWidget 永久回退 CPU），
 * 本控件随即被隐藏；paintGL 在 !m_glOk 时直接返回。
 */
#include "videoglwidget.h"

#include <QOpenGLContext>
#include <QtMath>
#include <cstddef>   // offsetof（quad 顶点属性偏移）

namespace {
// 单位 quad（clip space）+ UV；顶点着色器内做 Y 翻转（QImage 顶左原点 → GL 底左）
static const char *kVertexShader = R"(
#version 330 core
in vec4 position;
in vec2 texCoord;
uniform vec4 u_rect;   // clip space：xy=中心，zw=半尺寸
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(u_rect.xy + position.xy * u_rect.zw, 0.0, 1.0);
    vTexCoord = vec2(texCoord.x, 1.0 - texCoord.y);
}
)";

static const char *kFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
uniform sampler2D u_frame;
uniform sampler2D u_snap;
uniform float u_snapAlpha;
out vec4 fragColor;
void main() {
    vec4 c = texture(u_frame, vTexCoord);
    if (u_snapAlpha > 0.0) {
        vec4 s = texture(u_snap, vTexCoord);
        c = mix(c, s, u_snapAlpha);
    }
    fragColor = c;
}
)";

// 近黑视频底色——与 VideoWidget::paintEvent 的 fillRect(16,18,22) 一致（letterbox 无接缝）
constexpr float kBgR = 16.0f / 255.0f;
constexpr float kBgG = 18.0f / 255.0f;
constexpr float kBgB = 22.0f / 255.0f;

struct QuadVertex {
    float x, y;
    float u, v;
};

// 单位 quad 两个三角形（core profile 安全，不用 GL_QUADS）
static const QuadVertex kQuad[] = {
    {-1.0f, -1.0f, 0.0f, 0.0f}, {1.0f, -1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f},
    {-1.0f, -1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {-1.0f, 1.0f, 0.0f, 1.0f},
};
}   // namespace

GlVideoSurface::GlVideoSurface(QWidget *parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(format);
    setAutoFillBackground(false);
}

GlVideoSurface::~GlVideoSurface()
{
    // 全部显式删（纹理/缓冲无 parent 参数可用）；有上下文时先 makeCurrent
    if (context())
        makeCurrent();
    delete m_frameTex;
    delete m_snapTex;
    delete m_blackTex;
    delete m_program;
    delete m_vao;
    delete m_vbo;
    if (context())
        doneCurrent();
}

void GlVideoSurface::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        m_glOk = false;
        emit glFailed(QStringLiteral("initializeOpenGLFunctions() failed (no usable GL 3.3 core context)"));
        return;
    }
    ensureProgram();
    ensureQuad();
    if (!m_program || !m_vao) {
        m_glOk = false;
        emit glFailed(QStringLiteral("GL program/quad creation failed"));
        return;
    }
    // 1×1 黑占位纹理（无截图叠加时绑 unit 1，避免 shader 空采样器 UB）
    m_blackTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
    if (m_blackTex->create()) {
        m_blackTex->bind();
        const unsigned char black[3] = {0, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, black);
        m_blackTex->release();
    } else {
        delete m_blackTex;
        m_blackTex = nullptr;
    }
    m_glOk = true;
}

void GlVideoSurface::resizeGL(int w, int h)
{
    // QOpenGLWidget 的 FBO 是物理像素（×dpr）：viewport 必须用物理尺寸，
    // 否则 HiDPI（dpr≠1）下视频缩至 1/dpr² 且 glClear 只覆盖左上角
    // （语谱图面板同款处理，spectrogrampanel_enhanced resizeGL/paintGL）。
    glViewport(0, 0, w, h);
}

void GlVideoSurface::ensureProgram()
{
    m_program = new QOpenGLShaderProgram(this);
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)) {
        qWarning() << "[GlVideoSurface]" << m_program->log();
        delete m_program;
        m_program = nullptr;
        return;
    }
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader)) {
        qWarning() << "[GlVideoSurface]" << m_program->log();
        delete m_program;
        m_program = nullptr;
        return;
    }
    if (!m_program->link()) {
        qWarning() << "[GlVideoSurface]" << m_program->log();
        delete m_program;
        m_program = nullptr;
    }
}

void GlVideoSurface::ensureQuad()
{
    m_vao = new QOpenGLVertexArrayObject(this);
    m_vao->create();
    m_vao->bind();
    m_vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);   // 非 QObject，析构显式删
    m_vbo->create();
    m_vbo->allocate(kQuad, sizeof(kQuad));
    const int stride = static_cast<int>(sizeof(QuadVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(offsetof(QuadVertex, u)));
    m_vao->release();
}

void GlVideoSurface::presentFrame(const QImage &frame)
{
    m_pendingFrame = frame;   // COW 浅拷贝（O(1)）
    m_frameDirty = true;
    update();
}

void GlVideoSurface::presentSnapshotOverlay(const QImage &snapshot, qreal opacity)
{
    // 图像身份去重：opacity 不在纹理里（uniform 每次 paint 取新值），
    // 只有图像本体变化才需重传（防每帧 11MB 级重上传）
    const bool imgChanged = (snapshot.constBits() != m_pendingSnap.constBits())
        || snapshot.size() != m_pendingSnap.size();
    m_pendingSnap = snapshot;
    m_snapOpacity = qBound(0.0, opacity, 1.0);
    // 边缘：某快照首次 present 恰逢 alpha=0（融合滑杆=100，纹理从未上传），
    // 之后拖回 <100 时 imgChanged=false 不会重传 → 黑占位；该情形强制重传
    m_snapDirty = imgChanged || (m_snapOpacity > 0.0f && !m_snapTex);
    update();
}

void GlVideoSurface::clearSnapshotOverlay()
{
    m_pendingSnap = QImage();
    m_snapOpacity = 0.0;
    m_snapDirty = true;
    update();
}

void GlVideoSurface::clearSurface()
{
    m_pendingFrame = QImage();
    m_frameDirty = false;
    clearSnapshotOverlay();
}

void GlVideoSurface::setDisplayRect(const QRect &r)
{
    if (m_displayRect == r)
        return;
    m_displayRect = r;
    update();
}

void GlVideoSurface::setFilterLinear(bool linear)
{
    if (m_linear == linear)
        return;
    m_linear = linear;
    const QOpenGLTexture::Filter f =
        m_linear ? QOpenGLTexture::Linear : QOpenGLTexture::Nearest;
    if (m_glOk && m_frameTex)
        m_frameTex->setMinMagFilters(f, f);
    if (m_glOk && m_snapTex)
        m_snapTex->setMinMagFilters(f, f);
}

namespace {
/// 行打包的 RGB888（上传前归一：QImage::convertToFormat 产出 bytesPerLine=w×3）
inline QImage asPackedRgb888(QImage &img)
{
    if (img.format() == QImage::Format_RGB888 && img.bytesPerLine() == img.width() * 3)
        return img;
    return img.convertToFormat(QImage::Format_RGB888);
}
}   // namespace

void GlVideoSurface::uploadFrame()
{
    m_pendingFrame = asPackedRgb888(m_pendingFrame);
    if (m_pendingFrame.isNull())
        return;
    const int w = m_pendingFrame.width();
    const int h = m_pendingFrame.height();
    if (w < 2 || h < 2)
        return;

    if (!m_frameTex || m_frameTexSize != QSize(w, h)) {
        if (m_frameTex)
            m_frameTex->destroy();
        m_frameTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
        if (!m_frameTex->create()) {
            delete m_frameTex;
            m_frameTex = nullptr;
            return;
        }
        m_frameTexSize = QSize(w, h);
    }
    m_frameTex->bind();
    // 同尺寸帧：纹理对象存续（不 create/destroy），glTexImage2D 等价原地更新
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, m_pendingFrame.constBits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    m_linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    m_linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_frameTex->release();
    m_frameDirty = false;
}

void GlVideoSurface::uploadSnapshot()
{
    m_snapDirty = false;
    if (m_pendingSnap.isNull() || m_snapOpacity <= 0.0f)
        return;
    m_pendingSnap = asPackedRgb888(m_pendingSnap);
    const int w = m_pendingSnap.width();
    const int h = m_pendingSnap.height();
    if (w < 2 || h < 2)
        return;
    if (!m_snapTex || m_snapTexSize != QSize(w, h)) {
        if (m_snapTex)
            m_snapTex->destroy();
        m_snapTex = new QOpenGLTexture(QOpenGLTexture::Target2D);
        if (!m_snapTex->create()) {
            delete m_snapTex;
            m_snapTex = nullptr;
            return;
        }
        m_snapTexSize = QSize(w, h);
    }
    m_snapTex->bind();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, m_pendingSnap.constBits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    m_linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    m_linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_snapTex->release();
}

void GlVideoSurface::paintGL()
{
    if (!m_glOk)
        return;   // 已降级：VideoWidget 收到 glFailed 后隐藏本控件走 CPU 路径

    glClearColor(kBgR, kBgG, kBgB, 1.0f);
    // HiDPI：FBO 为物理像素（×dpr），viewport 用物理尺寸（u_rect 的
    // clip 换算中 dpr 代数相消，逻辑坐标即可，见下方 cx/cy 推导）
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, int(width() * dpr), int(height() * dpr));
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_frameDirty)
        uploadFrame();
    if (m_pendingFrame.isNull() || !m_frameTex)
        return;   // 无帧（控件应处于隐藏态）
    if (m_snapDirty)
        uploadSnapshot();

    const QRect r = m_displayRect.intersected(rect());
    if (r.isEmpty())
        return;

    const double W = width();
    const double H = height();
    const double cx = ((r.x() + r.width() / 2.0) * 2.0 / W) - 1.0;
    const double cy = 1.0 - ((r.y() + r.height() / 2.0) * 2.0 / H);
    const double hw = r.width() / W;
    const double hh = r.height() / H;

    m_program->bind();
    m_program->setUniformValue(m_program->uniformLocation("u_rect"),
                               cx, cy, hw, hh);
    m_program->setUniformValue(m_program->uniformLocation("u_snapAlpha"),
                               float(m_snapOpacity));
    m_frameTex->bind();   // texture unit 0
    m_program->setUniformValue(m_program->uniformLocation("u_frame"), 0);
    if (m_snapTex) {
        m_snapTex->bind(1);
        m_program->setUniformValue(m_program->uniformLocation("u_snap"), 1);
    } else {
        // 无叠加纹理：绑 1×1 黑占位（不能裸 glBindTexture(0)——会误解绑 unit 0 的帧纹理）
        if (m_blackTex)
            m_blackTex->bind(1);
        m_program->setUniformValue(m_program->uniformLocation("u_snap"), 1);
    }

    m_vao->bind();
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_vao->release();
    m_program->release();
    if (m_frameTex)
        m_frameTex->release();
    if (m_snapTex)
        m_snapTex->release();
    if (m_blackTex)
        m_blackTex->release();

    // GL 状态清理（防干扰后续 raster 合成，语谱图面板同款 :288-290）
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}