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
///
/// v1.18.x：宫格侧可选「导出内容＝单路视频」（exportContentCombo + singleLaneCombo），
/// 选择器默认隐藏（setContentSelectorVisible），隐藏时面板与旧版逐位一致；
/// 面板只表达「导哪一路」，参数派生交 SegmentExportEngine::makeSingleLaneParams。

#include <QDialog>
#include <QPair>
#include <QVector>
#include "domain/speed_plan.h"

class QTableWidget;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QLabel;
class QProgressBar;
class QPushButton;

class SegmentExportDialog : public QDialog
{
    Q_OBJECT

public:
    /// v1.18.x（用户反馈「导出里面没有可以导出单路视频的选项」）：导出内容二选一。
    /// Grid = 宫格合成（既有唯一口径，**默认**）；Single = 只导所选机位那一路。
    /// 只有 setContentSelectorVisible(true) 打开选择器后才可能为 Single；
    /// 选择器隐藏时 contentMode() 恒为 Grid → 旧调用方（主窗单路导出等）零影响。
    enum class ContentMode { Grid, Single };

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
    /// v2.8：相对选段起点的偏移文本（3:05 / 1:03:05）
    QString fmtOffset(qint64 ms) const;
    /// "3:00" / "0:03:10" / "185"（秒）→ 毫秒；无法解析 ok=false
    static qint64 parseOffsetMs(const QString &text, bool *ok = nullptr);
    QString fmtTime(qint64 ms) const;   // wallEpoch → 日历时刻；否则流内 h:mm:ss.mmm

    /// 选段/初值刷新（再次打开时）
    void setPlan(const speedplan::SpeedPlan &plan, const QString &suggestedPath);
    /// v1.15.3：告知“上次导出设置”（同选段沿用时醒目提示 + 一键恢复）
    void setLastPlan(const speedplan::SpeedPlan &plan);
    /// 实时游标（父窗口 positionChanged 驱动）
    void setCursorMs(qint64 ms) { m_cursorMs = ms; }

    // ---- 导出内容（宫格合成 / 单路视频）：默认隐藏，旧外观逐位不变 ----

    /// 打开/关闭「导出内容」下拉与其下的机位下拉。默认 false（面板外观与
    /// 行为与本功能之前完全一致）；宫格侧（多机播放窗）显式打开后才可选单路。
    /// 关闭时 contentMode() 恒为 Grid（面板上的历史选择保留，重新打开可续用）。
    void setContentSelectorVisible(bool visible);
    bool contentSelectorVisible() const { return m_contentSelector; }

    /// 机位下拉条目。`(显示名, 源文件路径)`，**顺序即机位序号**：本面板不排序、
    /// 不剔除（条目 i 与调用方机位向量第 i 路一一对应，singleLaneIndex() 即该下标）。
    /// 显示名为空时回落路径的文件名。复用面板重填时尽量保住原选中那一路。
    void setLaneChoices(const QVector<QPair<QString, QString>> &nameAndPath);

    /// 当前导出内容（选择器隐藏 → 恒为 Grid）
    ContentMode contentMode() const;
    /// 单路模式下选中的机位下标；无条目/越界返回 -1
    int singleLaneIndex() const;
    /// 选中机位的源文件路径；无有效选中返回空串（与 singleLaneIndex()<0 同义）
    QString singleLanePath() const;
    /// 机位条目数（= setLaneChoices 填入的数量）
    int laneChoiceCount() const;

    /// 导出进行中（禁用编辑区，显示进度）
    void setExportRunning(bool running, int totalFrames = 0);
    void setProgress(int done, int total);
    /// 导出结束：ok=false 时 msg 为错误/取消说明
    void setResult(bool ok, const QString &msg);
    bool isExportRunning() const { return m_running; }

signals:
    /// 「开始导出」被点且参数合法（面板保持打开，进度内嵌）
    /// 签名不变（宫格/主窗两处调用方无需改）：选的是「单路视频」时，调用方
    /// 在本信号内读 contentMode() / singleLaneIndex() / singleLanePath() 派生参数。
    void exportRequested(const speedplan::SpeedPlan &plan, bool burnOsd,
                         const QString &outputPath);
    void cancelRequested();
    /// v3.1：面板内改了选段/断点/倍率 → 播放页要跟着变（双向同步）。
    /// 参数是完整方案（墙钟域），调用方可直接套用。
    void planEdited(const speedplan::SpeedPlan &plan);

private:
    void rebuildTable();
    void updateSummary();
public:
    /// v2.8：把「分段点（相对 A）」输入框内容写进 plan（也可被自检调用）
    void applySplitsFromEdit();
private:
    /// 用 plan 现有 splits 回填输入框（打开/恢复/游标分段后保持同步）
    void syncSplitsEdit();
    /// v3.2：分段点 chip（点哪个删哪个）
    void rebuildSplitChips();
    class QHBoxLayout *m_splitChipLay = nullptr;
    class QLineEdit *m_splitsEdit = nullptr;

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
    QPushButton *m_openFolderBtn = nullptr;  ///< 成功后「打开所在文件夹」
    // v1.15.3：沿用上次变速设置的提示条与恢复钮
    QLabel *m_lastHint = nullptr;
    QPushButton *m_restoreBtn = nullptr;
    speedplan::SpeedPlan m_lastPlan;         ///< 上次导出的变速计划（可恢复）
    // v1.18.x：导出内容选择（整行容器默认隐藏 → 面板外观与旧版逐位一致）
    QWidget *m_contentRow = nullptr;         ///< 「导出内容」行
    QComboBox *m_contentCombo = nullptr;     ///< objectName = exportContentCombo
    QWidget *m_laneRow = nullptr;            ///< 「机位」行（仅单路模式显示）
    QComboBox *m_laneCombo = nullptr;        ///< objectName = singleLaneCombo
    bool m_contentSelector = false;          ///< 选择器是否启用（默认关）
};

#endif // SEGMENTEXPORTDIALOG_H
