#include "infrastructure/signature_store.h"

#include <QSettings>

QString SignatureStore::name() {
    return QSettings(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"))
        .value(QStringLiteral("signature/name"))
        .toString();
}

QString SignatureStore::org() {
    return QSettings(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"))
        .value(QStringLiteral("signature/org"))
        .toString();
}

void SignatureStore::save(const QString& name, const QString& org) {
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.setValue(QStringLiteral("signature/name"), name.trimmed());
    s.setValue(QStringLiteral("signature/org"), org.trimmed());
}

bool SignatureStore::empty() { return name().isEmpty() && org().isEmpty(); }

bool SignatureStore::initIfEmpty(const QString& name, const QString& org) {
    if (!empty()) return false;
    if (name.trimmed().isEmpty() && org.trimmed().isEmpty()) return false;
    save(name, org);
    return true;
}
