/**
 * @file videoglwidget.h
 * @brief GPU 视频面（P-29 v1.6.0 Stage 1）：QImage → GL 纹理上传 → 双线性缩放上屏
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-07
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 只替换 VideoWidget::paintEvent 中 painter.drawImage 的全帧 CPU 缩放段
 * （1440p ~10ms/帧背压 → GPU 光栅化，CPU 侧仅纹理上传 ~1ms）。
 * 帧语义（旋转/LUT/快照融合）全部继承 VideoWidget 现有 CPU 链（Q2 拍板），
 * 证据链"所见即所得"零变化。降级红线（§4.6）：initializeGL 失败 →
 * glFailed 信号 → VideoWidget 永久回退 CPU 路径，画面照常。
 * 模式复用语谱图面板（spectrogrampanel_enhanced：3.3 Core + 失败降级）。
 */
#pragma once

#include <QOpenGLWidget>
#include <QImage>
#include <QRect>

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>

class GlVideoSurface : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit GlVideoSurface(QWidget *parent = nullptr);
    ~GlVideoSurface() override;

    /// 帧到达（VideoWidget::onFrameReady 转发）：只存 COW 引用置脏，
    /// 纹理上传延迟到 paintGL（隐式 makeCurrent，避免额外上下文切换）
    void presentFrame(const QImage &frame);

    /// 截图融合叠加（低频；参数/图像变化时重传）。
    /// opacity ∈ [0,1]；与 CPU 路径同一 Porter-Duff source-over 语义（shader 内 mix）
    void presentSnapshotOverlay(const QImage &snapshot, qreal opacity);
    void clearSnapshotOverlay();

    void clearSurface();

    /// 上屏目标矩形（letterbox，本控件坐标 = VideoWidget 坐标，整 rect 铺满）
    void setDisplayRect(const QRect &r);

    /// 采样：true=GL_LINEAR（默认，视觉优于现状最近邻）/ false=GL_NEAREST
    /// （用户偏好现状锐利感时的一键切换兜底，Q2 拍板项；
    ///  Stage 1 未接设置 UI，留待 v1.6.0 后续任务接线或移除）
    void setFilterLinear(bool linear);

    /// 降级判定：false = GL 不可用（VideoWidget 收到 glFailed 后永久回退 CPU）
    bool glHealthy() const { return m_glOk; }

signals:
    /// GL 初始化失败（offscreen/RDP 无 GPU/老旧驱动）——本进程永久回退
    void glFailed(const QString &reason);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void ensureProgram();          // shader 编译链接（失败 → 降级）
    void ensureQuad();             // 静态单位 quad VAO/VBO
    void uploadFrame();            // 惰性上传（尺寸不变 glTexSubImage2D 原地更新）
    void uploadSnapshot();
    /// 首帧自检验收（安全网）：glReadPixels 回读显示矩形中心像素与帧中心比较，
    /// 不匹配 → false（emit glFailed → 永久回退 CPU，防"黑屏无告警"失效模式）
    bool verifyFirstDraw(const QRect &displayRect, const QImage &frame);

    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLVertexArrayObject *m_vao = nullptr;
    QOpenGLBuffer *m_vbo = nullptr;
    QOpenGLTexture *m_frameTex = nullptr;
    QOpenGLTexture *m_snapTex = nullptr;
    QOpenGLTexture *m_blackTex = nullptr;   // 1×1 黑占位（无叠加纹理时避免空采样器）
    QSize m_frameTexSize;          // 当前帧纹理分配尺寸（变尺寸重建）
    QSize m_snapTexSize;
    QImage m_pendingFrame;         // COW 浅引用（O(1)）
    bool m_frameDirty = false;
    QImage m_pendingSnap;
    bool m_snapDirty = false;
    QRect m_displayRect;
    qreal m_snapOpacity = 0.0;
    bool m_linear = true;
    bool m_glOk = false;
    bool m_glVerified = false;     // 首帧读回自检通过（未过前每帧无叠加时重试）
};