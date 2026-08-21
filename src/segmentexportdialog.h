#ifndef SEGMENTEXPORTDIALOG_H
#define SEGMENTEXPORTDIALOG_H

/// @file segmentexportdialog.h
/// @brief 选段导出对话框（P-68 拍板）：分速段表（倍速分段可调——不关键段
///        快放掠过、关键段常速/慢速）、OSD 角标可选、输出路径。
///        分速段初值 = 选段内标签（N 键标记即「关键时刻」）为边界。

#include <QDialog>
#include "domain/speed_plan.h"

class QTableWidget;
class QCheckBox;
class QLineEdit;
class QLabel;

class SegmentExportDialog : public QDialog
{
    Q_OBJECT

public:
    /// @param plan      初始方案（A/B + 标签边界初值）
    /// @param cursorMs  当前播放位置（「在游标处分段」钮用）
    /// @param suggestedPath 建议输出路径
    SegmentExportDialog(const speedplan::SpeedPlan &plan, qint64 cursorMs,
                        const QString &suggestedPath, QWidget *parent = nullptr,
                        bool wallEpoch = false);   // 多机：时刻按墙钟日历显示

    speedplan::SpeedPlan plan() const { return m_plan; }
    bool burnOsd() const;
    QString outputPath() const;

    static QString formatMs(qint64 ms);
    QString fmtTime(qint64 ms) const;   // wallEpoch → 日历时刻；否则流内 h:mm:ss.mmm

private:
    void rebuildTable();
    void updateSummary();

    speedplan::SpeedPlan m_plan;
    qint64 m_cursorMs = 0;
    bool m_wallEpoch = false;
    QTableWidget *m_table = nullptr;
    QCheckBox *m_osdCheck = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QLabel *m_summary = nullptr;
};

#endif // SEGMENTEXPORTDIALOG_H
