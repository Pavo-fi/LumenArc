#pragma once

/// @file composeworkbench.h
/// @brief 合成导出工作台（2026-09-03 拍板 v2：独立非模态窗口，废表格改片段块时间线）：
///        左素材树（多通道=机位同屏勾选组 / 单视频=案内全部视频含前处理产物）
///        + 中预览（可播放，单路=FfmpegVideoEngine，多通道=MultiCamSyncService 宫格）
///        + I/O 打点「+加入时间线」→ 下片段块时间线（点选/拖排序/双击精调/右键删）
///        + 底导出面板（证据直拷 / 分析演示片双模式）。
///        导出执行复用 SegmentExportEngine（多段/宫格段/证据直拷均已支持）。

#include <QDialog>
#include <QVector>
#include "app/cam_timeline.h"             // CamInventoryItem / buildCamInventory
#include "infrastructure/segment_export_engine.h"

class QTreeWidget;
class QTreeWidgetItem;
class QSlider;
class QLabel;
class QPushButton;
class QRadioButton;
class QCheckBox;
class QLineEdit;
class QProgressBar;
class QStackedWidget;
class QGridLayout;
class QToolButton;
class CamTileWidget;
class ComposeTimelineWidget;
class FfmpegVideoEngine;
class MultiCamSyncService;
class CaseManager;

class ComposeWorkbenchWindow : public QDialog
{
    Q_OBJECT
public:
    ComposeWorkbenchWindow(CaseManager *cm, const QString &currentVideo, double fps,
                           QWidget *parent = nullptr);
    ~ComposeWorkbenchWindow() override;

    void refreshContext(const QString &currentVideo, double fps);
    void setCursorMs(qint64 ms) { Q_UNUSED(ms); }   // 预览独立游标，主窗游标不喂

    bool wantPanels() const;
    bool isExportRunning() const { return m_running; }
    void setExportRunning(bool running, int totalFrames = 0);
    void setProgress(int done, int total);
    void setResult(bool ok, const QString &msg);

    static QString formatMs(qint64 ms);
    static qint64 parseMs(const QString &text, bool *ok = nullptr);

signals:
    void exportRequested(const SegmentExportEngine::Params &params);
    void cancelRequested();

protected:
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onMaterialChanged();
    void onPlayPause();
    void onSliderPressed();
    void onSliderMoved(int value);
    void onSliderReleased();
    void onMarkIn();
    void onMarkOut();
    void onRecordToggle();   ///< 红点单键流：第一下=起点，第二下=终点并加入
    void onAddSegment();
    void onBlockEdit(int idx);
    void onBlockRemove(int idx);
    void onBlockMove(int from, int to);
    void onModeChanged();
    void onBrowseOutput();
    void onStartExport();

private:
    void rebuildMaterials();
    void loadSinglePreview(const QString &path, const QString &displayName);
    void loadMultiPreview();
    void stopPreviews();
    qint64 previewPosMs() const;         ///< 当前预览位置（单路=流内；多通道=墙钟）
    qint64 previewDurationMs() const;    ///< 可打点的轴长
    bool previewIsMulti() const;
    void updateTransport();
    void updateGuide();      ///< 四步引导条状态机（选素材/截片段/排顺序/导出）
    void seekPreviewRelative(qint64 deltaMs);
    void installShortcuts(); ///< 快捷键（对齐剪映/PR：空格/I/O/←→/Delete/Ctrl+E…）
    QString segDisplayName(const SegmentExportEngine::Params::ComposeSeg &seg) const;
    QString effectivePath(const QString &path) const;
    void syncTimeline();
    void updateSuggestedPath();
    SegmentExportEngine::Params buildParams(QString *err);

    CaseManager *m_cm = nullptr;
    QString m_currentVideo;
    double m_fps = 25.0;

    // ---- 素材清单（案内全量：视频+前处理产物）----
    QVector<CamInventoryItem> m_inv;
    bool m_invLoaded = false;

    // ---- 预览 ----
    QStackedWidget *m_previewStack = nullptr;
    CamTileWidget *m_singleTile = nullptr;
    QWidget *m_multiPage = nullptr;
    QGridLayout *m_multiGrid = nullptr;
    QVector<CamTileWidget *> m_multiTiles;
    FfmpegVideoEngine *m_singleEngine = nullptr;   // 单路预览引擎
    MultiCamSyncService *m_svc = nullptr;          // 多通道预览服务
    QString m_singlePreviewPath;
    QString m_singlePreviewName;
    bool m_multiActive = false;
    bool m_sliderScrubbing = false;

    // ---- 打点 ----
    qint64 m_markIn = -1, m_markOut = -1;

    // ---- 片段序列（导出模型，引擎同款结构）----
    QVector<SegmentExportEngine::Params::ComposeSeg> m_segs;

    // ---- UI ----
    QTreeWidget *m_matTree = nullptr;
    QSlider *m_slider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QLabel *m_markLabel = nullptr;
    QPushButton *m_playBtn = nullptr;
    QPushButton *m_inBtn = nullptr;
    QPushButton *m_outBtn = nullptr;
    QPushButton *m_recordBtn = nullptr;   // ⏺/⏹ 录音笔式截取主钮
    QLabel *m_stepLabels[4] = {};         // 四步引导条
    QLabel *m_guideHint = nullptr;        // 白话动态提示
    QToolButton *m_advToggle = nullptr;   // 更多选项折叠开关
    QWidget *m_advPanel = nullptr;        // 高级选项折叠面板
    ComposeTimelineWidget *m_timeline = nullptr;
    QRadioButton *m_evidenceRadio = nullptr;
    QRadioButton *m_demoRadio = nullptr;
    QCheckBox *m_osdCheck = nullptr;
    QCheckBox *m_caseNoCheck = nullptr;
    QCheckBox *m_panelsCheck = nullptr;
    QCheckBox *m_roiCheck = nullptr;      // P2：ROI 烧录（单视频段，演示模式）
    QCheckBox *m_chartCheck = nullptr;    // P2：曲线滚动条（单视频段，演示模式）
    QLineEdit *m_outPath = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_startBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    bool m_running = false;
};
