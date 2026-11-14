#include "accountdialog.h"

#include "infrastructure/cloud_account.h"
#include "infrastructure/credential_store.h"

#include "theme.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString maskPhone(const QString& uid) {
    // "+86 13828507019" -> "+86 138****7019"
    if (!uid.startsWith(QStringLiteral("+86 ")) || uid.length() < 8) return uid;
    const QString num = uid.mid(4);
    return QStringLiteral("+86 ") + num.left(3) + QStringLiteral("****") + num.right(4);
}
QString tr_err(const QString& code, const QString& serverMsg) {
    if (code == QLatin1String("network") || code == QLatin1String("timeout"))
        return QStringLiteral("网络连接失败，请检查网络后重试");
    if (code == QLatin1String("INVALID_ARGUMENT") || code == QLatin1String("invalid_argument"))
        return QStringLiteral("验证码错误或已过期，请重新获取");
    if (code == QLatin1String("RESOURCE_EXHAUSTED") || code == QLatin1String("LIMIT_EXCEEDED"))
        return QStringLiteral("发送太频繁，请稍后再试");
    if (code == QLatin1String("phone_mismatch")) return QStringLiteral("验证码与当前账号不一致");
    if (code == QLatin1String("cloudbase_token_invalid")) return QStringLiteral("验证已过期，请重新获取验证码");
    if (code == QLatin1String("user_not_found")) return QStringLiteral("该手机号未注册");
    if (code == QLatin1String("invite_cannot_edit")) return QStringLiteral("邀请码账号资料由提供方管理");
    return serverMsg.isEmpty() ? QStringLiteral("失败：") + code
                               : QStringLiteral("失败：") + serverMsg;
}
}  // namespace

AccountDialog::AccountDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("账号管理"));
    setModal(true);
    setMinimumWidth(430);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    const Credential cred = CredentialStore::load();
    m_uid = cred.uid;
    m_isInvite = m_uid.startsWith(QStringLiteral("invite:"));

    auto* root = new QVBoxLayout(this);
    const QString who = cred.name.isEmpty() ? QStringLiteral("（未登记姓名）")
                                            : cred.name + QStringLiteral("（") + cred.org + QStringLiteral("）");
    m_infoLabel = new QLabel(
        QStringLiteral("当前账号：%1\n登录方式：%2")
            .arg(who, m_uid.contains(QLatin1Char('@'))
                          ? QStringLiteral("邮箱 %1").arg(m_uid)
                          : QStringLiteral("手机号 %1").arg(maskPhone(m_uid))),
        this);
    m_infoLabel->setWordWrap(true);
    root->addWidget(m_infoLabel);

    // ---- 修改姓名/单位（密码验证身份；不再走短信）----
    m_editBox = new QGroupBox(QStringLiteral("修改姓名 / 单位"), this);
    auto* ef = new QFormLayout(m_editBox);
    m_code = new QLineEdit(m_editBox);
    m_code->setEchoMode(QLineEdit::Password);
    m_code->setPlaceholderText(QStringLiteral("当前密码（验证身份用）"));
    m_name = new QLineEdit(cred.name, m_editBox);
    m_org = new QLineEdit(cred.org, m_editBox);
    ef->addRow(QStringLiteral("验 证"), m_code);
    ef->addRow(QStringLiteral("姓 名"), m_name);
    ef->addRow(QStringLiteral("单 位"), m_org);
    m_submitBtn = new QPushButton(QStringLiteral("提交修改"), m_editBox);
    ef->addRow(QString(), m_submitBtn);
    root->addWidget(m_editBox);

    if (m_isInvite) {
        m_editBox->setEnabled(false);
        m_editBox->setToolTip(QStringLiteral("邀请码账号资料由提供方管理，如需修改请联系提供方"));
    }

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto* btnRow = new QHBoxLayout();
    m_signoutBtn = new QPushButton(QStringLiteral("退出登录"), this);
    m_signoutBtn->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Status::Error));
    auto* closeBtn = new QPushButton(QStringLiteral("关 闭"), this);
    closeBtn->setDefault(true);
    btnRow->addWidget(m_signoutBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_submitBtn, &QPushButton::clicked, this, &AccountDialog::onSubmitProfile);
    connect(m_signoutBtn, &QPushButton::clicked, this, &AccountDialog::onSignOut);
}

void AccountDialog::setBusy(bool busy, const QString& hint) {
    m_editBox->setEnabled(!busy && !m_isInvite);
    m_signoutBtn->setEnabled(!busy);
    if (busy) {
        m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextTk::Muted));
        m_status->setText(hint.isEmpty() ? QStringLiteral("处理中…") : hint);
    }
}

void AccountDialog::fail(const QString& machineCode, const QString& serverMsg) {
    setBusy(false);
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Status::Error));
    m_status->setText(tr_err(machineCode, serverMsg));
}

void AccountDialog::onSendCode() {
    // v2：不再有短信通道；改资料用当前密码验证身份（见 onSubmitProfile）
}

void AccountDialog::onSubmitProfile() {
    const QString pwd = m_code->text();
    const QString name = m_name->text().trimmed();
    const QString org = m_org->text().trimmed();
    if (pwd.isEmpty()) { fail(QStringLiteral("bad_password"), QStringLiteral("请输入当前密码")); return; }
    if (name.isEmpty() || org.isEmpty()) { fail(QStringLiteral("missing_fields"), QString()); return; }

    setBusy(true, QStringLiteral("正在验证并提交…"));
    // v1.18.0 P1 修复：QPointer 守卫对话框生命周期
    QPointer<AccountDialog> self(this);
    const Credential cred = CredentialStore::load();
    CloudAccount::instance().updateProfile(cred.token, pwd, name, org,
                                           [this, self, name, org](const CloudAccount::Result& r) {
        if (!self) return;
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        Credential c = CredentialStore::load();
        c.name = name;
        c.org = org;
        CredentialStore::save(c);
        setBusy(false);
        m_status->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Status::Ok));
        m_status->setText(QStringLiteral("资料已更新。新建案件与报告将使用新的姓名和单位。"));
        m_infoLabel->setText(QStringLiteral("当前账号：%1（%2）").arg(name, org));
    });
}

void AccountDialog::onSignOut() {
    const Credential cred = CredentialStore::load();
    const QString who = cred.name.isEmpty() ? cred.uid : cred.name;
    if (QMessageBox::question(this, QStringLiteral("退出登录"),
                              QStringLiteral("当前登录账号：%1\n\n退出后将返回登录界面（取消登录将退出程序）。")
                                  .arg(who))
        != QMessageBox::Yes)
        return;
    CredentialStore::clear();
    m_signedOut = true;
    accept();
}
