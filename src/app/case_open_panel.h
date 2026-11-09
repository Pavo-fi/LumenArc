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

private:
    void onItemActivated(QListWidgetItem *item);
    void activateRecent(const QString &dir);
    void applyFilter();

    CaseManager *m_caseManager = nullptr;   // 不持有
    QStringList m_recents;

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
