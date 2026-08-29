#include "logindialog.h"

#include "infrastructure/cloud_account.h"
#include "infrastructure/credential_store.h"

#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
qint64 rr_expires(const QJsonObject& j) {  // 兼容 expires_at / exp
    double v = j.value(QStringLiteral("expires_at")).toDouble();
    if (v <= 0) v = j.value(QStringLiteral("exp")).toDouble();
    return static_cast<qint64>(v);
}
QString tr_err(const QString& code, const QString& serverMsg) {
    if (code == QLatin1String("network") || code == QLatin1String("timeout"))
        return QStringLiteral("网络连接失败，请检查网络后重试");
    if (code == QLatin1String("INVALID_ARGUMENT") || code == QLatin1String("invalid_argument")
        || code == QLatin1String("sms_code_invalid"))
        return QStringLiteral("验证码错误或已过期，请重新获取");
    if (code == QLatin1String("LIMIT_EXCEEDED") || code == QLatin1String("OPERATION_TOO_FREQUENT")
        || code == QLatin1String("RESOURCE_EXHAUSTED"))
        return QStringLiteral("发送太频繁，请稍后再试");
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
    auto* intro = new QLabel(QStringLiteral("本软件需登录后使用。已注册用户直接收验证码登录；"
                                            "首次使用会自动进入注册补全；无网络环境请使用邀请码。"), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_tabs = new QTabWidget(this);

    // ---- 手机号页 ----
    auto* phonePage = new QWidget(this);
    auto* pv = new QVBoxLayout(phonePage);
    auto* pf = new QFormLayout();
    pv->addLayout(pf);

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
    pf->addRow(QStringLiteral("手机号"), m_phone);
    pf->addRow(QStringLiteral("验证码"), codeRow);

    m_loginBtn = new QPushButton(QStringLiteral("登 录"), phonePage);
    m_loginBtn->setDefault(true);
    pv->addWidget(m_loginBtn);

    // 第二层：注册补全（默认隐藏，need_signup 时出现）
    m_regLayer = new QWidget(phonePage);
    auto* rv = new QVBoxLayout(m_regLayer);
    rv->setContentsMargins(0, 8, 0, 0);
    auto* notice = new QLabel(QStringLiteral("该手机号尚未注册。请补全以下信息完成注册——\n"
                                             "您填写的姓名和单位会自动用于案件录入和分析报告生成相关功能，请妥善填写。"),
                              m_regLayer);
    notice->setWordWrap(true);
    notice->setStyleSheet(QStringLiteral("color:#b26a00;"));
    rv->addWidget(notice);
    auto* rf = new QFormLayout();
    m_name = new QLineEdit(m_regLayer);
    m_name->setPlaceholderText(QStringLiteral("您的姓名"));
    m_org = new QLineEdit(m_regLayer);
    m_org->setPlaceholderText(QStringLiteral("单位名称"));
    rf->addRow(QStringLiteral("姓 名"), m_name);
    rf->addRow(QStringLiteral("单 位"), m_org);
    rv->addLayout(rf);
    m_registerBtn = new QPushButton(QStringLiteral("完成注册并登录"), m_regLayer);
    rv->addWidget(m_registerBtn);
    m_regLayer->setVisible(false);
    pv->addWidget(m_regLayer);

    m_tabs->addTab(phonePage, QStringLiteral("手机号登录"));

    // ---- 邀请码页 ----
    auto* invPage = new QWidget(this);
    auto* inv = new QVBoxLayout(invPage);
    auto* invNotice = new QLabel(QStringLiteral("适用于无网络的离线机器。邀请码由软件提供方发放，"
                                                "每个邀请码仅能激活一次。\n"
                                                "您填写的姓名和单位会自动用于案件录入和分析报告生成相关功能，请妥善填写。"),
                                 invPage);
    invNotice->setWordWrap(true);
    inv->addWidget(invNotice);
    auto* inf = new QFormLayout();
    m_invCode = new QLineEdit(invPage);
    m_invCode->setPlaceholderText(QStringLiteral("如 LA-XXXX-XXXX"));
    m_invName = new QLineEdit(invPage);
    m_invName->setPlaceholderText(QStringLiteral("您的姓名"));
    m_invOrg = new QLineEdit(invPage);
    m_invOrg->setPlaceholderText(QStringLiteral("单位名称"));
    inf->addRow(QStringLiteral("邀请码"), m_invCode);
    inf->addRow(QStringLiteral("姓 名"), m_invName);
    inf->addRow(QStringLiteral("单 位"), m_invOrg);
    inv->addLayout(inf);
    m_invBtn = new QPushButton(QStringLiteral("激活并登录"), invPage);
    inv->addWidget(m_invBtn);
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
    connect(m_registerBtn, &QPushButton::clicked, this, &LoginDialog::onCompleteRegistration);
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

void LoginDialog::showRegistrationLayer(bool show) {
    m_regLayer->setVisible(show);
    m_loginBtn->setVisible(!show);
    if (show) {
        m_code->setEnabled(false);
        m_sendBtn->setEnabled(false);
        m_phone->setEnabled(false);
        m_registerBtn->setDefault(true);
        m_name->setFocus();
    }
    adjustSize();
}

void LoginDialog::setBusy(bool busy, const QString& hint) {
    m_tabs->setEnabled(!busy);
    m_loginBtn->setEnabled(!busy);
    m_registerBtn->setEnabled(!busy);
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
    if (phone.length() != 11) { fail(QStringLiteral("bad_phone"), QStringLiteral("请输入正确的 11 位手机号")); return; }
    if (code.length() < 4) { fail(QStringLiteral("bad_code"), QStringLiteral("请输入短信验证码")); return; }

    setBusy(true, QStringLiteral("正在验证短信…"));
    CloudAccount::instance().signInWithSms(phone, code, [=](const CloudAccount::Result& r) {
        if (!r.ok && r.error == QLatin1String("need_signup")) {
            // 第二层：新用户补全姓名/单位
            setBusy(false);
            showRegistrationLayer(true);
            m_status->setStyleSheet(QStringLiteral("color:#666;"));
            m_status->setText(QStringLiteral("验证通过，请补全注册信息"));
            return;
        }
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        // 老用户：注册已在链路内完成；档案姓名/单位以服务端响应为准（署名写死用）
        const QString token = r.data.value(QStringLiteral("token")).toString();
        const qint64 exp = rr_expires(r.data);
        succeedWithToken(token, exp, QStringLiteral("+86 ") + phone,
                         r.data.value(QStringLiteral("name")).toString(),
                         r.data.value(QStringLiteral("org")).toString());
    });
}

void LoginDialog::onCompleteRegistration() {
    const QString name = m_name->text().trimmed();
    const QString org = m_org->text().trimmed();
    if (name.isEmpty() || org.isEmpty()) {
        fail(QStringLiteral("missing_fields"), QStringLiteral("请填写姓名和单位（将用于案件录入与报告生成）"));
        return;
    }
    setBusy(true, QStringLiteral("正在注册…"));
    CloudAccount::instance().signUpAndRegister(name, org, [=](const CloudAccount::Result& r) {
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        const QString token = r.data.value(QStringLiteral("token")).toString();
        const qint64 exp = rr_expires(r.data);
        // 新注册：服务端响应为准（与提交一致）
        succeedWithToken(token, exp, QStringLiteral("+86 ") + m_phone->text().trimmed(),
                         r.data.value(QStringLiteral("name")).toString().isEmpty()
                             ? name : r.data.value(QStringLiteral("name")).toString(),
                         r.data.value(QStringLiteral("org")).toString().isEmpty()
                             ? org : r.data.value(QStringLiteral("org")).toString());
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
        const qint64 exp = rr_expires(r.data);
        succeedWithToken(token, exp, QStringLiteral("invite:") + code, name, org);
    });
}
