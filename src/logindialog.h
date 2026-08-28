#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class QLabel;
class QTabWidget;
class QTimer;
class QWidget;

// 启动登录闸（账号系统 v1.1，两层流程）：
//   第一层：手机号 + 短信验证码（或邀请码页签）；
//   第二层：仅在服务端判定未注册时出现——补填姓名/单位
//          （提示：将自动用于案件录入与分析报告生成）。
// 模态盖在主窗上；登录成功 accept() 并写 CredentialStore；用户关闭 = 退出程序。
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget* parent = nullptr);

private:
    void setBusy(bool busy, const QString& hint = QString());
    void fail(const QString& machineCode, const QString& serverMsg);
    void succeedWithToken(const QString& token, qint64 expiresAtMs, const QString& uid,
                          const QString& name, const QString& org);
    void showRegistrationLayer(bool show);  // 第二层显隐

    void onSendCode();
    void onSmsLogin();              // 第一层「登录」
    void onCompleteRegistration();  // 第二层「完成注册」
    void onInviteActivate();

    QTabWidget* m_tabs = nullptr;
    // 手机页：第一层
    QLineEdit* m_phone = nullptr;
    QLineEdit* m_code = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QPushButton* m_loginBtn = nullptr;
    // 手机页：第二层（默认隐藏）
    QWidget* m_regLayer = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_org = nullptr;
    QPushButton* m_registerBtn = nullptr;
    // 邀请码页
    QLineEdit* m_invCode = nullptr;
    QLineEdit* m_invName = nullptr;
    QLineEdit* m_invOrg = nullptr;
    QPushButton* m_invBtn = nullptr;

    QLabel* m_status = nullptr;
    QTimer* m_countdown = nullptr;
    int m_countdownLeft = 0;
};
