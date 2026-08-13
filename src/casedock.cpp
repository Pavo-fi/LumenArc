/**
 * @file casedock.cpp
 * @brief 案件面板实现：证据树/徽标/右键（重定位/移除/算指纹/复制指纹/资源管理器）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "casedock.h"

#include <QAction>
#include <QApplication>
#include <QAbstractButton>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "app/case_manager.h"
#include "casedialogs.h"
#include "i18n.h"
#include "theme.h"

namespace {
/// item UserRole 载荷类型
constexpr int kRoleKind = Qt::UserRole;      // "video" / "output" / "file"
constexpr int kRoleId   = Qt::UserRole + 1;  // V### / P###（video/output）
constexpr int kRolePath = Qt::UserRole + 2;  // 绝对路径
} // namespace

CaseDock::CaseDock(CaseManager *cm, QWidget *parent)
    : QDockWidget(parent)
    , m_caseManager(cm)
{
    setObjectName(QStringLiteral("caseDock"));
    setWindowTitle(lang("案件", "Case"));
    setFeatures(QDockWidget::DockWidgetClosable
                | QDockWidget::DockWidgetMovable);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    auto *host = new QWidget(this);
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(6, 6, 6, 6);
    lay->setSpacing(4);

    m_titleLabel = new QLabel(host);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-weight:bold; color:%1;").arg(Theme::TextPrimary));
    m_titleLabel->setWordWrap(true);
    lay->addWidget(m_titleLabel);

    m_tree = new QTreeWidget(host);
    m_tree->setHeaderHidden(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setStyleSheet(QStringLiteral(
        "QTreeWidget { background:%1; border:1px solid %2; border-radius:6px; }"
        "QTreeWidget::item { padding:2px; }")
        .arg(Theme::BgCard, Theme::Border));
    lay->addWidget(m_tree, 1);
    setWidget(host);

    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &CaseDock::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &CaseDock::onContextMenu);
}

void CaseDock::closeEvent(QCloseEvent *event)
{
    // ✕ = 退出案件模式（任务10 模式出口一），非单纯隐藏面板；
    // 是否真正关闭由 MainWindow 关闭流程决定（dirty 提示可取消）
    event->ignore();
    emit closeCaseRequested();
}

QTreeWidgetItem *CaseDock::addGroup(const QString &text)
{
    auto *g = new QTreeWidgetItem(m_tree, {text});
    g->setFlags(Qt::ItemIsEnabled);   // 组头不可选
    g->setForeground(0, QColor(Theme::TextSecond));
    QFont f = g->font(0);
    f.setBold(true);
    g->setFont(0, f);
    return g;
}

QString CaseDock::hashBadge(const CaseVideoRef &v, QString *tooltip,
                            QColor *color) const
{
    // 完整包接收端：原路径缺失时以包内 sources/ 副本为准（M3 任务12）
    const QString eff = m_caseManager->effectivePathFor(v);
    const QFileInfo fi(eff);
    const bool bundled = (eff != v.originalPath);
    if (!fi.exists()) {
        if (tooltip) *tooltip = lang("源文件缺失（右键可重新定位）",
                                     "Source missing (right-click to relocate)");
        if (color) *color = QColor(Theme::Danger);
        return QStringLiteral("✗");
    }
    if (!bundled
        && (fi.size() != v.sizeBytes
            || fi.lastModified().toMSecsSinceEpoch() != v.mtimeMs)) {
        if (tooltip) *tooltip = lang("源文件已变更（大小/时间不符），指纹将重算",
                                     "Source changed (size/mtime), will re-hash");
        if (color) *color = QColor(Theme::Accent);
        return QStringLiteral("⚠");
    }
    if (v.sha256.isEmpty()) {
        if (tooltip) *tooltip = lang("指纹待计算（后台队列）",
                                     "Fingerprint pending (background queue)");
        if (color) *color = QColor(Theme::TextMuted);
        return QStringLiteral("⏳");
    }
    if (tooltip) *tooltip = bundled
        ? lang("包内副本，指纹一致", "Bundled copy, fingerprint verified")
        : lang("指纹一致", "Fingerprint verified");
    if (color) *color = QColor(Theme::Success);
    return QStringLiteral("✓");
}

void CaseDock::fillVideos(QTreeWidgetItem *group)
{
    for (const auto &v : m_caseManager->meta().videos) {
        QString badgeTip;
        QColor badgeColor;
        const QString badge = hashBadge(v, &badgeTip, &badgeColor);
        const QString calMark = v.hasCalibration
            ? QStringLiteral(" ⏰") : QString();
        // 有效路径：包内副本兜底（双击直接可播，完整包零操作）
        const QString eff = m_caseManager->effectivePathFor(v);
        const bool bundled = (eff != v.originalPath);
        const QString name = bundled
            ? QStringLiteral("📦") + QFileInfo(eff).fileName()
            : QFileInfo(v.originalPath).fileName();
        auto *it = new QTreeWidgetItem(group,
            {QStringLiteral("%1  %2%3  %4")
                 .arg(v.id, name, calMark, badge)});
        it->setData(0, kRoleKind, QStringLiteral("video"));
        it->setData(0, kRoleId, v.id);
        it->setData(0, kRolePath, eff);
        it->setForeground(0, badgeColor);
        QString tip = v.originalPath
            + (bundled ? QStringLiteral("\n") + lang("（原路径缺失，包内副本：%1）",
                                                      "(original missing, bundled: %1)").arg(eff)
                       : QString())
            + QStringLiteral("\n") + badgeTip;
        if (v.hasCalibration && !v.calibrationSummary.isEmpty())
            tip += QStringLiteral("\n") + lang("校时：", "Calibration: ")
                   + v.calibrationSummary;
        it->setToolTip(0, tip);
    }
    group->setText(0, lang("视频（%1）", "Videos (%1)")
                          .arg(m_caseManager->meta().videos.size()));
}

void CaseDock::fillPreprocess(QTreeWidgetItem *group)
{
    const auto &sessions = m_caseManager->meta().preprocessSessions;
    const QDir caseDir(m_caseManager->caseDir());
    for (const auto &p : sessions) {
        auto *sIt = new QTreeWidgetItem(group,
            {QStringLiteral("🗜 %1（%2 输出）")
                 .arg(QFileInfo(p.sessionDirRelPath).fileName())
                 .arg(p.outputRefs.size())});
        sIt->setFlags(Qt::ItemIsEnabled);
        sIt->setForeground(0, QColor(Theme::TextSecond));
        sIt->setData(0, kRoleKind, QStringLiteral("session"));
        sIt->setData(0, kRolePath, caseDir.absoluteFilePath(p.sessionDirRelPath));
        sIt->setToolTip(0, caseDir.absoluteFilePath(p.sessionDirRelPath));
        for (const auto &o : p.outputRefs) {
            auto *oIt = new QTreeWidgetItem(sIt,
                {QStringLiteral("%1  %2")
                     .arg(o.id, QFileInfo(o.originalPath).fileName())});
            oIt->setData(0, kRoleKind, QStringLiteral("output"));
            oIt->setData(0, kRoleId, o.id);
            oIt->setData(0, kRolePath, o.originalPath);
            oIt->setToolTip(0, o.originalPath);
            oIt->setForeground(0, QColor(Theme::TextPrimary));
        }
        for (const QString &sc : p.sidecarRelPaths) {
            auto *cIt = new QTreeWidgetItem(sIt,
                {QStringLiteral("📄 ") + QFileInfo(sc).fileName()});
            cIt->setData(0, kRoleKind, QStringLiteral("file"));
            cIt->setData(0, kRolePath, caseDir.absoluteFilePath(sc));
            cIt->setToolTip(0, caseDir.absoluteFilePath(sc));
            cIt->setForeground(0, QColor(Theme::TextMuted));
        }
    }
    group->setText(0, lang("前处理会话（%1）", "Preprocess (%1)")
                          .arg(sessions.size()));
}

void CaseDock::fillReports(QTreeWidgetItem *group)
{
    const auto &reports = m_caseManager->meta().reports;
    const QDir caseDir(m_caseManager->caseDir());
    for (const QString &r : reports) {
        auto *it = new QTreeWidgetItem(group,
            {QStringLiteral("📑 ") + QFileInfo(r).fileName()});
        it->setData(0, kRoleKind, QStringLiteral("file"));
        it->setData(0, kRolePath, caseDir.absoluteFilePath(r));
        it->setToolTip(0, caseDir.absoluteFilePath(r));
        it->setForeground(0, QColor(Theme::TextPrimary));
    }
    group->setText(0, lang("报告（%1）", "Reports (%1)").arg(reports.size()));
}

void CaseDock::fillSnapshots(QTreeWidgetItem *group)
{
    const QDir dir(m_caseManager->caseDir() + QStringLiteral("/snapshots"));
    const auto files = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const auto &fi : files) {
        auto *it = new QTreeWidgetItem(group,
            {QStringLiteral("📷 ") + fi.fileName()});
        it->setData(0, kRoleKind, QStringLiteral("file"));
        it->setData(0, kRolePath, fi.absoluteFilePath());
        it->setToolTip(0, fi.absoluteFilePath());
        it->setForeground(0, QColor(Theme::TextPrimary));
    }
    group->setText(0, lang("快照（%1）", "Snapshots (%1)").arg(files.size()));
}

void CaseDock::refreshTree()
{
    if (!m_caseManager || !m_caseManager->isOpen())
        return;
    m_titleLabel->setText(QStringLiteral("📁 %1\n%2")
        .arg(m_caseManager->meta().caseNo + QStringLiteral("-")
             + m_caseManager->meta().title,
             m_caseManager->caseDir()));
    setWindowTitle(lang("案件：%1", "Case: %1")
                       .arg(m_caseManager->meta().caseNo));
    m_tree->clear();
    fillVideos(addGroup(QString()));
    fillPreprocess(addGroup(QString()));
    fillReports(addGroup(QString()));
    fillSnapshots(addGroup(QString()));
    m_tree->expandAll();
}

void CaseDock::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;
    const QString kind = item->data(0, kRoleKind).toString();
    const QString path = item->data(0, kRolePath).toString();
    if ((kind == QLatin1String("video") || kind == QLatin1String("output"))
        && !path.isEmpty())
        emit openVideoRequested(path);
}

void CaseDock::showInExplorer(const QString &path) const
{
    // Windows：explorer /select 定位到文件；其余平台打开所在目录
#ifdef Q_OS_WIN
    QProcess::startDetached(QStringLiteral("explorer.exe"),
        {QStringLiteral("/select,"),
         QDir::toNativeSeparators(path)});
#else
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

void CaseDock::relocateVideo(const QString &id)
{
    // 共用交互（casedialogs）：大小不一致默认拒绝，显式「仍要采用」留档
    if (relocateVideoInteractive(m_caseManager, id, this))
        refreshTree();
}

void CaseDock::removeVideo(const QString &id)
{
    const auto *v = m_caseManager->videoById(id);
    if (!v)
        return;
    // 移除默认不删数据（拍板§8-5）：三钮 —— 保留案内数据 / 一并删除 / 取消
    QMessageBox box(this);
    box.setWindowTitle(lang("移除视频", "Remove video"));
    box.setText(lang("将 %1（%2）移出案件？\n案内 .vla 分析数据与校时证据帧默认保留。",
                     "Remove %1 (%2) from the case?\nIn-case .vla and calibration "
                     "evidence are kept by default.")
                    .arg(id, QFileInfo(v->originalPath).fileName()));
    QAbstractButton *btnKeep = box.addButton(lang("移除（保留案内数据）", "Remove (keep data)"),
                                  QMessageBox::AcceptRole);
    QAbstractButton *btnDelete = box.addButton(lang("一并删除案内数据", "Remove + delete data"),
                                    QMessageBox::DestructiveRole);
    box.addButton(lang("取消", "Cancel"), QMessageBox::RejectRole);
    box.exec();
    const bool del = (box.clickedButton() == btnDelete);
    if (box.clickedButton() != btnKeep && !del)
        return;
    QString err;
    if (!m_caseManager->removeVideo(id, del, &err))
        QMessageBox::warning(this, lang("移除失败", "Remove failed"), err);
    refreshTree();
}

void CaseDock::onContextMenu(const QPoint &pos)
{
    auto *item = m_tree->itemAt(pos);
    if (!item)
        return;
    const QString kind = item->data(0, kRoleKind).toString();
    const QString id = item->data(0, kRoleId).toString();
    const QString path = item->data(0, kRolePath).toString();
    if (kind.isEmpty() || path.isEmpty())
        return;

    QMenu menu(this);
    if (kind == QLatin1String("video")) {
        const auto *v = m_caseManager->videoById(id);
        menu.addAction(lang("打开播放", "Open & play"), this, [this, path]() {
            emit openVideoRequested(path);
        });
        menu.addSeparator();
        menu.addAction(lang("重新定位…", "Relocate…"), this,
                       [this, id]() { relocateVideo(id); });
        menu.addAction(lang("移除出案件…", "Remove from case…"), this,
                       [this, id]() { removeVideo(id); });
        menu.addSeparator();
        menu.addAction(lang("计算指纹", "Compute fingerprint"), this,
                       [this, id]() {
                           m_caseManager->queueHashFor(id);
                           refreshTree();   // ⏳ 立即可见
                       });
        auto *copyAct = menu.addAction(lang("复制指纹", "Copy fingerprint"), this,
                                       [this, v]() {
                                           QApplication::clipboard()->setText(
                                               v ? v->sha256 : QString());
                                       });
        copyAct->setEnabled(v && !v->sha256.isEmpty());
        menu.addSeparator();
        menu.addAction(lang("在资源管理器中显示", "Show in Explorer"), this,
                       [this, path]() { showInExplorer(path); });
    } else if (kind == QLatin1String("output")) {
        menu.addAction(lang("打开播放", "Open & play"), this, [this, path]() {
            emit openVideoRequested(path);
        });
        menu.addAction(lang("在资源管理器中显示", "Show in Explorer"), this,
                       [this, path]() { showInExplorer(path); });
    } else {   // session / file
        menu.addAction(lang("在资源管理器中显示", "Show in Explorer"), this,
                       [this, path]() { showInExplorer(path); });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}
