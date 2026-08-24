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
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
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
constexpr int kRoleIdx  = Qt::UserRole + 3;  // 会话索引/报告索引（2026-08）
constexpr int kRoleIdx2 = Qt::UserRole + 4;  // 输出索引（2026-08）
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

    // 操作行：删除选中视频 + 手动刷新（2026-08 人工反馈：误入视频需显式
    // 删除入口；外部删除文件后需刷新反映）
    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    m_btnDelete = new QPushButton(lang("🗑 删除选中", "🗑 Delete selected"), host);
    m_btnDelete->setEnabled(false);
    m_btnDelete->setToolTip(lang(
        "删除选中的视频：源文件在案件内则一并删除；案件外仅删分析结果（不可恢复）",
        "Delete selected video: source inside case dir is also deleted; "
        "outside sources are kept (irreversible)"));
    auto *btnRefresh = new QPushButton(lang("🔄 刷新", "🔄 Refresh"), host);
    btnRefresh->setToolTip(lang("重新扫描案件目录文件状态",
                                "Re-scan case files"));
    btnRow->addWidget(m_btnDelete);
    btnRow->addStretch(1);
    btnRow->addWidget(btnRefresh);
    lay->addLayout(btnRow);

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
    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &CaseDock::onSelectionChanged);
    connect(m_btnDelete, &QPushButton::clicked, this, [this]() {
        auto *it = m_tree->currentItem();
        if (it && it->data(0, kRoleKind).toString() == QLatin1String("video"))
            deleteVideoFile(it->data(0, kRoleId).toString());
    });
    connect(btnRefresh, &QPushButton::clicked, this, [this]() {
        refreshTree();
        m_snapshot = buildSnapshot();
    });
    // 外部文件变动轮询（资源管理器删除/改名 → 自动刷新；2s 轻量存在性检查）
    m_watchTimer = new QTimer(this);
    m_watchTimer->setInterval(2000);
    connect(m_watchTimer, &QTimer::timeout, this, &CaseDock::onWatchTimer);
    m_watchTimer->start();
    m_snapshot = buildSnapshot();
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

/// 机位组分层树（拍板 B，2026-08-24）：组节点 → 成员文件（V###/P###
/// 同待遇混排）。成员行复用原视频/产物行语义（kind=video/output，
/// 右键菜单兼容）。
void CaseDock::fillCameraGroups(QTreeWidgetItem *group)
{
    const CaseMeta &meta = m_caseManager->meta();
    int totalFiles = 0;
    for (const CaseCameraGroup &g : meta.cameraGroups) {
        const QString gname = CaseModel::groupDisplayName(g);
        auto *gIt = new QTreeWidgetItem(group,
            {QStringLiteral("📷 %1（%2 个文件）")
                 .arg(gname).arg(g.memberIds.size())});
        gIt->setData(0, kRoleKind, QStringLiteral("camgroup"));
        gIt->setData(0, kRoleId, g.groupId);
        gIt->setData(0, kRolePath, g.groupId);   // 右键菜单非空守卫
        gIt->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        gIt->setToolTip(0, g.name.isEmpty()
            ? lang("未命名机位组 %1——右键「机位组改名」",
                   "Unnamed camera group %1 — right-click to rename").arg(g.groupId)
            : QStringLiteral("%1（%2）").arg(g.name, g.groupId));
        QFont f = gIt->font(0);
        f.setBold(true);
        gIt->setFont(0, f);
        for (const QString &mid : g.memberIds) {
            const CaseVideoRef *v = m_caseManager->videoById(mid);
            if (!v)
                continue;
            ++totalFiles;
            QString badgeTip;
            QColor badgeColor;
            const QString badge = hashBadge(*v, &badgeTip, &badgeColor);
            const QString calMark = v->hasCalibration
                ? QStringLiteral(" ⏰") : QString();
            const QString eff = m_caseManager->effectivePathFor(*v);
            const bool bundled = (eff != v->originalPath);
            const QString name = bundled
                ? QStringLiteral("📦") + QFileInfo(eff).fileName()
                : QFileInfo(v->originalPath).fileName();
            const bool isOutput = mid.startsWith(QLatin1Char('P'));
            auto *it = new QTreeWidgetItem(gIt,
                {QStringLiteral("%1  %2%3  %4").arg(v->id, name, calMark, badge)});
            it->setData(0, kRoleKind, isOutput ? QStringLiteral("output")
                                               : QStringLiteral("video"));
            it->setData(0, kRoleId, v->id);
            it->setData(0, kRolePath, eff);
            it->setForeground(0, badgeColor);
            QString tip = v->originalPath
                + QStringLiteral("\n") + lang("机位组：", "Group: ") + gname
                + QStringLiteral("\n") + badgeTip;
            if (v->hasCalibration && !v->calibrationSummary.isEmpty())
                tip += QStringLiteral("\n") + lang("校时：", "Calibration: ")
                       + v->calibrationSummary;
            it->setToolTip(0, tip);
            // output 行的会话/输出索引（删除输出用）
            if (isOutput) {
                const auto &sessions = meta.preprocessSessions;
                for (int si = 0; si < sessions.size(); ++si)
                    for (int oi = 0; oi < sessions[si].outputRefs.size(); ++oi)
                        if (sessions[si].outputRefs[oi].id == mid) {
                            it->setData(0, kRoleIdx, si);
                            it->setData(0, kRoleIdx2, oi);
                        }
            }
        }
    }
    group->setText(0, lang("机位组（%1 组 / %2 文件）", "Camera groups (%1 / %2 files)")
                          .arg(meta.cameraGroups.size()).arg(totalFiles));
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
        const QString cam = v.cameraLabel.isEmpty()
            ? QString() : QStringLiteral(" 📷") + v.cameraLabel;
        const QString name = bundled
            ? QStringLiteral("📦") + QFileInfo(eff).fileName()
            : QFileInfo(v.originalPath).fileName();
        auto *it = new QTreeWidgetItem(group,
            {QStringLiteral("%1  %2%3%4  %5")
                 .arg(v.id, name, calMark, cam, badge)});
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
    for (int si = 0; si < sessions.size(); ++si) {
        const auto &p = sessions[si];
        auto *sIt = new QTreeWidgetItem(group,
            {QStringLiteral("🗜 %1（%2 输出）")
                 .arg(QFileInfo(p.sessionDirRelPath).fileName())
                 .arg(p.outputRefs.size())});
        sIt->setFlags(Qt::ItemIsEnabled);
        sIt->setForeground(0, QColor(Theme::TextSecond));
        sIt->setData(0, kRoleKind, QStringLiteral("session"));
        sIt->setData(0, kRolePath, caseDir.absoluteFilePath(p.sessionDirRelPath));
        sIt->setData(0, kRoleIdx, si);
        sIt->setToolTip(0, caseDir.absoluteFilePath(p.sessionDirRelPath));
        if (!p.outputRefs.isEmpty()) {
            auto *note = new QTreeWidgetItem(sIt,
                {lang("↳ 产物已归入机位组（见上方）",
                      "↳ outputs grouped under camera groups above")});
            note->setFlags(Qt::ItemIsEnabled);
            note->setForeground(0, QColor(Theme::TextMuted));
        }
        for (const QString &sc : p.sidecarRelPaths) {
            auto *cIt = new QTreeWidgetItem(sIt,
                {QStringLiteral("📄 ") + QFileInfo(sc).fileName()});
            cIt->setData(0, kRoleKind, QStringLiteral("file"));
            cIt->setData(0, kRolePath, caseDir.absoluteFilePath(sc));
            cIt->setData(0, kRoleIdx, si);
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
    for (int ri = 0; ri < reports.size(); ++ri) {
        const QString &r = reports[ri];
        auto *it = new QTreeWidgetItem(group,
            {QStringLiteral("📑 ") + QFileInfo(r).fileName()});
        it->setData(0, kRoleKind, QStringLiteral("file"));
        it->setData(0, kRolePath, caseDir.absoluteFilePath(r));
        it->setData(0, kRoleIdx, ri);
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
    // v1.7.1 闪退修复：树全量重建 → 旧高亮指针必然悬空，先置空；
    // 重建完成后按保存的路径重新应用高亮
    m_currentHighlight = nullptr;
    m_titleLabel->setText(QStringLiteral("📁 %1\n%2")
        .arg(m_caseManager->meta().caseNo + QStringLiteral("-")
             + m_caseManager->meta().title,
             m_caseManager->caseDir()));
    setWindowTitle(lang("案件：%1", "Case: %1")
                       .arg(m_caseManager->meta().caseNo));
    m_tree->clear();
    fillCameraGroups(addGroup(QString()));
    fillPreprocess(addGroup(QString()));
    fillReports(addGroup(QString()));
    fillSnapshots(addGroup(QString()));
    m_tree->expandAll();
    // v1.7.1：重建后按保存路径重新应用播放高亮
    if (!m_currentVideoPath.isEmpty())
        setCurrentVideoPath(m_currentVideoPath);
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

/// v1.7.1：高亮正在播放的案件条目（▶ 前缀 + 背景色 + 加粗；旧高亮恢复）
void CaseDock::setCurrentVideoPath(const QString &videoPath)
{
    m_currentVideoPath = videoPath;
    const QString norm = QDir::cleanPath(QFileInfo(videoPath).absoluteFilePath());
    // 清除旧高亮（防悬空：条目可能已被 refreshTree 删除——
    // 用 treeWidget() 判活，仍健在才操作）
    if (m_currentHighlight) {
        QTreeWidgetItem *old = m_currentHighlight;
        m_currentHighlight = nullptr;
        if (old->treeWidget()) {   // 条目仍在树上（未随 refreshTree 删除）
            old->setBackground(0, QBrush());
            QFont f = old->font(0);
            f.setBold(false);
            old->setFont(0, f);
            const QString t = old->text(0);
            if (t.startsWith(QStringLiteral("▶ ")))
                old->setText(0, t.mid(2));
        }
    }
    if (norm.isEmpty())
        return;
    // 遍历所有 video/output 条目匹配路径
    QTreeWidgetItemIterator it(m_tree);
    for (; *it; ++it) {
        QTreeWidgetItem *item = *it;
        const QString kind = item->data(0, kRoleKind).toString();
        if (kind != QLatin1String("video") && kind != QLatin1String("output"))
            continue;
        const QString p = QDir::cleanPath(
            QFileInfo(item->data(0, kRolePath).toString()).absoluteFilePath());
        // v1.7.1：Windows 路径大小写不敏感——用户经不同大小写路径打开
        // 视频时字符串比较失败致高亮不跟随
        if (p.compare(norm, Qt::CaseInsensitive) != 0)
            continue;
        item->setBackground(0, QBrush(QColor(0x2A, 0x4A, 0x6E)));   // 蓝底
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        item->setText(0, QStringLiteral("▶ ") + item->text(0));
        m_currentHighlight = item;
        break;
    }
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

void CaseDock::deleteVideoFile(const QString &id)
{
    // 删除策略（2026-08 人工反馈拍板）：
    //  - 源文件在案件目录外 → 只删案内分析结果（.vla/校时帧/登记），
    //    源文件保留（取证红线，用户素材不被误删）
    //  - 源文件在案件目录内（如 videos/ 下）→ 源文件 + 案内数据一并删
    // 包内副本（📦，原路径缺失时生效）不主动删，保护完整包取证完整性。
    const auto *v = m_caseManager->videoById(id);
    if (!v)
        return;
    const QDir caseDir(m_caseManager->caseDir());
    const QString rel = caseDir.relativeFilePath(v->originalPath);
    const bool insideCase = !rel.startsWith(QStringLiteral(".."))
        && !QDir::isAbsolutePath(rel);
    QMessageBox box(this);
    box.setWindowTitle(lang("删除视频", "Delete video"));
    box.setIcon(QMessageBox::Warning);
    if (insideCase) {
        box.setText(lang(
            "将删除视频「%1」：\n源文件（案件目录内）：%2\n案内数据（.vla 分析/校时证据帧）\n\n"
            "此操作不可恢复！包内副本（如有）保留。",
            "Delete video “%1”:\nsource (inside case dir): %2\nin-case data (.vla / calibration)\n\n"
            "This cannot be undone! Bundled copy (if any) is kept.")
            .arg(id, v->originalPath));
    } else {
        box.setText(lang(
            "将删除视频「%1」的分析结果：\n案内数据（.vla 分析/校时证据帧）+ 案件登记\n\n"
            "源文件在案件外，保留：%2",
            "Delete analysis of “%1”:\nin-case data (.vla / calibration) + registration\n\n"
            "Source stays untouched (outside case dir): %2")
            .arg(id, v->originalPath));
    }
    QAbstractButton *btnDel = box.addButton(
        lang("删除", "Delete"), QMessageBox::DestructiveRole);
    box.addButton(lang("取消", "Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != btnDel)
        return;
    QString err;
    if (insideCase && QFileInfo::exists(v->originalPath))
        QFile::remove(v->originalPath);   // 案件内源文件：一并删除
    if (!m_caseManager->removeVideo(id, true, &err)) {
        QMessageBox::warning(this, lang("删除失败", "Delete failed"), err);
        refreshTree();
        return;
    }
    QString saveErr;
    m_caseManager->saveCase(&saveErr);
    refreshTree();
}

QSet<QString> CaseDock::buildSnapshot() const
{
    // 案内引用文件存在性快照（外部变更检测；文件数少，2s 轮询开销可忽略）
    QSet<QString> snap;
    if (!m_caseManager || !m_caseManager->isOpen())
        return snap;
    const QDir caseDir(m_caseManager->caseDir());
    for (const auto &v : m_caseManager->meta().videos) {
        snap.insert(v.originalPath);
        snap.insert(m_caseManager->effectivePathFor(v));
    }
    for (const auto &p : m_caseManager->meta().preprocessSessions) {
        snap.insert(caseDir.absoluteFilePath(p.sessionDirRelPath));
        for (const auto &o : p.outputRefs)
            snap.insert(o.originalPath);
        for (const QString &sc : p.sidecarRelPaths)
            snap.insert(caseDir.absoluteFilePath(sc));
    }
    for (const QString &r : m_caseManager->meta().reports)
        snap.insert(caseDir.absoluteFilePath(r));
    return snap;
}

void CaseDock::onWatchTimer()
{
    if (!isVisible() || !m_caseManager || !m_caseManager->isOpen())
        return;
    const QSet<QString> now = buildSnapshot();
    if (now != m_snapshot) {
        m_snapshot = now;
        // 外部删除：自动清理已不存在的登记引用（2026-08 人工反馈：
        // 资源管理器删文件后列表应清除对应条目；不删任何现存文件）
        QString err;
        const int pruned = m_caseManager->pruneMissingFiles(&err);
        if (pruned > 0) {
            m_caseManager->saveCase(&err);
            m_snapshot = buildSnapshot();   // 清理后重新对齐
        }
        refreshTree();
    }
}

void CaseDock::onSelectionChanged()
{
    const auto *it = m_tree->currentItem();
    const bool video = it
        && it->data(0, kRoleKind).toString() == QLatin1String("video");
    if (m_btnDelete)
        m_btnDelete->setEnabled(video);
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
        // 批量重新定位（M3 任务13）：对话框为案件级操作，任意视频行可入
        menu.addAction(lang("批量重新定位…", "Batch relocate…"), this,
                       [this]() {
                           BatchRelocateDialog dlg(m_caseManager, this);
                           dlg.exec();
                           refreshTree();
                       });
        menu.addAction(lang("移除出案件…", "Remove from case…"), this,
                       [this, id]() { removeVideo(id); });
        menu.addAction(lang("删除视频…", "Delete video…"), this,
                       [this, id]() { deleteVideoFile(id); });
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
        // 机位组（2026-08-24 拍板：改名前置案件树；编号制废止）
        QMenu *gm = menu.addMenu(lang("移到机位组 ▸", "Move to camera group ▸"));
        for (const CaseCameraGroup &g : m_caseManager->meta().cameraGroups) {
            const QString gid = g.groupId;
            gm->addAction(CaseModel::groupDisplayName(g), this,
                          [this, id, gid]() {
                              QString err;
                              if (m_caseManager->assignToGroup(id, gid, &err)) {
                                  m_caseManager->saveCase(&err);
                                  refreshTree();
                              }
                          });
        }
        gm->addSeparator();
        gm->addAction(lang("新建机位组…", "New group…"), this,
                      [this, id]() {
                          bool ok = false;
                          const QString name = QInputDialog::getText(this,
                              lang("新建机位组", "New camera group"),
                              lang("位置名（如「东门烟酒店」；可留空后改）：",
                                   "Location name:"), QLineEdit::Normal,
                              QString(), &ok);
                          if (!ok) return;
                          QString err;
                          const QString gid = m_caseManager->createGroup(
                              name.trimmed(), &err);
                          if (gid.isEmpty()) {
                              QMessageBox::warning(this, lang("新建机位组", "New group"), err);
                              return;
                          }
                          m_caseManager->assignToGroup(id, gid, &err);
                          m_caseManager->saveCase(&err);
                          refreshTree();
                      });
        menu.addAction(lang("在资源管理器中显示", "Show in Explorer"), this,
                       [this, path]() { showInExplorer(path); });
    } else if (kind == QLatin1String("output")) {
        menu.addAction(lang("打开播放", "Open & play"), this, [this, path]() {
            emit openVideoRequested(path);
        });
        menu.addAction(lang("在资源管理器中显示", "Show in Explorer"), this,
                       [this, path]() { showInExplorer(path); });
        menu.addSeparator();
        // v1.7.1：产物与视频同待遇——指纹 + 编号
        const auto *o = m_caseManager->videoById(id);
        menu.addAction(lang("计算指纹", "Compute fingerprint"), this,
                       [this, id]() {
                           m_caseManager->queueHashFor(id);
                           refreshTree();   // ⏳ 立即可见
                       });
        auto *oCopy = menu.addAction(lang("复制指纹", "Copy fingerprint"), this,
                                     [o]() {
                                         QApplication::clipboard()->setText(
                                             o ? o->sha256 : QString());
                                     });
        oCopy->setEnabled(o && !o->sha256.isEmpty());
        QMenu *gm2 = menu.addMenu(lang("移到机位组 ▸", "Move to camera group ▸"));
        for (const CaseCameraGroup &g : m_caseManager->meta().cameraGroups) {
            const QString gid = g.groupId;
            gm2->addAction(CaseModel::groupDisplayName(g), this,
                           [this, id, gid]() {
                               QString err;
                               if (m_caseManager->assignToGroup(id, gid, &err)) {
                                   m_caseManager->saveCase(&err);
                                   refreshTree();
                               }
                           });
        }
        menu.addSeparator();
        // v1.7.1：产物与视频同待遇——重定位（缺失时 ✗ → 指回新位置）
        menu.addAction(lang("重新定位…", "Relocate…"), this,
                       [this, id]() { relocateVideo(id); });
        menu.addAction(lang("删除输出文件…", "Delete output file…"), this,
                       [this, item]() {
                           const int si = item->data(0, kRoleIdx).toInt();
                           const int oi = item->data(0, kRoleIdx2).toInt();
                           removePreprocessOutput(si, oi);
                       });
    } else if (kind == QLatin1String("camgroup")) {
        menu.addAction(lang("机位组改名…", "Rename group…"), this,
                       [this, id]() { renameGroupFlow(id); });
        menu.addAction(lang("新建机位组…", "New group…"), this, [this]() {
            bool ok = false;
            const QString name = QInputDialog::getText(this,
                lang("新建机位组", "New camera group"),
                lang("位置名：", "Location name:"), QLineEdit::Normal,
                QString(), &ok);
            if (!ok) return;
            QString err;
            m_caseManager->createGroup(name.trimmed(), &err);
            m_caseManager->saveCase(&err);
            refreshTree();
        });
    } else if (kind == QLatin1String("session")) {
        menu.addAction(lang("在资源管理器中显示", "Show in Explorer"), this,
                       [this, path]() { showInExplorer(path); });
        menu.addSeparator();
        menu.addAction(lang("删除会话与文件…", "Delete session & files…"), this,
                       [this, item]() {
                           removePreprocessSession(
                               item->data(0, kRoleIdx).toInt());
                       });
    } else {   // file：sidecar / 报告 / 快照
        menu.addAction(lang("在资源管理器中显示", "Show in Explorer"), this,
                       [this, path]() { showInExplorer(path); });
        menu.addSeparator();
        menu.addAction(lang("删除文件…", "Delete file…"), this,
                       [this, item]() { removeCaseFile(item); });
    }
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

void CaseDock::removePreprocessSession(int si)
{
    if (si < 0)
        return;
    const auto &sessions = m_caseManager->meta().preprocessSessions;
    if (si >= sessions.size())
        return;
    QMessageBox box(this);
    box.setWindowTitle(lang("删除会话", "Delete session"));
    box.setIcon(QMessageBox::Warning);
    box.setText(lang(
        "将删除前处理会话及其全部文件（输出/sidecar/报告）：\n%1\n\n此操作不可恢复！",
        "Delete the preprocess session and all its files (outputs/sidecars):\n"
        "%1\n\nThis cannot be undone!")
        .arg(QFileInfo(sessions[si].sessionDirRelPath).fileName()));
    QAbstractButton *btnDel = box.addButton(
        lang("删除", "Delete"), QMessageBox::DestructiveRole);
    box.addButton(lang("取消", "Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != btnDel)
        return;
    QString err;
    if (!m_caseManager->removePreprocessSession(si, true, &err)) {
        QMessageBox::warning(this, lang("删除失败", "Delete failed"), err);
        return;
    }
    m_caseManager->saveCase(&err);
    refreshTree();
}

void CaseDock::removePreprocessOutput(int si, int oi)
{
    const auto &sessions = m_caseManager->meta().preprocessSessions;
    if (si < 0 || si >= sessions.size() || oi < 0
        || oi >= sessions[si].outputRefs.size())
        return;
    const QString path = sessions[si].outputRefs[oi].originalPath;
    QMessageBox box(this);
    box.setWindowTitle(lang("删除输出文件", "Delete output file"));
    box.setIcon(QMessageBox::Warning);
    box.setText(lang("将删除输出文件：\n%1\n\n此操作不可恢复！",
                     "Delete output file:\n%1\n\nThis cannot be undone!")
                    .arg(path));
    QAbstractButton *btnDel = box.addButton(
        lang("删除", "Delete"), QMessageBox::DestructiveRole);
    box.addButton(lang("取消", "Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != btnDel)
        return;
    QString err;
    if (!m_caseManager->removePreprocessOutput(si, oi, true, &err)) {
        QMessageBox::warning(this, lang("删除失败", "Delete failed"), err);
        return;
    }
    m_caseManager->saveCase(&err);
    refreshTree();
}

void CaseDock::removeCaseFile(QTreeWidgetItem *item)
{
    if (!item)
        return;
    const QString path = item->data(0, kRolePath).toString();
    const int si = item->data(0, kRoleIdx).toInt();
    const bool underSession = item->parent()
        && item->parent()->data(0, kRoleKind).toString()
               == QLatin1String("session");
    const bool isReport = !underSession && si >= 0;
    QMessageBox box(this);
    box.setWindowTitle(lang("删除文件", "Delete file"));
    box.setIcon(QMessageBox::Warning);
    box.setText(lang("将删除文件：\n%1\n\n此操作不可恢复！",
                     "Delete file:\n%1\n\nThis cannot be undone!")
                    .arg(path));
    QAbstractButton *btnDel = box.addButton(
        lang("删除", "Delete"), QMessageBox::DestructiveRole);
    box.addButton(lang("取消", "Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != btnDel)
        return;
    QString err;
    if (underSession) {
        m_caseManager->removeSidecar(si, path, true, &err);
    } else if (isReport) {
        m_caseManager->removeReport(si, true, &err);
    } else {
        // 快照等无登记文件：直接删
        if (!QFile::remove(path))
            err = lang("无法删除文件", "Cannot delete file");
    }
    if (!err.isEmpty()) {
        QMessageBox::warning(this, lang("删除失败", "Delete failed"), err);
        return;
    }
    m_caseManager->saveCase(&err);
    refreshTree();
}

/// 组改名（组节点右键；改名=改组名，键不动，引用零牵连）
void CaseDock::renameGroupFlow(const QString &groupId)
{
    const CaseCameraGroup *g = CaseModel::findGroup(m_caseManager->meta(),
                                                    groupId);
    if (!g)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this,
        lang("机位组改名", "Rename group"),
        lang("机位名称（建议用位置名，如「东门烟酒店」）：\n"
             "对时间线/点位图/报告全系统生效。",
             "Camera name (applies system-wide):"),
        QLineEdit::Normal, g->name, &ok);
    if (!ok)
        return;
    QString err;
    if (!m_caseManager->renameGroup(groupId, name.trimmed(), &err)) {
        QMessageBox::warning(this, lang("机位组改名", "Rename group"), err);
        return;
    }
    if (!m_caseManager->saveCase(&err))
        QMessageBox::warning(this, lang("机位组改名", "Rename group"), err);
    refreshTree();
}

void CaseDock::moveToGroupFlow(const QString &id)
{
    Q_UNUSED(id);   // 被子菜单内联实现取代（保留槽位防外引）
}
