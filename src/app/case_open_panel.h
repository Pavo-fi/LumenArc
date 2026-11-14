/**
 * @file case_open_panel.h
 * @brief 案件工作入口：首次使用引导 / 最近案件续作，页面内居中非模态。
 *
 * 纯展示组件，案件状态一律经 CaseManager 接口读写（R5/R6）。
 */
#pragma once

#include <QFrame>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QResizeEvent;
class QWidget;
class CaseManager;

class CaseOpenPanel : public QFrame
{
    Q_OBJECT

public:
    explicit CaseOpenPanel(CaseManager *cm, QWidget *parent = nullptr);

    /// 从 CaseManager 重建最近案件列表与首访/回访布局。
    void refresh();
    /// 用给定目录列表重建视图；也供离屏自检直接注入，避免改写用户 QSettings。
    void populateRecentCases(const QStringList &dirs);

    /// 启动时是否显示工作入口（QSettings case/showStartPage）
    static bool showStartupPanel();
    static void setShowStartupPanel(bool on);

signals:
    void openCaseRequested(const QString &dir);
    void browseRequested();
    void newCaseRequested();
    void independentRequested();   // 直接打开视频：关闭面板，不进入案件
    void relocateCaseRequested(const QString &oldDir);
    void closeRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    /// 面板尺寸变化后按新的可用宽度重算行内省略号（不参与居中决策，
    /// 居中仍由主窗 centerCaseOpenPanel() 以主窗口坐标完成）。
    void resizeEvent(QResizeEvent *event) override;

private:
    void onItemActivated(QListWidgetItem *item);
    void activateRecent(const QString &dir);
    void removeRecentEntry(const QString &dir);   // 只移除最近记录，不碰磁盘
    void applyFilter();
    /// 主按钮下方那行案件名的省略绘制（宽度随面板变化，文本永远来自 UserRole 目录）
    void updatePrimaryCaption();
    /// 重绘所有带 "elideFull" 属性的行内标签（案件名 / 目录路径 / 失效提示）
    void relayoutElidedTexts();
    /// 最近案件行文字列在当前面板宽度下可用的像素宽度
    int recentTextWidth() const;

    CaseManager *m_caseManager = nullptr;   // 不持有
    QStringList m_recents;
    QString m_primaryCaseDir;   // 主按钮当前指向的最近案件目录（空 = 首访）

    QPushButton *m_primaryButton = nullptr;
    QLabel *m_primaryCaption = nullptr;
    QPushButton *m_newCaseButton = nullptr;
    QPushButton *m_openCaseButton = nullptr;
    QPushButton *m_directVideoButton = nullptr;

    QWidget *m_recentPage = nullptr;
    QWidget *m_onboardingPage = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QListWidget *m_recentList = nullptr;
    QLabel *m_noResultsLabel = nullptr;
};
