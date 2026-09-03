#pragma once

/// @file composeexportdialog.h
/// @brief 合成导出器（2026-09-03 拍板：分析成果合成导出器，取代分段导出入口）：
///        多段序列（案内素材、可跨文件、逐段速率）+ 双模式——
///        证据片段（无损直拷+完整性清单，像素零改动）/
///        分析演示片（重编码烧录：校正时间角标 + 强制「非原始证据」角标）。
///        单段且源为当前视频且速率 1x 时可附加图表面板（走旧复合导出路径，零回归）。

#include <QDialog>
#include <QVector>
#include "infrastructure/segment_export_engine.h"

class QTableWidget;
class QRadioButton;
class QCheckBox;
class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class CaseManager;

class ComposeExportDialog : public QDialog
{
    Q_OBJECT
public:
    /// @param cm           案件管理器（可空=独立模式；案内视频清单与逐文件校正取自它）
    /// @param currentVideo 主窗当前视频（「添加当前选段」的默认源）
    /// @param aMs/bMs      主窗当前 A-B 选段（流内毫秒）
    /// @param cursorMs     当前播放位置（添加片段时的入点初值）
    /// @param fps          输出帧率（源帧率）
    ComposeExportDialog(CaseManager *cm, const QString &currentVideo,
                        qint64 aMs, qint64 bMs, qint64 cursorMs, double fps,
                        QWidget *parent = nullptr);

    /// 再次打开时刷新上下文（选段/游标/当前视频可能已变）
    void refreshContext(const QString &currentVideo, qint64 aMs, qint64 bMs,
                        qint64 cursorMs, double fps);
    void setCursorMs(qint64 ms) { m_cursorMs = ms; }
    bool wantPanels() const;

    /// 导出进行中（禁用编辑区，显示进度）
    void setExportRunning(bool running, int totalFrames = 0);
    void setProgress(int done, int total);
    void setResult(bool ok, const QString &msg);
    bool isExportRunning() const { return m_running; }

    static QString formatMs(qint64 ms);
    static qint64 parseMs(const QString &text, bool *ok = nullptr);   // h:mm:ss[.mmm] / mm:ss / 秒数
signals:
    void exportRequested(const SegmentExportEngine::Params &params);
    void cancelRequested();

private slots:
    void onAddSegment();
    void onRemoveSegment();
    void onMoveSegment(int delta);
    void onBrowseOutput();
    void onStartExport();
    void onModeChanged();

private:
    void rebuildTable();
    void refreshRowDurations();
    void updateSuggestedPath();
    bool collectSegments(QVector<SegmentExportEngine::Params::ComposeSeg> *out,
                         QString *err);
    SegmentExportEngine::Params buildParams(QString *err);
    QString videoDisplayName(const QString &path) const;
    QString effectivePath(const QString &path) const;   // 案内副本分流

    CaseManager *m_cm = nullptr;
    QString m_currentVideo;
    qint64 m_aMs = 0, m_bMs = 0, m_cursorMs = 0;
    double m_fps = 25.0;

    struct Row {
        QString sourcePath;   // 原始路径（展示用简名；导出时经 effectivePath 分流）
        qint64 inMs = 0, outMs = 0;
        double rate = 1.0;
    };
    QVector<Row> m_rows;

    QTableWidget *m_table = nullptr;
    QRadioButton *m_evidenceRadio = nullptr;
    QRadioButton *m_demoRadio = nullptr;
    QCheckBox *m_osdCheck = nullptr;
    QCheckBox *m_caseNoCheck = nullptr;
    QCheckBox *m_panelsCheck = nullptr;
    QLineEdit *m_outPath = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    bool m_running = false;
};
