#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;
class QLabel;
class QTabWidget;
class QTimer;

// 启动登录闸（账号系统 v2：pavo 自建服务，不再依赖短信）：
//   页签①「账号登录」：账号（手机号或邮箱）+ 密码 —— 日常入口，离线 token 有效期内不弹
//   页签②「邮箱注册」：邮箱验证码 + 姓名/单位 + 密码 —— 通道 A 自助注册
//   页签③「邀请码激活」：批次码 + 手机号 + 姓名/单位 + 密码 —— 通道 B，培训班/内部发放
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

    void onLogin();
    void onSendCode();
    void onRegisterEmail();
    void onInviteActivate();

    QTabWidget* m_tabs = nullptr;

    // 页签①：账号登录
    QLineEdit* m_account = nullptr;
    QLineEdit* m_password = nullptr;
    QPushButton* m_loginBtn = nullptr;

    // 页签②：邮箱注册
    QLineEdit* m_email = nullptr;
    QLineEdit* m_code = nullptr;
    QPushButton* m_sendBtn = nullptr;
    QLineEdit* m_regName = nullptr;
    QLineEdit* m_regOrg = nullptr;
    QLineEdit* m_regPwd = nullptr;
    QLineEdit* m_regPwd2 = nullptr;
    QPushButton* m_regBtn = nullptr;

    // 页签③：邀请码激活
    QLineEdit* m_invCode = nullptr;
    QLineEdit* m_invAccount = nullptr;
    QLineEdit* m_invName = nullptr;
    QLineEdit* m_invOrg = nullptr;
    QLineEdit* m_invPwd = nullptr;
    QPushButton* m_invBtn = nullptr;

    QPushButton* m_quitBtn = nullptr;   // busy 期间禁用（防在途请求回调悬空）

    QLabel* m_status = nullptr;
    QTimer* m_countdown = nullptr;
    int m_countdownLeft = 0;
};
