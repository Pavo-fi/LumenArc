#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class QLabel;
class QTabWidget;
class QTimer;

// 启动登录闸（账号系统 v1）：手机号+短信验证码 / 邀请码 两个通道。
// 模态；登录成功 accept() 并把凭证写入 CredentialStore；用户关闭 = 放弃启动。
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget* parent = nullptr);

private:
    void setBusy(bool busy, const QString& hint = QString());
    void fail(const QString& machineCode, const QString& serverMsg);
    void succeedWithToken(const QString& token, qint64 expiresAtMs, const QString& uid,
                          const QString& name, const QString& org);

    void onSendCode();
    void onSmsLogin();
    void onInviteActivate();

    QTabWidget* m_tabs = nullptr;
    // 手机页
    QLineEdit* m_phone = nullptr;
    QLineEdit* m_code = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_org = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_loginBtn = nullptr;
    // 邀请码页
    QLineEdit* m_invCode = nullptr;
    QLineEdit* m_invName = nullptr;
    QLineEdit* m_invOrg = nullptr;
    QPushButton* m_invBtn = nullptr;

    QLabel* m_status = nullptr;
    QTimer* m_countdown = nullptr;
    int m_countdownLeft = 0;
};
