#include "logindialog.h"

#include "infrastructure/cloud_account.h"
#include "infrastructure/credential_store.h"

#include <QDateTime>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString tr_err(const QString& code, const QString& serverMsg) {
    if (code == QLatin1String("network") || code == QLatin1String("timeout"))
        return QStringLiteral("网络连接失败，请检查网络后重试");
    if (code == QLatin1String("INVALID_ARGUMENT") || code == QLatin1String("sms_code_invalid"))
        return QStringLiteral("验证码错误或已过期，请重新获取");
    if (code == QLatin1String("LIMIT_EXCEEDED") || code == QLatin1String("OPERATION_TOO_FREQUENT"))
        return QStringLiteral("操作太频繁，请稍后再试");
    if (code == QLatin1String("invite_invalid")) return QStringLiteral("邀请码不存在");
    if (code == QLatin1String("invite_used")) return QStringLiteral("邀请码已被使用，请联系提供方");
    if (code == QLatin1String("invite_disabled")) return QStringLiteral("邀请码已停用");
    if (code == QLatin1String("user_disabled")) return QStringLiteral("该账号已停用，请联系提供方");
    if (code == QLatin1String("cloudbase_token_invalid")) return QStringLiteral("短信登录态失效，请重新获取验证码");
    if (code == QLatin1String("missing_fields")) return QStringLiteral("请填写完整信息");
    return serverMsg.isEmpty() ? QStringLiteral("失败：") + code
                               : QStringLiteral("失败：") + serverMsg;
}
}  // namespace

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("追光者 Lumen Arc — 账号登录"));
    setModal(true);
    setMinimumWidth(420);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(QStringLiteral("本软件需登录后使用。首次使用请用手机号注册（免费）；"
                                            "无网络环境请使用邀请码。"), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_tabs = new QTabWidget(this);

    // ---- 手机号页 ----
    auto* phonePage = new QWidget(this);
    auto* pf = new QFormLayout(phonePage);
    m_phone = new QLineEdit(phonePage);
    m_phone->setPlaceholderText(QStringLiteral("11 位手机号"));
    m_phone->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\d{0,11}")), m_phone));
    auto* codeRow = new QWidget(phonePage);
    auto* cr = new QHBoxLayout(codeRow);
    cr->setContentsMargins(0, 0, 0, 0);
    m_code = new QLineEdit(codeRow);
    m_code->setPlaceholderText(QStringLiteral("6 位短信验证码"));
    m_sendBtn = new QPushButton(QStringLiteral("发送验证码"), codeRow);
    cr->addWidget(m_code, 1);
    cr->addWidget(m_sendBtn);
    m_name = new QLineEdit(phonePage);
    m_name->setPlaceholderText(QStringLiteral("您的姓名"));
    m_org = new QLineEdit(phonePage);
    m_org->setPlaceholderText(QStringLiteral("单位名称"));
    m_loginBtn = new QPushButton(QStringLiteral("登 录"), phonePage);
    m_loginBtn->setDefault(true);
    pf->addRow(QStringLiteral("手机号"), m_phone);
    pf->addRow(QStringLiteral("验证码"), codeRow);
    pf->addRow(QStringLiteral("姓 名"), m_name);
    pf->addRow(QStringLiteral("单 位"), m_org);
    pf->addRow(QString(), m_loginBtn);
    m_tabs->addTab(phonePage, QStringLiteral("手机号登录"));

    // ---- 邀请码页 ----
    auto* invPage = new QWidget(this);
    auto* inf = new QFormLayout(invPage);
    m_invCode = new QLineEdit(invPage);
    m_invCode->setPlaceholderText(QStringLiteral("如 LA-XXXX-XXXX"));
    m_invName = new QLineEdit(invPage);
    m_invName->setPlaceholderText(QStringLiteral("您的姓名"));
    m_invOrg = new QLineEdit(invPage);
    m_invOrg->setPlaceholderText(QStringLiteral("单位名称"));
    m_invBtn = new QPushButton(QStringLiteral("激活并登录"), invPage);
    inf->addRow(QStringLiteral("邀请码"), m_invCode);
    inf->addRow(QStringLiteral("姓 名"), m_invName);
    inf->addRow(QStringLiteral("单 位"), m_invOrg);
    inf->addRow(QString(), m_invBtn);
    m_tabs->addTab(invPage, QStringLiteral("邀请码激活"));

    root->addWidget(m_tabs);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
    root->addWidget(m_status);

    auto* quitBtn = new QPushButton(QStringLiteral("退出程序"), this);
    root->addWidget(quitBtn, 0, Qt::AlignRight);
    connect(quitBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_sendBtn, &QPushButton::clicked, this, &LoginDialog::onSendCode);
    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::onSmsLogin);
    connect(m_invBtn, &QPushButton::clicked, this, &LoginDialog::onInviteActivate);

    m_countdown = new QTimer(this);
    connect(m_countdown, &QTimer::timeout, this, [this]() {
        if (--m_countdownLeft <= 0) {
            m_countdown->stop();
            m_sendBtn->setEnabled(true);
            m_sendBtn->setText(QStringLiteral("发送验证码"));
        } else {
            m_sendBtn->setText(QStringLiteral("重新发送(%1s)").arg(m_countdownLeft));
        }
    });
}

void LoginDialog::setBusy(bool busy, const QString& hint) {
    m_tabs->setEnabled(!busy);
    m_loginBtn->setEnabled(!busy);
    m_invBtn->setEnabled(!busy);
    if (busy) {
        m_status->setStyleSheet(QStringLiteral("color:#666;"));
        m_status->setText(hint.isEmpty() ? QStringLiteral("处理中…") : hint);
    }
}

void LoginDialog::fail(const QString& machineCode, const QString& serverMsg) {
    setBusy(false);
    m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
    m_status->setText(tr_err(machineCode, serverMsg));
}

void LoginDialog::succeedWithToken(const QString& token, qint64 expiresAtMs, const QString& uid,
                                   const QString& name, const QString& org) {
    Credential c;
    c.token = token;
    c.expiresAtMs = expiresAtMs;
    c.lastOkAtMs = QDateTime::currentMSecsSinceEpoch();
    c.uid = uid;
    c.name = name;
    c.org = org;
    CredentialStore::save(c);
    accept();
}

void LoginDialog::onSendCode() {
    const QString phone = m_phone->text().trimmed();
    if (phone.length() != 11 || !phone.startsWith(QLatin1Char('1'))) {
        fail(QStringLiteral("bad_phone"), QStringLiteral("请输入正确的 11 位手机号"));
        return;
    }
    setBusy(true, QStringLiteral("正在发送验证码…"));
    CloudAccount::instance().sendSmsCode(phone, [this](const CloudAccount::Result& r) {
        setBusy(false);
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        m_status->setStyleSheet(QStringLiteral("color:#27ae60;"));
        m_status->setText(QStringLiteral("验证码已发送，请查收短信（5 分钟内有效）"));
        m_countdownLeft = 60;
        m_sendBtn->setEnabled(false);
        m_countdown->start(1000);
    });
}

void LoginDialog::onSmsLogin() {
    const QString phone = m_phone->text().trimmed();
    const QString code = m_code->text().trimmed();
    const QString name = m_name->text().trimmed();
    const QString org = m_org->text().trimmed();
    if (phone.length() != 11) { fail(QStringLiteral("bad_phone"), QStringLiteral("请输入正确的 11 位手机号")); return; }
    if (code.length() < 4) { fail(QStringLiteral("bad_code"), QStringLiteral("请输入短信验证码")); return; }
    if (name.isEmpty() || org.isEmpty()) { fail(QStringLiteral("missing_fields"), QString()); return; }

    setBusy(true, QStringLiteral("正在验证短信…"));
    CloudAccount::instance().signInWithSms(phone, code, [=](const CloudAccount::Result& r) {
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        const QString accessToken = r.data.value(QStringLiteral("access_token")).toString();
        setBusy(true, QStringLiteral("正在注册账号…"));
        CloudAccount::instance().registerAccount(accessToken, name, org,
                                                 [=](const CloudAccount::Result& rr) {
            if (!rr.ok) {
                fail(rr.error, rr.message);
                return;
            }
            const QString token = rr.data.value(QStringLiteral("token")).toString();
            const qint64 exp = rr.data.value(QStringLiteral("expires_at")).toDouble();
            succeedWithToken(token, exp, QStringLiteral("+86 ") + phone, name, org);
        });
    });
}

void LoginDialog::onInviteActivate() {
    const QString code = m_invCode->text().trimmed().toUpper();
    const QString name = m_invName->text().trimmed();
    const QString org = m_invOrg->text().trimmed();
    if (code.isEmpty() || name.isEmpty() || org.isEmpty()) {
        fail(QStringLiteral("missing_fields"), QString());
        return;
    }
    setBusy(true, QStringLiteral("正在激活…"));
    CloudAccount::instance().activateInvite(code, name, org, [=](const CloudAccount::Result& r) {
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        const QString token = r.data.value(QStringLiteral("token")).toString();
        const qint64 exp = r.data.value(QStringLiteral("expires_at")).toDouble();
        succeedWithToken(token, exp, QStringLiteral("invite:") + code, name, org);
    });
}
