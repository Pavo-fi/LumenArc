#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QString>

#include <functional>

// 云端账号 API 封装（v2：自建 pavo 服务，替代已停用的 CloudBase 短信）。
//
// 服务端：pavo 上的 lumenarc-auth（tools/auth_service/lumenarc_auth.py）
//   POST /auth/send-code        {email}                        发邮箱验证码
//   POST /auth/register-email   {email,code,name,org,password}  通道 A 自助注册
//   POST /auth/register-invite  {code,account,name,org,password} 通道 B 邀请码/批次码
//   POST /auth/login            {account,password}
//   POST /auth/set-password     {token,password}
//   POST /auth/update-profile   {token,password,name,org}
//   POST /auth/heartbeat        {token,version,platform}        顺带续期
//   POST /feedback              {token,text,diag}
// 全部异步回调；网络错误归一化为 Result.error（"network"/"timeout"/服务端 code）。
class CloudAccount : public QObject {
    Q_OBJECT
public:
    struct Result {
        bool ok = false;
        QString error;          // 机器码：network / timeout / 服务端 code
        QString message;        // 人类可读
        QJsonObject data;       // 成功时的 JSON（含 token / expiresAt / name / org）
    };
    using Callback = std::function<void(const Result&)>;

    static CloudAccount& instance();

    /// 服务地址：QSettings "cloud/baseUrl" 优先，环境变量 LUMENARC_AUTH_URL 次之，
    /// 最后是内置默认（局域网地址，出正式域名后改这一处即可）。
    static QString baseUrl();
    static void setBaseUrl(const QString& url);

    QString deviceId();  // 稳定设备指纹（首启生成 UUID 存 QSettings）

    void sendEmailCode(const QString& email, Callback cb);
    void registerByEmail(const QString& email, const QString& code,
                         const QString& name, const QString& org,
                         const QString& password, Callback cb);
    void registerByInvite(const QString& batchCode, const QString& account,
                          const QString& name, const QString& org,
                          const QString& password, Callback cb);
    void login(const QString& account, const QString& password, Callback cb);
    void setPassword(const QString& token, const QString& password, Callback cb);
    void updateProfile(const QString& token, const QString& password,
                       const QString& name, const QString& org, Callback cb);
    void heartbeat(const QString& token, Callback cb);
    void submitFeedback(const QString& token, const QString& text,
                        const QJsonObject& diag, Callback cb);

private:
    CloudAccount() = default;
    void postJson(const QString& path, const QJsonObject& body, Callback cb);
    QNetworkAccessManager m_nam;
};
