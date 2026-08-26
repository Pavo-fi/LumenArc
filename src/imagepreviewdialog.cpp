#include "imagepreviewdialog.h"

#include <cmath>

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "i18n.h"
#include "theme.h"

namespace {
constexpr double kMinScale = 0.02;
constexpr double kMaxScale = 40.0;
}

ImagePreviewDialog::ImagePreviewDialog(const QString &path, QWidget *parent)
    : QDialog(parent), m_path(path)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(lang("图片预览 - ", "Image Preview - ")
                   + QFileInfo(path).fileName());
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    resize(1024, 720);

    m_img.load(m_path);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 画面区自绘（paintEvent）；底部状态栏 + 按钮行
    auto *bottom = new QWidget(this);
    auto *bl = new QHBoxLayout(bottom);
    bl->setContentsMargins(10, 6, 10, 6);
    m_status = new QLabel(bottom);
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    bl->addWidget(m_status, 1);

    // v1.16.0 用户拍板：打开即自适应 + 双击复位，不提供「适应窗口」按钮
    auto *actualBtn = new QPushButton(lang("1:1", "1:1"), bottom);
    connect(actualBtn, &QPushButton::clicked, this, [this, bottom]() {
        // 以视口中心为锚设回 100%
        setScaleAt(1.0, QPointF(width() / 2.0,
                                (height() - bottom->height()) / 2.0));
        update();
    });
    bl->addWidget(actualBtn);
    auto *explBtn = new QPushButton(
        lang("在资源管理器中显示", "Show in Explorer"), bottom);
    connect(explBtn, &QPushButton::clicked, this, [this]() {
#ifdef Q_OS_WIN
        QProcess::startDetached(QStringLiteral("explorer.exe"),
            {QStringLiteral("/select,"), QDir::toNativeSeparators(m_path)});
#endif
    });
    bl->addWidget(explBtn);
    auto *closeBtn = new QPushButton(lang("关闭", "Close"), bottom);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    bl->addWidget(closeBtn);

    lay->addStretch(1);          // 占位：paintEvent 画在整窗，底部条压其上
    lay->addWidget(bottom, 0);
    bottom->setStyleSheet(QStringLiteral("background:%1;").arg(Theme::BgCard));

    fitToWindow();
    updateStatus();
}

void ImagePreviewDialog::preview(const QString &path, QWidget *parent)
{
    auto *dlg = new ImagePreviewDialog(path, parent);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

double ImagePreviewDialog::fitScale() const
{
    if (m_img.isNull())
        return 1.0;
    const double vw = width();
    const double vh = height() - 36;   // 底部条近似高度
    return qMin(vw / m_img.width(), vh / m_img.height());
}

void ImagePreviewDialog::fitToWindow()
{
    m_scale = qBound(kMinScale, fitScale(), kMaxScale);
    const double vw = width();
    const double vh = height() - 36;
    m_offset = QPointF((vw - m_img.width() * m_scale) / 2.0,
                       (vh - m_img.height() * m_scale) / 2.0);
}

void ImagePreviewDialog::setScaleAt(double newScale, const QPointF &anchorVp)
{
    newScale = qBound(kMinScale, newScale, kMaxScale);
    if (m_img.isNull())
        return;
    // 光标下的图像点保持不动：off' = a - (a - off) * (s'/s)
    const double k = newScale / m_scale;
    m_offset = anchorVp - (anchorVp - m_offset) * k;
    m_scale = newScale;
    updateStatus();
}

void ImagePreviewDialog::updateStatus()
{
    if (!m_status)
        return;
    if (m_img.isNull()) {
        m_status->setText(lang("无法加载图片", "Cannot load image"));
        return;
    }
    m_status->setText(QStringLiteral("%1% · %2×%3 · %4")
        .arg(int(m_scale * 100))
        .arg(m_img.width()).arg(m_img.height())
        .arg(QFileInfo(m_path).fileName()));
}

void ImagePreviewDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(18, 19, 22));
    if (m_img.isNull()) {
        p.setPen(QColor(Theme::TextSecond));
        p.drawText(rect(), Qt::AlignCenter,
                   lang("无法加载图片：", "Cannot load: ") + m_path);
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(m_offset);
    p.scale(m_scale, m_scale);
    p.drawImage(0, 0, m_img);
}

void ImagePreviewDialog::wheelEvent(QWheelEvent *e)
{
    if (m_img.isNull())
        return;
    const double steps = e->angleDelta().y() / 120.0;
    if (steps == 0.0)
        return;
    setScaleAt(m_scale * std::pow(1.15, steps), e->position());
    update();
    e->accept();
}

void ImagePreviewDialog::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStart = e->pos();
        m_offsetStart = m_offset;
        setCursor(Qt::ClosedHandCursor);
    }
    QDialog::mousePressEvent(e);
}

void ImagePreviewDialog::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        m_offset = m_offsetStart + (e->pos() - m_dragStart);
        update();
    }
    QDialog::mouseMoveEvent(e);
}

void ImagePreviewDialog::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
    QDialog::mouseReleaseEvent(e);
}

void ImagePreviewDialog::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        fitToWindow();
        updateStatus();
        update();
    }
    QDialog::mouseDoubleClickEvent(e);
}
