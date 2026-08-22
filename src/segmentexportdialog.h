#ifndef SEGMENTEXPORTDIALOG_H
#define SEGMENTEXPORTDIALOG_H

/// @file segmentexportdialog.h
/// @brief 选段导出面板（P-68；v1.13.0 起为**非模态浮窗**，用户反馈②③）：
///        打开后不打断播放/游标操作——「在游标处分段」实时取当前播放位置；
///        导出进度内嵌面板（非阻塞弹窗），可最小化/拖到一边继续看视频。
///
/// 交互流：打开（带入当前 AB 选段与标签初值分段）→ 调分速/分段/OSD/路径
/// → 「开始导出」→ 面板内进度条与取消 → 完成提示。关闭即隐藏（不销毁，
/// 由父窗口持有复用；选段变化时经 setPlan 刷新）。

#include <QDialog>
#include "domain/speed_plan.h"

class QTableWidget;
class QCheckBox;
class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;

class SegmentExportDialog : public QDialog
{
    Q_OBJECT

public:
    /// @param plan      初始方案（A/B + 标签边界初值）
    /// @param cursorMs  当前播放位置（「在游标处分段」钮用）
    /// @param suggestedPath 建议输出路径
    /// @param wallEpoch 多机模式：时刻按墙钟日历显示
    SegmentExportDialog(const speedplan::SpeedPlan &plan, qint64 cursorMs,
                        const QString &suggestedPath, QWidget *parent = nullptr,
                        bool wallEpoch = false);

    speedplan::SpeedPlan plan() const { return m_plan; }
    bool burnOsd() const;
    QString outputPath() const;

    static QString formatMs(qint64 ms);
    QString fmtTime(qint64 ms) const;   // wallEpoch → 日历时刻；否则流内 h:mm:ss.mmm

    /// 选段/初值刷新（再次打开时）
    void setPlan(const speedplan::SpeedPlan &plan, const QString &suggestedPath);
    /// 实时游标（父窗口 positionChanged 驱动）
    void setCursorMs(qint64 ms) { m_cursorMs = ms; }

    /// 导出进行中（禁用编辑区，显示进度）
    void setExportRunning(bool running, int totalFrames = 0);
    void setProgress(int done, int total);
    /// 导出结束：ok=false 时 msg 为错误/取消说明
    void setResult(bool ok, const QString &msg);
    bool isExportRunning() const { return m_running; }

signals:
    /// 「开始导出」被点且参数合法（面板保持打开，进度内嵌）
    void exportRequested(const speedplan::SpeedPlan &plan, bool burnOsd,
                         const QString &outputPath);
    void cancelRequested();

private:
    void rebuildTable();
    void updateSummary();

    speedplan::SpeedPlan m_plan;
    qint64 m_cursorMs = 0;
    bool m_wallEpoch = false;
    bool m_running = false;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_osdCheck = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_exportBtn = nullptr;
    QWidget *m_progressBox = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_resultLabel = nullptr;
};

#endif // SEGMENTEXPORTDIALOG_H
