#include "infrastructure/credential_store.h"

#include <QSettings>

namespace {
const char* kGroup = "account";
}

Credential CredentialStore::load() {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.beginGroup(QLatin1String(kGroup));
    Credential c;
    c.token = s.value(QStringLiteral("token")).toString();
    c.expiresAtMs = s.value(QStringLiteral("expiresAt")).toLongLong();
    c.lastOkAtMs = s.value(QStringLiteral("lastOkAt")).toLongLong();
    c.uid = s.value(QStringLiteral("uid")).toString();
    c.name = s.value(QStringLiteral("name")).toString();
    c.org = s.value(QStringLiteral("org")).toString();
    s.endGroup();
    return c;
}

void CredentialStore::save(const Credential& c) {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.beginGroup(QLatin1String(kGroup));
    s.setValue(QStringLiteral("token"), c.token);
    s.setValue(QStringLiteral("expiresAt"), c.expiresAtMs);
    s.setValue(QStringLiteral("lastOkAt"), c.lastOkAtMs);
    s.setValue(QStringLiteral("uid"), c.uid);
    s.setValue(QStringLiteral("name"), c.name);
    s.setValue(QStringLiteral("org"), c.org);
    s.endGroup();
}

void CredentialStore::clear() {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.beginGroup(QLatin1String(kGroup));
    s.remove(QStringLiteral(""));
    s.endGroup();
}

void CredentialStore::touchOk(qint64 nowMs) {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.beginGroup(QLatin1String(kGroup));
    s.setValue(QStringLiteral("lastOkAt"), nowMs);
    s.endGroup();
}

CredentialStore::Verdict CredentialStore::startupVerdict(const Credential& c, qint64 nowMs) {
    if (!c.valid()) return Verdict::NeedLogin;
    if (c.expiresAtMs > 0 && nowMs >= c.expiresAtMs) return Verdict::NeedLogin;      // token 过期
    if (c.lastOkAtMs <= 0 || nowMs - c.lastOkAtMs >= kReverifyWindowMs)               // 超 30 天未在线验证
        return Verdict::NeedLogin;
    return Verdict::Pass;
}
