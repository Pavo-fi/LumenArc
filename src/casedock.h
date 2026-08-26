/**
 * @file casedock.h
 * @brief 案件面板（ui 层）：证据树四组 + 徽标 + 右键操作（v1.3.0 M2 任务10）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §2.3/§3-M2 任务10。
 * 案件模式下替代视频列表面板；纯展示组件，案件状态一律经 CaseManager
 * 接口读写（R5/R6，不自行解析 case.json）。
 * 证据树四组：视频（含哈希/校时徽标）/前处理会话（含输出与 sidecar）/
 * 报告/快照。标题栏 ✕ = 退出案件模式（非隐藏面板）。
 */
#pragma once

#include <QDockWidget>
#include <QSet>
#include <QString>

class QTreeWidget;
class QTreeWidgetItem;
class QLabel;
class QPushButton;
class QTimer;
class CaseManager;
struct CaseVideoRef;

class CaseDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit CaseDock(CaseManager *cm, QWidget *parent = nullptr);

    /// 全量重建证据树（caseOpened/videoAdded/哈希完成/校验报告后调用）
    void refreshTree();

    /// v1.7.1：高亮正在播放的案件条目（视频/产物按 originalPath 匹配；
    /// 空路径清除高亮）
    void setCurrentVideoPath(const QString &videoPath);

signals:
    /// 双击视频/前处理输出 → MainWindow openVideoFile
    void openVideoRequested(const QString &path);
    /// 标题栏 ✕ / 树内「关闭案件」→ MainWindow 关闭流程（dirty 提示）
    void closeCaseRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QTreeWidgetItem *m_currentHighlight = nullptr;   // v1.7.1：正在播放条目
    QString m_currentVideoPath;                       // 高亮路径（refreshTree 重建后重刷）

private slots:
    void onItemDoubleClicked(QTreeWidgetItem *item, int column);
    void onContextMenu(const QPoint &pos);
    void onSelectionChanged();
    void onWatchTimer();      // 外部文件变动轮询（资源管理器删除/改名）

private:
    QTreeWidgetItem *addGroup(const QString &text);
    void fillCameraGroups(QTreeWidgetItem *group);   // 机位组分层树（拍板B）
    void fillVideos(QTreeWidgetItem *group);         // （已并入机位组树，保留签名）
    void fillPreprocess(QTreeWidgetItem *group);
    void moveToGroupFlow(const QString &id);         // 移到机位组▸ 流程
    void renameGroupFlow(const QString &groupId);    // 组改名
    void fillReports(QTreeWidgetItem *group);
    void fillSnapshots(QTreeWidgetItem *group);
    /// 哈希徽标：✗缺失 ⚠已变更 ⏳待算 ✓一致（同步快判，与 M1 校验语义一致）
    QString hashBadge(const CaseVideoRef &v, QString *tooltip,
                      QColor *color) const;
    void relocateVideo(const QString &id);
    void deleteVideoFile(const QString &id);   // 删除源文件+案内数据（2026-08）
    void removePreprocessSession(int si);      // 删除会话与文件（2026-08）
    void removePreprocessOutput(int si, int oi);  // 删除输出文件（2026-08）
    void removeCaseFile(QTreeWidgetItem *item);   // 删除 sidecar/报告/快照（2026-08）
    /// 案内引用文件存在性快照（外部变更检测用）
    QSet<QString> buildSnapshot() const;
    void removeVideo(const QString &id);
    void showInExplorer(const QString &path) const;
    /// v1.16.0：快照缩略图（异步加载防卡 UI，完成后经定时器合批刷新）
    void requestThumbnail(const QString &path);
    static bool isImageFilePath(const QString &path);   ///< 图片后缀判定

    QHash<QString, QIcon> m_thumbCache;     ///< 路径 → 缩略图
    QSet<QString> m_thumbLoading;           ///< 在途加载（防重入）
    QTimer *m_thumbTimer = nullptr;         ///< 合批刷新定时器

    CaseManager *m_caseManager = nullptr;   // 不持有（SSOT 在 MainWindow）
    QTreeWidget *m_tree = nullptr;
    QLabel *m_titleLabel = nullptr;
    QPushButton *m_btnDelete = nullptr;     // 删除选中视频（2026-08）
    QTimer *m_watchTimer = nullptr;         // 外部变更轮询（2026-08）
    QSet<QString> m_snapshot;               // 案内引用文件存在性快照
};
