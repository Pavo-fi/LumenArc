/**
 * @file case_manager.h
 * @brief 案件管理器（app 层服务）：案件生命周期 + 视频登记 + 路径分流 SSOT
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §2/§3。
 * SSOT（R5/R6）：打开的案件由 CaseManager 持有，MainWindow/PreprocessWindow
 * 经接口读写，禁止各处自行解析 case.json。
 * 模式规则：视频是否"在案"决定其行为（per-video membership）——
 * vlaPathFor/evidenceDirFor 对未入案视频返回独立模式老路径（逐字节照旧）。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QRectF>
#include <QVector>
#include <QThreadPool>
#include <atomic>
#include "domain/case_model.h"

/// 完整性报告条目（integrityReportReady 载荷）
struct CaseIntegrityItem {
    QString label;      ///< 显示名：V### 或案内相对路径
    QString path;       ///< 绝对路径
    int status = 0;     ///< 0=一致 1=已变更 2=缺失
};
Q_DECLARE_METATYPE(CaseIntegrityItem)
Q_DECLARE_METATYPE(QVector<CaseIntegrityItem>)

class CaseManager : public QObject
{
    Q_OBJECT

public:
    explicit CaseManager(QObject *parent = nullptr);
    ~CaseManager() override;

    // ---- 状态 ----
    bool isOpen() const { return m_open; }
    QString caseDir() const { return m_caseDir; }
    const CaseMeta &meta() const { return m_meta; }   ///< 只读；修改经 API
    bool isDirty() const { return m_dirty; }

    // ---- 生命周期 ----
    /// 新建案件：在 parentDir 下建 <编号>-<名称>/ 与子目录，写 case.json。
    /// meta.caseNo 须已由对话框预览生成（generateCaseNo）。
    bool createCase(const QString &parentDir, const CaseMeta &meta,
                    QString *error);
    /// 打开案件：读+迁移+.lock。lockConflict 出参=true 表示锁占用
    /// （其他实例或上次未正常关闭），调用方可 force=true 强制打开。
    bool openCase(const QString &dir, QString *error,
                  QStringList *warnings = nullptr,
                  bool *lockConflict = nullptr, bool force = false);
    bool saveCase(QString *error);
    /// 关闭案件（dirty 提示由调用方负责）。移除锁文件。
    void closeCase();

    // ---- 最近案件（QSettings，最多 10 条，新→旧）----
    QStringList recentCases() const;
    void removeRecent(const QString &dir);

    // ---- 案件根目录（拍板§8-4：独立设置项，默认 <程序目录>/cases/）----
    static QString caseRootDir();          ///< 当前生效根目录（设置值或默认）
    static QString defaultRootDir();       ///< 默认 <程序目录>/cases/
    static void setCaseRootDir(const QString &dir);

    // ---- 案件属性（任务11：名称/调查员/单位/详细地址/备注可改；
    //      编号/案发时间/地点创建后固定——编号是目录索引源）----
    bool updateCaseInfo(const QString &title, const QString &investigator,
                        const QString &unit, const QString &locationDetail,
                        const QString &description, QString *error);

    // ---- 视频登记 ----
    /// 添加视频：分配 V###（高水位不复用）、登记 size/mtime、置 dirty。
    /// 重复路径拒绝（error 说明已有编号）。成功返回新 id，失败返回空串。
    QString addVideo(const QString &path, QString *error);
    /// 移除视频；deleteData=true 时连同案内 .vla 与校时证据帧一并删除。
    bool removeVideo(const QString &id, bool deleteData, QString *error);

    /// 重定位（M2 基础版；取证红线：只改引用路径，绝不动 .vla 数据）。
    /// 新文件必须存在；大小与登记不一致 → 返回 false 且 sizeMismatch=true
    /// （调用方默认拒绝，显式「仍要采用」后以 force=true 重调，extraFields
    /// 留档）。採用后重登记 size/mtime、清 sha256 并入队重算。
    /// M3 任务13 批量版在此基础上加指纹强制比对。
    bool relocateVideo(const QString &id, const QString &newPath,
                       QString *error, bool *sizeMismatch = nullptr,
                       bool force = false);
    /// 手动算单路指纹（右键「算指纹」；幂等，已排队不重复）
    void queueHashFor(const QString &id) { queueVideoHash(id); }

    const CaseVideoRef *videoById(const QString &id) const;
    /// originalPath 或包内副本（sources/）绝对路径匹配（完整包接收端播放
    /// 副本时 vlaPathFor/timestampRoi 等分流仍命中本路视频）
    const CaseVideoRef *videoByPath(const QString &path) const;
    bool isCaseVideo(const QString &path) const
    { return videoByPath(path) != nullptr; }

    /// 有效路径（v1.3.0 M3 任务12）：originalPath 存在→原路径；否则包内
    /// sources/ 副本存在→副本绝对路径。originalPath 永不改写（取证红线）。
    /// 均无→返回 originalPath（缺失语义由调用方按 exists 判）。
    QString effectivePathFor(const CaseVideoRef &v) const;

    // ---- 路径分流（双模式核心）----
    /// 入案视频 → <案件>/videos/V###.vla；未入案/无案件 → 视频路径+".vla"
    QString vlaPathFor(const QString &videoPath) const;
    /// 入案视频 → <案件>/evidence/calibration/<V###>；
    /// 未入案/无案件 → <视频目录>/LumenArc_Calibration（v1.2.x 老行为）
    QString evidenceDirFor(const QString &videoPath) const;

    // ---- 框选记忆（随案；独立模式仍由 MainWindow 走 QSettings）----
    QRectF timestampRoiFor(const QString &videoPath) const;
    void setTimestampRoi(const QString &videoPath, const QRectF &roi);

    // ---- 校时徽标缓存（写 .vla 时同步刷新；真数据以 .vla 为准）----
    void updateCalibrationBadge(const QString &videoPath, bool has,
                                const QString &summary);

    // ---- 前处理会话登记（v1.3.0 M2 任务8）----
    /// finalize 后由 PreprocessWindow 调用：sessionDirAbs 必须位于案件目录
    /// 内（preprocess/<ts>）；sidecar 复制归类 sessionDir/sidecars/（原件
    /// 保留在输出旁供独立打开继承）；输出引用制登记（不复制），size/mtime
    /// 入册，sha256 留空（案内文件由 manifest 覆盖，见 M1 清单）。
    bool addPreprocessSession(const QString &sessionDirAbs,
                              const QString &reportCsvAbs,
                              const QStringList &outputPaths,
                              const QStringList &sidecarAbsPaths,
                              QString *error);

    // ---- uiState ----
    void setLastVideoId(const QString &id);

    void setModified();   ///< 登记变更 → dirty

    // ---- 哈希队列（Q-9：闲时后台单线程低优先级，逐文件完成提示）----
    void queueMissingHashes();   ///< 未算/已变更的入队（开案自动调，手动也可）
    void queueAllHashes();       ///< 「统一计算哈希」全量重算
    void cancelHashes();         ///< 关案/退出前置取消
    bool hashQueueActive() const { return m_hashPending > 0; }

    // ---- 完整性校验（异步：快扫 size/mtime + 差异/全量重算比对）----
    void verifyIntegrity(bool fullRehash);

    // ---- 移交包导出（v1.3.0 M3 任务12）----
    /// 导出前自检（同步快判，对话框展示用）
    struct CaseExportPrecheck {
        int missingHashCount = 0;     ///< 未算指纹视频数（可即补）
        QStringList missingVideoIds;  ///< 源文件缺失的 V###（可即定位）
        qint64 caseBytes = 0;         ///< 案件目录体量（不含 sources/）
        qint64 sourcesBytes = 0;      ///< 完整包 sources/ 增量体量（存在的源文件）
        qint64 availableBytes = -1;   ///< 目标卷可用空间（-1=未取得）
        bool insufficientSpace = false;
    };
    CaseExportPrecheck exportPrecheck(const QString &targetDir,
                                      bool fullPackage) const;
    /// 后台导出移交包：完整包=案件目录+sources/ 视频副本+包内 case.json
    ///（bundledRelPath 写入包内副本，源案件不动）+manifest+README+导后快校；
    /// 轻量包=案件目录（无 sources/）。半成品清理，可取消。
    void exportCase(const QString &targetParentDir, bool fullPackage);
    void cancelExport();
    bool exportActive() const { return m_exportActive.load(); }

    // ---- manifest.json（机器维护：保存后队列回填 + 校验时重建）----
    void queueManifestRefresh();

signals:
    void caseOpened(const QString &dir);
    void caseClosed();
    void caseDirtyChanged(bool dirty);
    void caseSaved();
    void videoAdded(const QString &id);
    void videoRemoved(const QString &id);
    /// 视频引用信息变更（重定位/重登记后，面板刷新用）
    void videoInfoChanged(const QString &id);
    /// 哈希逐文件完成（Q-9 逐文件提示）：done/total 为队列进度
    void hashProgress(const QString &videoId, int done, int total);
    void hashQueueFinished();
    /// 完整性校验报告：status 0=一致 1=已变更 2=缺失
    void integrityReportReady(const QVector<CaseIntegrityItem> &items);
    /// 移交包导出进度/结果（M3 任务12）
    void exportProgress(int percent, const QString &stage);
    void exportFinished(bool ok, const QString &message);

private slots:
    // 工作线程结果回投（QueuedConnection，UI 线程更新 meta）
    void onVideoHashed(const QString &id, const QString &sha256,
                       qint64 sizeBytes, qint64 mtimeMs);
    void onManifestRefreshed(int fileCount);
    void onVerifyDone(const QVector<CaseIntegrityItem> &items);
    void onExportProgress(int percent, const QString &stage);
    void onExportFinished(bool ok, const QString &message);

private:
    void pushRecent(const QString &dir);
    bool createLock(QString *error);
    void removeLock();
    static QString caseDirName(const CaseMeta &meta);
    void queueVideoHash(const QString &id);   ///< 单路入队（内部）

    QString m_caseDir;
    CaseMeta m_meta;
    bool m_open = false;
    bool m_dirty = false;

    // 哈希队列状态（QThreadPool×1，worker 经 QueuedConnection 回投）
    class HashTask;
    class ExportTask;
    QThreadPool *m_hashPool = nullptr;
    std::atomic<bool> m_hashAbort{false};
    std::atomic<int> m_hashPending{0};
    int m_hashTotal = 0, m_hashDone = 0;
    bool m_verifyPending = false;
    std::atomic<bool> m_exportActive{false};
    std::atomic<bool> m_exportAbort{false};
    friend class HashTask;
    friend class ExportTask;
};
