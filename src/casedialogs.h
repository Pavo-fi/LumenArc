/**
 * @file casedialogs.h
 * @brief 案件对话框（ui 层）：新建案件 + 案件属性（v1.3.0 M2 任务11）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §3-M2 任务11 + §8 拍板。
 * 新建：必填校验（名称/调查员/单位/案发时间/城市/区县），编号自动生成
 * 预览（YYYYMMDD-城市区县-x，随输入实时刷新）。属性：可改名/人/单位/
 * 地址/备注；编号/案发时间/地点创建后固定（编号是目录索引源）。
 * 批量重定位/校验报告/导出对话框属 M3（任务12/13）。
 */
#pragma once

#include <QDialog>
#include <atomic>
#include <memory>
#include "domain/case_model.h"

class QLineEdit;
class QDateEdit;
class QPlainTextEdit;
class QLabel;
class QDialogButtonBox;
class QRadioButton;
class QProgressBar;
class QTableWidget;
class CaseManager;

/// 新建案件对话框：收集 CaseMeta 必填字段 + 编号预览
class NewCaseDialog : public QDialog
{
    Q_OBJECT
public:
    /// rootDir：编号预览扫描用（CaseModel::generateCaseNo）
    explicit NewCaseDialog(const QString &rootDir, QWidget *parent = nullptr);

    CaseMeta meta() const;   ///< 仅当 accepted 后有意义（编号已生成入 caseNo）

private slots:
    void refreshPreview();
    void validate();

private:
    QString m_rootDir;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_investigator = nullptr;
    QLineEdit *m_unit = nullptr;
    QDateEdit *m_incidentDate = nullptr;
    QLineEdit *m_city = nullptr;
    QLineEdit *m_district = nullptr;
    QLineEdit *m_locationDetail = nullptr;
    QPlainTextEdit *m_description = nullptr;
    QLabel *m_caseNoPreview = nullptr;
    QLabel *m_hint = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

/// 案件属性对话框：查看全部字段，编辑可改字段
class CasePropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CasePropertiesDialog(CaseManager *cm, QWidget *parent = nullptr);

private:
    CaseManager *m_caseManager = nullptr;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_investigator = nullptr;
    QLineEdit *m_unit = nullptr;
    QLineEdit *m_locationDetail = nullptr;
    QPlainTextEdit *m_description = nullptr;
};

/// 重定位交互流程（CaseDock 右键 / 导出前自检共用）：
/// 选文件 → relocateVideo；大小不一致默认拒绝、显式「仍要采用」后 force。
/// 返回 true = 已採用新引用。
bool relocateVideoInteractive(CaseManager *cm, const QString &id,
                              QWidget *parent);

/// 导出移交包对话框（v1.3.0 M3 任务12）：
/// 完整包默认/轻量可选；导前自检（未算可即补/缺失可即定位/知情可仍要导出）；
/// 空间预检；后台进度可取消。
class ExportCaseDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExportCaseDialog(CaseManager *cm, QWidget *parent = nullptr);

private slots:
    void refreshPrecheck();
    void onExport();
    void onComputeHashes();
    void onRelocateMissing();

private:
    CaseManager *m_cm = nullptr;
    QRadioButton *m_fullRadio = nullptr;
    QRadioButton *m_lightRadio = nullptr;
    QLineEdit *m_targetEdit = nullptr;
    QLabel *m_checkLabel = nullptr;
    QPushButton *m_btnExport = nullptr;
    QPushButton *m_btnHash = nullptr;
    QPushButton *m_btnRelocate = nullptr;
    QPushButton *m_btnCancelExport = nullptr;
    QProgressBar *m_progress = nullptr;
    bool m_exporting = false;
};

/// 批量重新定位对话框（v1.3.0 M3 任务13）：
/// 文件夹名+大小模糊匹配 → 人工确认 → 定位后强制指纹比对，
/// 不一致默认拒绝、显式【仍要采用】留档。
class BatchRelocateDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BatchRelocateDialog(CaseManager *cm, QWidget *parent = nullptr);
    void reject() override;   // 关闭前先取消后台指纹比对

private slots:
    void onScan();
    void onBrowseRow();
    void onVerifyAndApply();
    void onApplyOverrides();
    void onRowHashed(int row, bool ok, const QString &sha);
    void onAllHashed();

private:
    struct Row {
        QString videoId;
        QString originalPath;
        QString candidatePath;   // 可被【浏览…】覆盖
        int matchLevel = 0;
        bool manual = false;     // 候选由【浏览…】人工指定
        QString computedSha;     // 比对后填入
        int status = 0;          // 0=待比对 1=已採用 2=不一致(待决) 3=採用(留档) 4=失败 5=无候选
        QString note;
    };
    void rebuildTable();
    void setRowStatus(int row);
    void refreshSummary();
    void startNextHash();        // 逐路串行比对（IO 纪律同哈希队列）
    QString registeredSha(const QString &id) const;

    CaseManager *m_cm = nullptr;
    QLineEdit *m_dirEdit = nullptr;
    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_btnScan = nullptr;
    QPushButton *m_btnBrowseRow = nullptr;
    QPushButton *m_btnVerify = nullptr;
    QPushButton *m_btnOverride = nullptr;
    QPushButton *m_btnCancelHash = nullptr;
    QVector<Row> m_rows;
    QVector<int> m_pending;      // 待比对行号队列（startNextHash 消费）
    std::atomic<bool> m_hashCancel{false};
    std::shared_ptr<std::atomic<bool>> m_workerAbort;  // 工作线程持有副本防悬垂
    bool m_hashing = false;
};
