/**
 * @file videolistpanel.h
 * @brief 视频列表面板：QDockWidget + QListWidget 拖拽排序
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QDockWidget>
#include <QVector>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QDragEnterEvent;
class QDropEvent;

struct VideoEntry {
    QString filePath;
    qint64 durationMs = 0;
    float fps = 30.0f;
    qint64 timeOffsetMs = 0;
};

/**
 * @brief Left-side dock panel showing video list with drag-to-reorder.
 */
class VideoListPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit VideoListPanel(QWidget *parent = nullptr);

    void addVideo(const QString &path, qint64 durationMs, float fps);
    void updateDuration(const QString &path, qint64 durationMs);
    void removeVideo(int index);
    void clearVideos();
    int videoCount() const;
    VideoEntry videoAt(int index) const;
    QVector<VideoEntry> allVideos() const;
    int currentIndex() const;
    QListWidget *listWidget() const;

signals:
    void videoSelected(int index);
    void videoCountChanged(int count);
    /// Emitted after drag-reorder changes the video sequence. (B5)
    void videoReordered();

private slots:
    void onItemDoubleClicked(QListWidgetItem *item);
    void onAddClicked();
    void onRemoveClicked();
    void onClearClicked();
    void onItemMoved();

private:
    void updateStatusLabel();
    QString formatDuration(qint64 ms) const;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    QListWidget *m_listWidget = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QVector<VideoEntry> m_videos;
    int m_currentIndex = -1;
};
