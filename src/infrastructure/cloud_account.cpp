#include "infrastructure/cloud_account.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUuid>
#include <QUrl>

namespace {
// 正式入口：pavo 的 Tailscale Funnel（自带 Let's Encrypt 证书）。
// 想改地址：QSettings("LumenArc","LumenArc") 的 cloud/baseUrl，或环境变量 LUMENARC_AUTH_URL。
constexpr const char* kDefaultBase = "https://pavo-850-aiga.taild709ca.ts.net";
}  // namespace

CloudAccount& CloudAccount::instance() {
    static CloudAccount s;
    return s;
}

QString CloudAccount::baseUrl() {
    const QByteArray env = qgetenv("LUMENARC_AUTH_URL");
    if (!env.isEmpty())
        return QString::fromUtf8(env);
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    const QString v = s.value(QStringLiteral("cloud/baseUrl")).toString();
    return v.isEmpty() ? QString::fromLatin1(kDefaultBase) : v;
}

void CloudAccount::setBaseUrl(const QString& url) {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.setValue(QStringLiteral("cloud/baseUrl"), url);
}

QString CloudAccount::deviceId() {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    QString id = s.value(QStringLiteral("cloud/deviceId")).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(QStringLiteral("cloud/deviceId"), id);
    }
    return id;
}

void CloudAccount::postJson(const QString& path, const QJsonObject& body, Callback cb) {
    QUrl url(baseUrl() + path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json; charset=utf-8"));
    req.setRawHeader("X-Device-Id", deviceId().toUtf8());
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = m_nam.post(req, payload);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, cb]() {
        reply->deleteLater();
        Result r;
        const QByteArray raw = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            r.error = reply->error() == QNetworkReply::TimeoutError
                          ? QStringLiteral("timeout")
                          : QStringLiteral("network");
            r.message = reply->errorString();
            cb(r);
            return;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            r.error = QStringLiteral("bad_response");
            r.message = QString::fromUtf8(raw.left(200));
            cb(r);
            return;
        }
        const QJsonObject o = doc.object();
        r.ok = o.value(QStringLiteral("ok")).toBool(false);
        r.error = o.value(QStringLiteral("error")).toString();
        r.message = o.value(QStringLiteral("message")).toString();
        r.data = o;
        cb(r);
    });
}

void CloudAccount::sendEmailCode(const QString& email, Callback cb) {
    postJson(QStringLiteral("/auth/send-code"),
             QJsonObject{{QStringLiteral("email"), email}}, std::move(cb));
}

void CloudAccount::registerByEmail(const QString& email, const QString& code,
                                   const QString& name, const QString& org,
                                   const QString& password, Callback cb) {
    postJson(QStringLiteral("/auth/register-email"),
             QJsonObject{{QStringLiteral("email"), email},
                         {QStringLiteral("code"), code},
                         {QStringLiteral("name"), name},
                         {QStringLiteral("org"), org},
                         {QStringLiteral("password"), password}},
             std::move(cb));
}

void CloudAccount::registerByInvite(const QString& batchCode, const QString& account,
                                    const QString& name, const QString& org,
                                    const QString& password, Callback cb) {
    postJson(QStringLiteral("/auth/register-invite"),
             QJsonObject{{QStringLiteral("code"), batchCode},
                         {QStringLiteral("account"), account},
                         {QStringLiteral("name"), name},
                         {QStringLiteral("org"), org},
                         {QStringLiteral("password"), password}},
             std::move(cb));
}

void CloudAccount::login(const QString& account, const QString& password, Callback cb) {
    postJson(QStringLiteral("/auth/login"),
             QJsonObject{{QStringLiteral("account"), account},
                         {QStringLiteral("password"), password}},
             std::move(cb));
}

void CloudAccount::setPassword(const QString& token, const QString& password, Callback cb) {
    postJson(QStringLiteral("/auth/set-password"),
             QJsonObject{{QStringLiteral("token"), token},
                         {QStringLiteral("password"), password}},
             std::move(cb));
}

void CloudAccount::updateProfile(const QString& token, const QString& password,
                                 const QString& name, const QString& org, Callback cb) {
    postJson(QStringLiteral("/auth/update-profile"),
             QJsonObject{{QStringLiteral("token"), token},
                         {QStringLiteral("password"), password},
                         {QStringLiteral("name"), name},
                         {QStringLiteral("org"), org}},
             std::move(cb));
}

void CloudAccount::heartbeat(const QString& token, Callback cb) {
    postJson(QStringLiteral("/auth/heartbeat"),
             QJsonObject{{QStringLiteral("token"), token},
                         {QStringLiteral("version"), QStringLiteral(APP_VERSION)},
                         {QStringLiteral("platform"), QStringLiteral("Windows")}},
             std::move(cb));
}

void CloudAccount::submitFeedback(const QString& token, const QString& text,
                                  const QJsonObject& diag, Callback cb) {
    postJson(QStringLiteral("/feedback"),
             QJsonObject{{QStringLiteral("token"), token},
                         {QStringLiteral("text"), text},
                         {QStringLiteral("diag"), diag}},
             std::move(cb));
}
