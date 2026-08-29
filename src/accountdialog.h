#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class QLabel;
class QTimer;
class QGroupBox;

// 账号管理（帮助菜单进入）：当前账号展示 + 修改姓名/单位（手机号验证码验证）
// + 退出登录。邀请码账号无手机号，修改区灰置（资料由提供方管理）。
class AccountDialog : public QDialog {
    Q_OBJECT
public:
    explicit AccountDialog(QWidget* parent = nullptr);
    bool signedOut() const { return m_signedOut; }

private:
    void onSendCode();
    void onSubmitProfile();
    void onSignOut();
    void setBusy(bool busy, const QString& hint = QString());
    void fail(const QString& machineCode, const QString& serverMsg);

    QString m_uid;    // 手机号（+86 …）或 invite:<code>
    bool m_isInvite = false;

    QLabel* m_infoLabel = nullptr;
    QGroupBox* m_editBox = nullptr;
    QLineEdit* m_code = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_org = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_submitBtn = nullptr;
    QPushButton* m_signoutBtn = nullptr;
    QLabel* m_status = nullptr;
    QTimer* m_countdown = nullptr;
    int m_countdownLeft = 0;
    bool m_signedOut = false;
};
