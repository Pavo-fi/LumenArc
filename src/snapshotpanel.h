#pragma once

#include <QDockWidget>
#include <QSlider>
#include <QPushButton>
#include <QImage>
#include <QLabel>

/**
 * @brief Dock panel for frame snapshot capture and image fusion.
 *
 * Captures the current video frame, then blends it as an overlay
 * with adjustable brightness, contrast, and opacity.
 */
class SnapshotPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit SnapshotPanel(QWidget *parent = nullptr);

    void setSnapshot(const QImage &img);
    const QImage &snapshotImage() const { return m_snapshot; }
    bool hasSnapshot() const { return !m_snapshot.isNull(); }

    int brightness() const { return m_brightnessSlider->value(); }
    qreal contrast() const { return 1.0 + m_contrastSlider->value() / 100.0; }
    qreal opacity() const { return m_opacitySlider->value() / 100.0; }

signals:
    void snapshotChanged();
    void captureRequested();
    void clearRequested();

private:
    QImage m_snapshot;
    QSlider *m_brightnessSlider;
    QSlider *m_contrastSlider;
    QSlider *m_opacitySlider;
    QLabel *m_previewLabel;
};

