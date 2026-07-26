/**
 * @file videolistpanel.cpp
 * @brief 视频列表面板实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "videolistpanel.h"
#include "i18n.h"

#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileInfo>
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>

VideoListPanel::VideoListPanel(QWidget *parent)
    : QDockWidget(parent)
{
    setWindowTitle(lang("视频列表", "Video List"));
    setFeatures(QDockWidget::DockWidgetMovable);
    setAcceptDrops(true);

    auto *widget = new QWidget(this);
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 4, 4, 4);

    m_listWidget = new QListWidget(this);
    m_listWidget->setDragDropMode(QAbstractItemView::InternalMove);
    m_listWidget->setDefaultDropAction(Qt::MoveAction);
    m_listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_listWidget);

    // Button row
    auto *btnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton(lang("添加", "Add"), this);
    m_removeBtn = new QPushButton(lang("移除", "Remove"), this);
    m_clearBtn = new QPushButton(lang("清空", "Clear"), this);
    btnLayout->addWidget(m_addBtn);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addWidget(m_clearBtn);
    layout->addLayout(btnLayout);

    m_statusLabel = new QLabel(this);
    layout->addWidget(m_statusLabel);

    setWidget(widget);

    connect(m_addBtn, &QPushButton::clicked, this, &VideoListPanel::onAddClicked);
    connect(m_removeBtn, &QPushButton::clicked, this, &VideoListPanel::onRemoveClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &VideoListPanel::onClearClicked);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &VideoListPanel::onItemDoubleClicked);

    // Connect drag-drop reorder
    connect(m_listWidget->model(), &QAbstractItemModel::rowsMoved,
            this, &VideoListPanel::onItemMoved);

    updateStatusLabel();
}

void VideoListPanel::addVideo(const QString &path, qint64 durationMs, float fps)
{
    // B8: Unified duplicate check so all callers (onOpenFile, dropEvent, addClicked)
    // are protected. Use canonicalPath for reliable comparison.
    QString canonical = QFileInfo(path).canonicalFilePath();
    for (const auto &v : m_videos) {
        if (QFileInfo(v.filePath).canonicalFilePath() == canonical)
            return;  // Already in list, silently skip
    }

    VideoEntry entry;
    entry.filePath = path;
    entry.durationMs = durationMs;
    entry.fps = fps;
    m_videos.append(entry);

    QFileInfo fi(path);
    QString label = QString("%1  [%2]  %3fps")
        .arg(fi.fileName())
        .arg(formatDuration(durationMs))
        .arg(fps, 0, 'f', 1);
    auto *item = new QListWidgetItem(label);
    item->setData(Qt::UserRole, m_videos.size() - 1);  // Store original index
    m_listWidget->addItem(item);

    updateStatusLabel();
    emit videoCountChanged(m_videos.size());
}

void VideoListPanel::updateDuration(const QString &path, qint64 durationMs)
{
    for (int i = 0; i < m_videos.size(); ++i) {
        if (m_videos[i].filePath == path) {
            m_videos[i].durationMs = durationMs;
            // B13: Find the list widget item by UserRole instead of row position,
            // so the correct item is found even after drag reordering.
            for (int j = 0; j < m_listWidget->count(); ++j) {
                if (m_listWidget->item(j)->data(Qt::UserRole).toInt() == i) {
                    QFileInfo fi(path);
                    QString label = QString("%1  [%2]  %3fps")
                        .arg(fi.fileName())
                        .arg(formatDuration(durationMs))
                        .arg(m_videos[i].fps, 0, 'f', 1);
                    m_listWidget->item(j)->setText(label);
                    break;
                }
            }
            updateStatusLabel();
            return;
        }
    }
}

void VideoListPanel::removeVideo(int index)
{
    if (index < 0 || index >= m_videos.size())
        return;

    m_videos.removeAt(index);
    delete m_listWidget->takeItem(index);

    // Update UserRole data for remaining items
    for (int i = 0; i < m_listWidget->count(); ++i) {
        m_listWidget->item(i)->setData(Qt::UserRole, i);
    }

    if (m_currentIndex >= m_videos.size())
        m_currentIndex = m_videos.size() - 1;

    // Recalculate timeOffsetMs
    qint64 offset = 0;
    for (auto &v : m_videos) {
        v.timeOffsetMs = offset;
        offset += v.durationMs;
    }

    updateStatusLabel();
    emit videoCountChanged(m_videos.size());
}

void VideoListPanel::clearVideos()
{
    m_videos.clear();
    m_listWidget->clear();
    m_currentIndex = -1;
    updateStatusLabel();
    emit videoCountChanged(0);
}

int VideoListPanel::videoCount() const
{
    return m_videos.size();
}

QListWidget *VideoListPanel::listWidget() const
{
    return m_listWidget;
}

VideoEntry VideoListPanel::videoAt(int index) const
{
    if (index >= 0 && index < m_videos.size())
        return m_videos[index];
    return VideoEntry();
}

QVector<VideoEntry> VideoListPanel::allVideos() const
{
    return m_videos;
}

int VideoListPanel::currentIndex() const
{
    return m_currentIndex;
}

void VideoListPanel::onItemDoubleClicked(QListWidgetItem *item)
{
    int idx = m_listWidget->row(item);
    if (idx >= 0 && idx < m_videos.size()) {
        m_currentIndex = idx;
        emit videoSelected(idx);
    }
}

void VideoListPanel::onAddClicked()
{
    QStringList paths = QFileDialog::getOpenFileNames(this,
        lang("添加视频", "Add Videos"), QString(),
        lang("视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;所有文件 (*)",
             "Video files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All files (*)"));

    for (const QString &path : paths) {
        // Avoid duplicates
        bool exists = false;
        for (const auto &v : m_videos) {
            if (v.filePath == path) { exists = true; break; }
        }
        if (!exists) {
            // Duration and FPS will be set by MainWindow after adding
            addVideo(path, 0, 30.0f);
        }
    }
}

void VideoListPanel::onRemoveClicked()
{
    int row = m_listWidget->currentRow();
    if (row >= 0)
        removeVideo(row);
}

void VideoListPanel::onClearClicked()
{
    clearVideos();
}

void VideoListPanel::onItemMoved()
{
    // B5: Rebuild m_videos to match the new QListWidget order.
    // UserRole is kept in sync with m_videos index by addVideo/removeVideo,
    // so we can safely use it as a direct index into m_videos.
    QVector<VideoEntry> newOrder;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        auto *item = m_listWidget->item(i);
        int idx = item->data(Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_videos.size()) {
            newOrder.append(m_videos[idx]);
        }
    }
    m_videos = newOrder;

    // Recalculate timeOffsetMs based on new order
    qint64 offset = 0;
    for (auto &v : m_videos) {
        v.timeOffsetMs = offset;
        offset += v.durationMs;
    }

    // Re-sync UserRole to match the rebuilt m_videos indices
    for (int i = 0; i < m_listWidget->count(); ++i) {
        m_listWidget->item(i)->setData(Qt::UserRole, i);
    }

    // B5: Notify MainWindow that the video sequence changed.
    emit videoReordered();

    updateStatusLabel();
}

void VideoListPanel::updateStatusLabel()
{
    qint64 totalDuration = 0;
    for (const auto &v : m_videos)
        totalDuration += v.durationMs;

    m_statusLabel->setText(QString(lang("共 %1 个视频 | 总时长 %2", "%1 videos | Total %2"))
        .arg(m_videos.size())
        .arg(formatDuration(totalDuration)));
}

QString VideoListPanel::formatDuration(qint64 ms) const
{
    if (ms <= 0) return "00:00";
    int totalSec = static_cast<int>(ms / 1000);
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

void VideoListPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        // Accept if any URL is a video file
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile()) {
                QString path = url.toLocalFile();
                QString ext = QFileInfo(path).suffix().toLower();
                if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov" ||
                    ext == "wmv" || ext == "flv" || ext == "webm" || ext == "m4v" ||
                    ext == "mpg" || ext == "mpeg" || ext == "ts") {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
    }
    event->ignore();
}

void VideoListPanel::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime->hasUrls()) {
        event->ignore();
        return;
    }

    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile()) {
            QString path = url.toLocalFile();
            QString ext = QFileInfo(path).suffix().toLower();
            if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov" ||
                ext == "wmv" || ext == "flv" || ext == "webm" || ext == "m4v" ||
                ext == "mpg" || ext == "mpeg" || ext == "ts") {
                // Check duplicate
                bool exists = false;
                for (const auto &v : m_videos) {
                    if (v.filePath == path) { exists = true; break; }
                }
                if (!exists) {
                    addVideo(path, 0, 30.0f);
                }
            }
        }
    }
    event->acceptProposedAction();
}
