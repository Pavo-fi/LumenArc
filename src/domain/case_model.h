/**
 * @file case_model.h
 * @brief 案件数据模型（domain 层纯数据，不依赖 Qt Widgets）+ case.json 读写迁移
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §2.2（2026-08-13 拍板终稿）。
 * 要点：
 * - case.json = 案件档案登记表：magic + formatVersion=1 + 迁移（F1）；
 *   版本超上限明确拒绝（F4）；未知字段忽略并收集警告（F3 只加不改）。
 * - 校时数据不入案（.vla 为 SSOT），仅存徽标缓存（hasCalibration/summary）。
 * - 视频引用制（NFR2 源文件只读延续）；V### 单调递增、移除后不复用。
 * - 案件编号自动生成 YYYYMMDD-城市区县-x，创建后固定不变。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QRectF>
#include <QJsonObject>
#include <QtGlobal>

/// 案件内视频登记（引用制：源文件只读，不复制不入案）
struct CaseVideoRef {
    QString id;                 ///< "V001" 单调递增，移除后不复用
    QString originalPath;       ///< 原始绝对路径（重新定位只改它，不动 .vla）
    QString vlaRelPath;         ///< 案内分析结果相对路径 videos/V001.vla
    qint64  sizeBytes = 0;
    qint64  mtimeMs = 0;
    QString sha256;             ///< 空 = 未算（闲时队列/手动统一算）
    QRectF  timestampRoi;       ///< 时间戳框选记忆（归一化 0~1，无效=未框选）
    bool    hasCalibration = false;     ///< 徽标缓存：已校时（真数据以 .vla 为准）
    QString calibrationSummary;         ///< 徽标文案（写 .vla 时同步刷新）
    QString bundledRelPath;     ///< 【仅完整包】包内副本 sources/V001__原名.mp4
    QString cameraLabel;        ///< v1.7.1：摄像头编号（自定义；前处理产物
                                ///< 登记时自动继承通道名）

    QJsonObject toJson() const;
    static CaseVideoRef fromJson(const QJsonObject &o);
};

/// 前处理会话登记（产物引用制 + sidecar 复制入案归类）
struct CasePreprocessRef {
    QString sessionDirRelPath;          ///< preprocess/<yyyyMMdd_HHmmss>
    QString reportCsvRelPath;           ///< 证据报告 CSV
    QVector<CaseVideoRef> outputRefs;   ///< 拼接/转码输出（引用制，同视频待哈希）
    QStringList sidecarRelPaths;        ///< 复制入案的 .lumencal.json（sidecars/）

    QJsonObject toJson() const;
    static CasePreprocessRef fromJson(const QJsonObject &o);
};

/// 机位组（2026-08-24 拍板：组为案件组织轴心——视频/前处理产物一律归组；
/// 同组才可同轴播放）。G### 稳定 id 永不复用；name 可改不作键。
struct CaseCameraGroup {
    QString groupId;            ///< "G001" 单调递增
    QString name;               ///< 位置名（可空=未命名；UI 退化显首成员 id）
    QStringList memberIds;      ///< V###/P###
    qint64  createdMs = 0;

    QJsonObject toJson() const;
    static CaseCameraGroup fromJson(const QJsonObject &o);
};

/// 案件档案（case.json 根）
struct CaseMeta {
    int     formatVersion = 1;
    QString caseNo;             ///< 案件编号：YYYYMMDD-城市区县-x（创建后固定）
    QString title;              ///< 案件名称（必填）
    QString investigator;       ///< 调查员（必填）
    QString unit;               ///< 单位（必填）
    qint64  incidentTimeMs = 0; ///< 案发时间（必填，编号日期索引源）
    QString city;               ///< 案发城市（必填，编号索引源）
    QString district;           ///< 案发区县（必填，编号索引源）
    QString locationDetail;     ///< 详细地址（选填）
    QString description;        ///< 备注（选填）
    qint64  createdMs = 0;
    qint64  modifiedMs = 0;
    QString lastVideoId;        ///< uiState：开案恢复现场
    int     nextVideoSeq = 1;   ///< 视频编号高水位（分配自增，永不回退→V### 不复用）
    QVector<CaseCameraGroup> cameraGroups;  ///< 机位组（F3：只加不改）
    int     nextGroupSeq = 1;   ///< 组编号高水位（G### 不复用）
    QVector<CaseVideoRef> videos;
    QVector<CasePreprocessRef> preprocessSessions;
    QStringList reports;        ///< reports/ 相对路径（v1.4.0 登记）
    QHash<QString, QString> extraFields;  ///< 扩展位（F3 只加不改）

    QJsonObject toJson() const;
};

/// case.json 读写与编号规则（全应用唯一入口，R5：禁止各处自行解析）
namespace CaseModel {

constexpr int kFormatVersion = 1;
extern const char kMagic[];         ///< "LumenArcCase"
extern const char kFileName[];      ///< "case.json"
extern const char kLockName[];      ///< "case.json.lock"

/// 读取 case.json。失败返回 false 并填 error（中文，可直接上 UI）；
/// 成功时 warnings 收集被忽略的未知字段（F3）。
bool load(const QString &caseDir, CaseMeta &out, QString *error,
          QStringList *warnings = nullptr);

/// 原子写 case.json（QSaveFile：临时文件 + rename，崩溃不留半文件）。
bool save(const QString &caseDir, const CaseMeta &meta, QString *error);

/// 生成案件编号：YYYYMMDD-城市区县-x（x=a,b,c…同日同区县序号，
/// 扫描 caseRootDir 下既有同前缀目录）。城市/区县内部去空白。
QString generateCaseNo(qint64 incidentTimeMs, const QString &city,
                       const QString &district, const QString &caseRootDir);

/// 分配视频编号：取 nextVideoSeq 并自增（高水位，移除后不复用）。
QString allocateVideoId(CaseMeta &meta);

/// 按 id 查找视频（无则 nullptr）。
const CaseVideoRef *findVideo(const CaseMeta &meta, const QString &id);
CaseVideoRef *findVideo(CaseMeta &meta, const QString &id);

/// 按 id 查找（videos[] 与 preprocessSessions[].outputRefs 通用；
/// 前处理产物 P### 与视频 V### 同待遇：哈希/徽标/编号，v1.7.1）
const CaseVideoRef *findRef(const CaseMeta &meta, const QString &id);
CaseVideoRef *findRef(CaseMeta &meta, const QString &id);

/// ---- 机位组辅助 ----
const CaseCameraGroup *findGroup(const CaseMeta &meta, const QString &groupId);
CaseCameraGroup *findGroup(CaseMeta &meta, const QString &groupId);
/// 引用所属组 id（空=未归组——迁移后不应存在）
QString groupIdOf(const CaseMeta &meta, const QString &refId);
/// 老案件迁移：把未归组的引用按 cameraLabel 自动建组（同标签一组共用名；
/// 无标签各自成组）。返回是否有改动（→ 调用方落盘）。幂等。
bool migrateCameraGroups(CaseMeta &meta);
/// 组显示名（name 空 → 首成员 id / 组 id 兜底）
QString groupDisplayName(const CaseCameraGroup &g);

/// 全部检材引用清单（videos + 各前处理会话 outputRefs）——P###/V###
/// 同待遇场合统一走这里（报告检材清单/点位图机位侧栏等，P-74 返修）
inline QVector<const CaseVideoRef *> allCaseRefs(const CaseMeta &meta)
{
    QVector<const CaseVideoRef *> out;
    for (const CaseVideoRef &v : meta.videos)
        out << &v;
    for (const CasePreprocessRef &sess : meta.preprocessSessions)
        for (const CaseVideoRef &v : sess.outputRefs)
            out << &v;
    return out;
}

} // namespace CaseModel
