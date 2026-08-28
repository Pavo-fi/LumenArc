#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>

#include <functional>

// 云端账号 API 封装（账号系统 v1）。
// 两条链路：
//   1) 短信验证码：客户端直连 CloudBase 身份验证 HTTP API（网关），
//      发码 -> 验码(verification_token) -> 登录(access_token)；
//   2) 自家云函数（HTTP 触发器）：注册/心跳/邀请码/反馈，HMAC token 30 天。
// 全部异步回调；网络错误归一化为 Result.error（"network"/"timeout"/服务端 code）。
class CloudAccount : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        QString error;          // 机器码：network / timeout / 服务端 code
        QString message;        // 人类可读（服务端 message/error_description 或本地描述）
        QJsonObject data;       // 成功时的 JSON
    };
    using Callback = std::function<void(const Result&)>;

    static CloudAccount& instance();

    QString deviceId();  // 稳定设备指纹（首启生成 UUID 存 QSettings）

    // ---- CloudBase 身份验证（短信）----
    void sendSmsCode(const QString& phone11, Callback cb);
    // 验码并登录（含新用户自动注册），成功 data 含 accessToken
    void signInWithSms(const QString& phone11, const QString& code, Callback cb);

    // ---- 自家云函数 ----
    void registerAccount(const QString& accessToken, const QString& name, const QString& org, Callback cb);
    void heartbeat(const QString& token, Callback cb);
    void activateInvite(const QString& code, const QString& name, const QString& org, Callback cb);
    void submitFeedback(const QString& token, const QString& text, const QJsonObject& diag, Callback cb);

    QString m_lastVerificationId;  // sendSmsCode 成功后缓存；verify 必须回传（网关实锤）

private:
    CloudAccount() = default;
    void post(const QString& url, const QJsonObject& body, bool gateway, Callback cb);
    void get(const QString& url, const QString& bearer, bool gateway, Callback cb);

    QNetworkAccessManager m_nam;
};
