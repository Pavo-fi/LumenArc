#include "infrastructure/cloud_account.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <memory>
#include <functional>

namespace {
// 环境常量（部署实况见 docs/cloudbase/README.md）；LUMENARC_CLOUD_* env 可覆盖（联调/抓包用）
const char* kEnvId = "lumenarc-prod-d6gcdfb6a8873d906";
QString gatewayBase() {
    const QByteArray ov = qgetenv("LUMENARC_CLOUD_GATEWAY");
    return ov.isEmpty() ? QStringLiteral("https://lumenarc-prod-d6gcdfb6a8873d906.api.tcloudbasegateway.com/auth/v1")
                        : QString::fromUtf8(ov);
}
QString serviceBase() {
    const QByteArray ov = qgetenv("LUMENARC_CLOUD_SERVICE");
    return ov.isEmpty() ? QStringLiteral("https://lumenarc-prod-d6gcdfb6a8873d906.service.tcloudbase.com")
                        : QString::fromUtf8(ov);
}
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
#ifdef LUMENARC_CLOUD_DEBUG
        fprintf(stderr, "[cloud] POST %s -> status=%d\n  raw=%s\n",
                qUtf8Printable(rep->url().toString()),
                rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                qUtf8Printable(QString::fromUtf8(raw.left(600))));
        for (const auto& h : rep->rawHeaderPairs())
            fprintf(stderr, "  hdr %s: %s\n", h.first.constData(), h.second.constData());
        fflush(stderr);
#endif
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
    post(withClientId(gatewayBase() + QStringLiteral("/verification")), body, true,
         [this, cb = std::move(cb)](const Result& r) mutable {
             if (r.ok) m_lastVerificationId = r.data.value(QStringLiteral("verification_id")).toString();
             cb(r);
         });
}

// ---- 短信：验码并登录 ----
void CloudAccount::signInWithSms(const QString& phone11, const QString& code, Callback cb) {
    // 注意：withClientId 会加 ?client_id=，必须每个端点拼接后再加（此前 base 带 query
    // 再拼路径导致路径进 query 的真机坑）；gb 必须按值捕获（异步回调时栈已销毁，引用捕获悬空）
    const auto ep = [gb = gatewayBase()](const QString& path) { return withClientId(gb + path); };
    QJsonObject verifyBody;
    verifyBody.insert(QStringLiteral("phone_number"), phone11);
    verifyBody.insert(QStringLiteral("verification_code"), code);
    if (!m_lastVerificationId.isEmpty())
        verifyBody.insert(QStringLiteral("verification_id"), m_lastVerificationId);
    post(ep(QStringLiteral("/verification/verify")), verifyBody, true,
         [this, cb, ep, phone11, code](const Result& vr) mutable {
             if (!vr.ok) {
                 cb(vr);
                 return;
             }
             const QString vtoken = vr.data.value(QStringLiteral("verification_token")).toString();
             // 联调实锤（2026-08-28 真机）：verify 响应不带 is_user；signin 对新用户返
             // NOT_FOUND；signup 成功直接带 access_token。策略：先 signin，遇 NOT_FOUND
             // 走 signup，signup 无 token 时再 signin 一次。
             auto finish = [cb](const Result& sr) {
                 if (sr.ok && sr.data.value(QStringLiteral("access_token")).toString().isEmpty()) {
                     Result r = sr;
                     r.ok = false;
                     r.error = QStringLiteral("no_access_token");
                     r.message = QStringLiteral("登录响应异常");
                     cb(r);
                     return;
                 }
                 cb(sr);
             };
             auto doSignUp = std::make_shared<std::function<void()>>();
             auto doSignIn = std::make_shared<std::function<void()>>();
             *doSignUp = [this, cb, ep, phone11, code, vtoken, finish, doSignIn]() mutable {
                 QJsonObject up;
                 up.insert(QStringLiteral("phone_number"), QStringLiteral("+86 ") + phone11);
                 up.insert(QStringLiteral("verification_token"), vtoken);
                 up.insert(QStringLiteral("verification_code"), code);
                 post(ep(QStringLiteral("/signup")), up, true,
                      [cb, finish, doSignIn](const Result& ur) mutable {
                          if (ur.ok && !ur.data.value(QStringLiteral("access_token")).toString().isEmpty()) {
                              finish(ur);   // signup 直接带 token（新用户即登录）
                          } else {
                              (*doSignIn)();// 兜底：signup 后补 signin
                          }
                      });
             };
             *doSignIn = [this, cb, ep, phone11, vtoken, finish, doSignUp]() mutable {
                 QJsonObject in;
                 in.insert(QStringLiteral("username"), QStringLiteral("+86 ") + phone11);
                 in.insert(QStringLiteral("verification_token"), vtoken);
                 post(ep(QStringLiteral("/signin")), in, true,
                      [cb, finish, doSignUp](const Result& sr) mutable {
                          if (!sr.ok && sr.error == QLatin1String("NOT_FOUND")) {
                              (*doSignUp)();  // 新用户：先注册
                              return;
                          }
                          finish(sr);
                      });
             };
             (*doSignIn)();
         });
}

// ---- 自家云函数 ----
void CloudAccount::registerAccount(const QString& accessToken, const QString& name, const QString& org,
                                   Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("access_token"), accessToken);
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("org"), org);
    post(serviceBase() + QStringLiteral("/authRegister"), body, false, std::move(cb));
}

void CloudAccount::heartbeat(const QString& token, Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("token"), token);
    body.insert(QStringLiteral("version"), QStringLiteral(APP_VERSION));
    body.insert(QStringLiteral("platform"), QStringLiteral("win64"));
    post(serviceBase() + QStringLiteral("/authHeartbeat"), body, false, std::move(cb));
}

void CloudAccount::activateInvite(const QString& code, const QString& name, const QString& org, Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("code"), code);
    body.insert(QStringLiteral("name"), name);
    body.insert(QStringLiteral("org"), org);
    post(serviceBase() + QStringLiteral("/inviteActivate"), body, false, std::move(cb));
}

void CloudAccount::submitFeedback(const QString& token, const QString& text, const QJsonObject& diag,
                                  Callback cb) {
    QJsonObject body;
    body.insert(QStringLiteral("token"), token);
    body.insert(QStringLiteral("text"), text.left(4000));
    body.insert(QStringLiteral("diag"), diag);
    post(serviceBase() + QStringLiteral("/feedback"), body, false, std::move(cb));
}
