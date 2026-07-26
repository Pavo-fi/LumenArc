#include "snapshotpanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

SnapshotPanel::SnapshotPanel(QWidget *parent)
    : QDockWidget("Snapshot Fusion", parent)
{
    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);

    // Capture / Clear buttons
    auto *btnLayout = new QHBoxLayout();
    auto *captureBtn = new QPushButton("Capture Frame");
    auto *clearBtn = new QPushButton("Clear");
    btnLayout->addWidget(captureBtn);
    btnLayout->addWidget(clearBtn);
    layout->addLayout(btnLayout);

    connect(captureBtn, &QPushButton::clicked, this, &SnapshotPanel::captureRequested);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_snapshot = QImage();
        emit clearRequested();
    });

    // Brightness
    layout->addWidget(new QLabel("Brightness"));
    m_brightnessSlider = new QSlider(Qt::Horizontal);
    m_brightnessSlider->setRange(-100, 100);
    m_brightnessSlider->setValue(0);
    layout->addWidget(m_brightnessSlider);

    // Contrast
    layout->addWidget(new QLabel("Contrast"));
    m_contrastSlider = new QSlider(Qt::Horizontal);
    m_contrastSlider->setRange(-100, 100);
    m_contrastSlider->setValue(0);
    layout->addWidget(m_contrastSlider);

    // Opacity
    layout->addWidget(new QLabel("Opacity"));
    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(50);
    layout->addWidget(m_opacitySlider);

    // Connect slider changes
    auto emitChanged = [this]() { emit snapshotChanged(); };
    connect(m_brightnessSlider, &QSlider::valueChanged, this, emitChanged);
    connect(m_contrastSlider, &QSlider::valueChanged, this, emitChanged);
    connect(m_opacitySlider, &QSlider::valueChanged, this, emitChanged);

    setWidget(container);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    setVisible(false);
}

void SnapshotPanel::setSnapshot(const QImage &img)
{
    m_snapshot = img.copy();
    emit snapshotChanged();
}

