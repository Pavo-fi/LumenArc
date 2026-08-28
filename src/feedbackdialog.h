#pragma once

#include <QDialog>

class QPlainTextEdit;
class QCheckBox;
class QPushButton;
class QLabel;

// 意见反馈窗（帮助菜单进入）。可勾选附带诊断信息（版本/平台/上次崩溃摘要，
// 绝不包含案件数据与视频内容——取证红线）。
class FeedbackDialog : public QDialog {
    Q_OBJECT
public:
    explicit FeedbackDialog(QWidget* parent = nullptr);

private:
    void onSubmit();

    QPlainTextEdit* m_text = nullptr;
    QCheckBox* m_diag = nullptr;
    QPushButton* m_submit = nullptr;
    QLabel* m_status = nullptr;
};
