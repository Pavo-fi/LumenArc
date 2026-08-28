#include "infrastructure/cloud_account.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUuid>

namespace {
// 环境常量（部署实况见 docs/cloudbase/README.md）
const char* kEnvId = "lumenarc-prod-d6gcdfb6a8873d906";
const char* kGateway = "https://lumenarc-prod-d6gcdfb6a8873d906.api.tcloudbasegateway.com/auth/v1";
const char* kService = "https://lumenarc-prod-d6gcdfb6a8873d906.service.tcloudbase.com";
constexpr int kTimeoutMs = 15000;

QString withClientId(const QString& url) { return url + QStringLiteral("?client_id=") + QLatin1String(kEnvId); }
}  // namespace

CloudAccount& CloudAccount::instance() {
    static CloudAccount inst;
    return inst;
}

QString CloudAccount::deviceId() {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    QString id = s.value(QStringLiteral("account/deviceId")).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(QStringLiteral("account/deviceId"), id);
    }
    return id;
}

void CloudAccount::post(const QString& url, const QJsonObject& body, bool gateway, Callback cb) {
    QNetworkRequest req{ QUrl(url) };
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (gateway) {
        req.setRawHeader("x-device-id", deviceId().toUtf8());
        req.setRawHeader("X-SDK-Version", "lumenarc-cpp/1");
    }
    QNetworkReply* rep = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QTimer* timer = new QTimer(rep);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, rep, [rep]() {
        rep->setProperty("lumenarc_timeout", true);
        rep->abort();
    });
    timer->start(kTimeoutMs);
    QObject::connect(rep, &QNetworkReply::finished, rep, [rep, timer, cb]() {
        timer->deleteLater();
        rep->deleteLater();
        Result r;
        if (rep->property("lumenarc_timeout").toBool()) {
            r.error = QStringLiteral("timeout");
            r.message = QStringLiteral("网络超时，请检查网络后重试");
            cb(r);
            return;
        }
        const QByteArray raw = rep->readAll();
        if (rep->error() != QNetworkReply::NoError && raw.isEmpty()) {
            r.error = QStringLiteral("network");
            r.message = QStringLiteral("网络连接失败，请检查网络");
            cb(r);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        const QJsonObject j = doc.isObject() ? doc.object() : QJsonObject();
        // 服务端业务错误（gateway: code+error_description；自家函数: error 字段）
        const QString code = j.value(QStringLiteral("code")).toString();
        if (!code.isEmpty() && code != QLatin1String("SUCCESS")) {
            r.error = code;
            r.message = j.value(QStringLiteral("error_description")).toString();
            if (r.message.isEmpty()) r.message = j.value(QStringLiteral("message")).toString();
            if (r.message.isEmpty()) r.message = j.value(QStringLiteral("error")).toString();
            r.data = j;
            cb(r);
            return;
        }
        const QString myErr = j.value(QStringLiteral("error")).toString();
        if (!myErr.isEmpty()) {
            r.error = myErr;
            r.message = myErr;
            r.data = j;
            cb(r);
            return;
        }
        r.ok = true;
        r.data = j;
        cb(r);
    });
}

void CloudAccount::get(const QString& url, const QString& bearer, bool gateway, Callback cb) {
    QNetworkRequest req{ QUrl(url) };
    if (!bearer.isEmpty()) req.setRawHeader("Authorization", "Bearer " + bearer.toUtf8());
    if (gateway) req.setRawHeader("x-device-id", deviceId().toUtf8());
    QNetworkReply* rep = m_nam.get(req);
    QTimer* timer = new QTimer(rep);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, rep, [rep]() {
        rep->setProperty("lumenarc_timeout", true);
        rep->abort();
    });
    timer->start(kTimeoutMs);
    QObject::connect(rep, &QNetworkReply::finished, rep, [rep, timer, cb]() {
        timer->deleteLater();
        rep->deleteLater();
        Result r;
        if (rep->property("lumenarc_timeout").toBool()) {
            r.error = QStringLiteral("timeout");
            cb(r);
            return;
        }
        const QByteArray raw = rep->readAll();
        const QJsonObject j = QJsonDocument::fromJson(raw).object();
        if (rep->error() != QNetworkReply::NoError && j.isEmpty()) {
            r.error = QStringLiteral("network");
            cb(r);
            return;
        }
        if (j.contains(QStringLiteral("code"))) {
            r.error = j.value(QStringLiteral("code")).toString();
            r.message = j.value(QStringLiteral("error_description")).toString();
            cb(r);
            return;
        }
        r.ok = true;
        r.data = j;
        cb(r);
    });
}

// ---- 短信：发码 ----
void CloudAccount::sendSmsCode(const QString& phone11, Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("phone_number"), QStringLiteral("+86 ") + phone11);
    post(withClientId(QString::fromLatin1(kGateway) + QStringLiteral("/verification")), body, true, std::move(cb));
}

// ---- 短信：验码并登录 ----
void CloudAccount::signInWithSms(const QString& phone11, const QString& code, Callback cb) {
    const QString base = withClientId(QString::fromLatin1(kGateway));
    QJsonObject verifyBody;
    verifyBody.insert(QStringLiteral("phone_number"), phone11);
    verifyBody.insert(QStringLiteral("verification_code"), code);
    post(base + QStringLiteral("/verification/verify"), verifyBody, true,
         [this, cb, base, phone11, code](const Result& vr) mutable {
             if (!vr.ok) {
                 cb(vr);
                 return;
             }
             const QString vtoken = vr.data.value(QStringLiteral("verification_token")).toString();
             const bool isUser = vr.data.value(QStringLiteral("is_user")).toBool(true);
             if (vtoken.isEmpty()) {
                 Result r;
                 r.error = QStringLiteral("no_verification_token");
                 r.message = QStringLiteral("验证码校验失败");
                 cb(r);
                 return;
             }
             // 登录；新用户先 signup 再 signin
             auto doSignIn = [this, cb, base, phone11, vtoken]() mutable {
                 QJsonObject in;
                 in.insert(QStringLiteral("username"), QStringLiteral("+86 ") + phone11);
                 in.insert(QStringLiteral("verification_token"), vtoken);
                 post(base + QStringLiteral("/signin"), in, true, [cb](const Result& sr) {
                     if (sr.ok && sr.data.value(QStringLiteral("access_token")).toString().isEmpty()) {
                         Result r = sr;
                         r.ok = false;
                         r.error = QStringLiteral("no_access_token");
                         r.message = QStringLiteral("登录响应异常");
                         cb(r);
                         return;
                     }
                     cb(sr);
                 });
             };
             if (isUser) {
                 doSignIn();
             } else {
                 QJsonObject up;
                 up.insert(QStringLiteral("phone_number"), QStringLiteral("+86 ") + phone11);
                 up.insert(QStringLiteral("verification_token"), vtoken);
                 up.insert(QStringLiteral("verification_code"), code);
                 post(base + QStringLiteral("/signup"), up, true,
                      [doSignIn, cb](const Result& ur) mutable {
                          if (!ur.ok && ur.error != QLatin1String("ALREADY_EXISTS")) {
                              // signup 失败也尝试直接 signin（is_user 标记不可尽信）
                          }
                          doSignIn();
                      });
             }
         });
}

// ---- 自家云函数 ----
void CloudAccount::registerAccount(const QString& accessToken, const QString& name, const QString& org,
                                   Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("access_token"), accessToken);
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("org"), org);
    post(QString::fromLatin1(kService) + QStringLiteral("/authRegister"), body, false, std::move(cb));
}

void CloudAccount::heartbeat(const QString& token, Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("token"), token);
    body.insert(QStringLiteral("version"), QStringLiteral(APP_VERSION));
    body.insert(QStringLiteral("platform"), QStringLiteral("win64"));
    post(QString::fromLatin1(kService) + QStringLiteral("/authHeartbeat"), body, false, std::move(cb));
}

void CloudAccount::activateInvite(const QString& code, const QString& name, const QString& org, Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("code"), code);
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("org"), org);
    post(QString::fromLatin1(kService) + QStringLiteral("/inviteActivate"), body, false, std::move(cb));
}

void CloudAccount::submitFeedback(const QString& token, const QString& text, const QJsonObject& diag,
                                  Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("token"), token);
    body.insert(QStringLiteral("text"), text.left(4000));
    body.insert(QStringLiteral("diag"), diag);
    post(QString::fromLatin1(kService) + QStringLiteral("/feedback"), body, false, std::move(cb));
}
