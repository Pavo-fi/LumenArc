#include "theme.h"
#include "logindialog.h"

#include "infrastructure/cloud_account.h"
#include "infrastructure/credential_store.h"

#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
qint64 rr_expires(const QJsonObject& j) {  // 服务端发 expiresAt（ms）；兼容旧字段
    double v = j.value(QStringLiteral("expiresAt")).toDouble();
    if (v <= 0) v = j.value(QStringLiteral("expires_at")).toDouble();
    if (v <= 0) v = j.value(QStringLiteral("exp")).toDouble();
    return static_cast<qint64>(v);
}

QString tr_err(const QString& code, const QString& serverMsg) {
    if (code == QLatin1String("network") || code == QLatin1String("timeout"))
        return QStringLiteral("连不上账号服务器，请检查网络（或换用邀请码注册）");
    if (code == QLatin1String("bad_email")) return QStringLiteral("邮箱格式不对");
    if (code == QLatin1String("too_frequent")) return serverMsg;
    if (code == QLatin1String("ip_quota") || code == QLatin1String("global_quota"))
        return serverMsg;
    if (code == QLatin1String("code_expired")) return QStringLiteral("验证码已过期，请重新获取");
    if (code == QLatin1String("code_invalid")) return QStringLiteral("验证码不对");
    if (code == QLatin1String("too_many_tries"))
        return QStringLiteral("验证码错误次数过多，请重新获取");
    if (code == QLatin1String("weak_password"))
        return QStringLiteral("密码至少 8 位，且不能是纯数字或与账号相同");
    if (code == QLatin1String("exists")) return QStringLiteral("该账号已注册，请直接登录");
    if (code == QLatin1String("daily_quota"))
        return QStringLiteral("今天自助注册名额已满，请联系管理员获取邀请码");
    if (code == QLatin1String("invite_missing")) return QStringLiteral("邀请码不存在");
    if (code == QLatin1String("invite_expired")) return QStringLiteral("邀请码已过期，请联系提供方");
    if (code == QLatin1String("invite_used_up")) return QStringLiteral("该邀请码名额已用完");
    if (code == QLatin1String("not_found")) return QStringLiteral("账号不存在，请先注册");
    if (code == QLatin1String("bad_credentials"))
        return QStringLiteral("账号或密码不对。若是新用户，请先到「邮箱注册」页注册");
    if (code == QLatin1String("locked")) return serverMsg;
    if (code == QLatin1String("bad_password")) return QStringLiteral("密码不对");
    if (code == QLatin1String("disabled")) return QStringLiteral("该账号已停用，请联系管理员");
    if (code == QLatin1String("no_password"))
        return QStringLiteral("该账号还没有密码，请输入邀请码重新激活");
    if (code == QLatin1String("token_invalid")) return QStringLiteral("登录已过期，请重新登录");
    if (code == QLatin1String("missing_fields")) return QStringLiteral("请填写完整信息");
    return serverMsg.isEmpty() ? QStringLiteral("失败：") + code
                               : QStringLiteral("失败：") + serverMsg;
}

QLineEdit* makePwd(QWidget* parent, const QString& placeholder) {
    auto* e = new QLineEdit(parent);
    e->setEchoMode(QLineEdit::Password);
    e->setPlaceholderText(placeholder);
    return e;
}
}  // namespace

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("追光者 Lumen Arc — 账号登录"));
    setModal(true);
    setMinimumWidth(460);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(
        QStringLiteral("本软件需登录后使用。日常用「账号 + 密码」登录；"
                       "新用户用邮箱注册，或使用管理员发放的邀请码激活。"),
        this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_tabs = new QTabWidget(this);

    // ---------- 页签①：账号登录 ----------
    auto* loginPage = new QWidget(this);
    auto* lv = new QVBoxLayout(loginPage);
    auto* lf = new QFormLayout();
    m_account = new QLineEdit(loginPage);
    m_account->setPlaceholderText(QStringLiteral("手机号 或 邮箱"));
    m_password = makePwd(loginPage, QStringLiteral("密码"));
    lf->addRow(QStringLiteral("账 号"), m_account);
    lf->addRow(QStringLiteral("密 码"), m_password);
    lv->addLayout(lf);
    m_loginBtn = new QPushButton(QStringLiteral("登 录"), loginPage);
    m_loginBtn->setDefault(true);
    lv->addWidget(m_loginBtn);
    auto* tip = new QLabel(
        QStringLiteral("忘了密码？请联系管理员重置（不提供自助找回，避免被刷）。"), loginPage);
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextTk::Muted));
    lv->addWidget(tip);
    lv->addStretch(1);
    m_tabs->addTab(loginPage, QStringLiteral("账号登录"));

    // ---------- 页签②：邮箱注册 ----------
    auto* regPage = new QWidget(this);
    auto* rv = new QVBoxLayout(regPage);
    auto* rn = new QLabel(
        QStringLiteral("填邮箱收验证码即可注册。姓名和单位会自动用于案件录入与分析报告署名，请如实填写。"),
        regPage);
    rn->setWordWrap(true);
    rv->addWidget(rn);
    auto* rf = new QFormLayout();
    m_email = new QLineEdit(regPage);
    m_email->setPlaceholderText(QStringLiteral("如 you@163.com"));
    auto* codeRow = new QWidget(regPage);
    auto* cr = new QHBoxLayout(codeRow);
    cr->setContentsMargins(0, 0, 0, 0);
    m_code = new QLineEdit(codeRow);
    m_code->setPlaceholderText(QStringLiteral("6 位验证码"));
    m_sendBtn = new QPushButton(QStringLiteral("发送验证码"), codeRow);
    cr->addWidget(m_code, 1);
    cr->addWidget(m_sendBtn);
    m_regName = new QLineEdit(regPage);
    m_regName->setPlaceholderText(QStringLiteral("您的姓名"));
    m_regOrg = new QLineEdit(regPage);
    m_regOrg->setPlaceholderText(QStringLiteral("单位名称"));
    m_regPwd = makePwd(regPage, QStringLiteral("至少 8 位，不能纯数字"));
    m_regPwd2 = makePwd(regPage, QStringLiteral("再输一次"));
    rf->addRow(QStringLiteral("邮 箱"), m_email);
    rf->addRow(QStringLiteral("验证码"), codeRow);
    rf->addRow(QStringLiteral("姓 名"), m_regName);
    rf->addRow(QStringLiteral("单 位"), m_regOrg);
    rf->addRow(QStringLiteral("密 码"), m_regPwd);
    rf->addRow(QStringLiteral("确认密码"), m_regPwd2);
    rv->addLayout(rf);
    m_regBtn = new QPushButton(QStringLiteral("注册并登录"), regPage);
    rv->addWidget(m_regBtn);
    rv->addStretch(1);
    m_tabs->addTab(regPage, QStringLiteral("邮箱注册"));

    // ---------- 页签③：邀请码激活 ----------
    auto* invPage = new QWidget(this);
    auto* inv = new QVBoxLayout(invPage);
    auto* invNotice = new QLabel(
        QStringLiteral("用于培训班/内部集中开通：管理员发一个批次码，大家用它激活，不需要邮箱。"),
        invPage);
    invNotice->setWordWrap(true);
    inv->addWidget(invNotice);
    auto* inf = new QFormLayout();
    m_invCode = new QLineEdit(invPage);
    m_invCode->setPlaceholderText(QStringLiteral("如 LA-XXXX-XXXX"));
    m_invAccount = new QLineEdit(invPage);
    m_invAccount->setPlaceholderText(QStringLiteral("11 位手机号"));
    m_invName = new QLineEdit(invPage);
    m_invName->setPlaceholderText(QStringLiteral("您的姓名"));
    m_invOrg = new QLineEdit(invPage);
    m_invOrg->setPlaceholderText(QStringLiteral("单位名称"));
    m_invPwd = makePwd(invPage, QStringLiteral("至少 8 位，不能纯数字"));
    inf->addRow(QStringLiteral("邀请码"), m_invCode);
    inf->addRow(QStringLiteral("手机号"), m_invAccount);
    inf->addRow(QStringLiteral("姓 名"), m_invName);
    inf->addRow(QStringLiteral("单 位"), m_invOrg);
    inf->addRow(QStringLiteral("密 码"), m_invPwd);
    inv->addLayout(inf);
    m_invBtn = new QPushButton(QStringLiteral("激活并登录"), invPage);
    inv->addWidget(m_invBtn);
    inv->addStretch(1);
    m_tabs->addTab(invPage, QStringLiteral("邀请码激活"));

    root->addWidget(m_tabs);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Status::Error));
    root->addWidget(m_status);

    m_quitBtn = new QPushButton(QStringLiteral("退出程序"), this);
    root->addWidget(m_quitBtn, 0, Qt::AlignRight);
    connect(m_quitBtn, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::onLogin);
    connect(m_sendBtn, &QPushButton::clicked, this, &LoginDialog::onSendCode);
    connect(m_regBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterEmail);
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
    m_regBtn->setEnabled(!busy);
    m_invBtn->setEnabled(!busy);
    m_sendBtn->setEnabled(!busy && m_countdownLeft <= 0);
    if (m_quitBtn) m_quitBtn->setEnabled(!busy);
    if (busy) {
        m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextTk::Muted));
        m_status->setText(hint.isEmpty() ? QStringLiteral("处理中…") : hint);
    }
}

void LoginDialog::fail(const QString& machineCode, const QString& serverMsg) {
    setBusy(false);
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Status::Error));
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

// ---------------------------------------------------------------- 动作
void LoginDialog::onLogin() {
    const QString account = m_account->text().trimmed().toLower();
    const QString pwd = m_password->text();
    if (account.isEmpty() || pwd.isEmpty()) {
        fail(QStringLiteral("missing_fields"), QStringLiteral("请输入账号和密码"));
        return;
    }
    setBusy(true, QStringLiteral("正在登录…"));
    QPointer<LoginDialog> self(this);
    CloudAccount::instance().login(account, pwd, [this, self, account](const CloudAccount::Result& r) {
        if (!self) return;
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        succeedWithToken(r.data.value(QStringLiteral("token")).toString(), rr_expires(r.data),
                         account,
                         r.data.value(QStringLiteral("name")).toString(),
                         r.data.value(QStringLiteral("org")).toString());
    });
}

void LoginDialog::onSendCode() {
    const QString email = m_email->text().trimmed().toLower();
    if (email.isEmpty() || !email.contains(QLatin1Char('@'))) {
        fail(QStringLiteral("bad_email"), QString());
        return;
    }
    setBusy(true, QStringLiteral("正在发送验证码…"));
    QPointer<LoginDialog> self(this);
    CloudAccount::instance().sendEmailCode(email, [this, self](const CloudAccount::Result& r) {
        if (!self) return;
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        setBusy(false);
        m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Status::Ok));
        m_status->setText(QStringLiteral("验证码已发到邮箱，5 分钟内有效（没收到先看垃圾箱）"));
        m_countdownLeft = 60;
        m_sendBtn->setEnabled(false);
        m_countdown->start(1000);
    });
}

void LoginDialog::onRegisterEmail() {
    const QString email = m_email->text().trimmed().toLower();
    const QString code = m_code->text().trimmed();
    const QString name = m_regName->text().trimmed();
    const QString org = m_regOrg->text().trimmed();
    const QString pwd = m_regPwd->text();
    if (email.isEmpty() || code.length() < 4) {
        fail(QStringLiteral("missing_fields"), QStringLiteral("请填写邮箱并获取验证码"));
        return;
    }
    if (name.isEmpty() || org.isEmpty()) {
        fail(QStringLiteral("missing_fields"),
             QStringLiteral("请填写姓名和单位（将用于案件录入与报告署名）"));
        return;
    }
    if (pwd != m_regPwd2->text()) {
        fail(QStringLiteral("weak_password"), QStringLiteral("两次输入的密码不一致"));
        return;
    }
    if (pwd.length() < 8) {
        fail(QStringLiteral("weak_password"), QString());
        return;
    }
    setBusy(true, QStringLiteral("正在注册…"));
    QPointer<LoginDialog> self(this);
    CloudAccount::instance().registerByEmail(
        email, code, name, org, pwd,
        [this, self, email, name, org](const CloudAccount::Result& r) {
            if (!self) return;
            if (!r.ok) {
                fail(r.error, r.message);
                return;
            }
            succeedWithToken(r.data.value(QStringLiteral("token")).toString(), rr_expires(r.data),
                             email,
                             r.data.value(QStringLiteral("name")).toString().isEmpty()
                                 ? name : r.data.value(QStringLiteral("name")).toString(),
                             r.data.value(QStringLiteral("org")).toString().isEmpty()
                                 ? org : r.data.value(QStringLiteral("org")).toString());
        });
}

void LoginDialog::onInviteActivate() {
    const QString code = m_invCode->text().trimmed().toUpper();
    const QString account = m_invAccount->text().trimmed();
    const QString name = m_invName->text().trimmed();
    const QString org = m_invOrg->text().trimmed();
    const QString pwd = m_invPwd->text();
    if (code.isEmpty() || account.isEmpty() || name.isEmpty() || org.isEmpty()) {
        fail(QStringLiteral("missing_fields"), QString());
        return;
    }
    if (pwd.length() < 8) {
        fail(QStringLiteral("weak_password"), QString());
        return;
    }
    setBusy(true, QStringLiteral("正在激活…"));
    QPointer<LoginDialog> self(this);
    CloudAccount::instance().registerByInvite(
        code, account, name, org, pwd,
        [this, self, code, account, name, org](const CloudAccount::Result& r) {
            if (!self) return;
            if (!r.ok) {
                fail(r.error, r.message);
                return;
            }
            succeedWithToken(r.data.value(QStringLiteral("token")).toString(), rr_expires(r.data),
                             account, name, org);
        });
}
