#include "accountdialog.h"

#include "infrastructure/cloud_account.h"
#include "infrastructure/credential_store.h"
#include "infrastructure/signature_store.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
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
            .arg(who, m_isInvite ? QStringLiteral("邀请码 %1").arg(m_uid.mid(7))
                                 : QStringLiteral("手机号 %1").arg(maskPhone(m_uid))),
        this);
    m_infoLabel->setWordWrap(true);
    root->addWidget(m_infoLabel);

    // ---- 修改姓名/单位（手机号用户）----
    m_editBox = new QGroupBox(QStringLiteral("修改姓名 / 单位"), this);
    auto* ef = new QFormLayout(m_editBox);
    auto* codeRow = new QWidget(m_editBox);
    auto* cr = new QHBoxLayout(codeRow);
    cr->setContentsMargins(0, 0, 0, 0);
    m_code = new QLineEdit(codeRow);
    m_code->setPlaceholderText(QStringLiteral("短信验证码（发到当前手机号）"));
    m_sendBtn = new QPushButton(QStringLiteral("发送验证码"), codeRow);
    cr->addWidget(m_code, 1);
    cr->addWidget(m_sendBtn);
    m_name = new QLineEdit(cred.name, m_editBox);
    m_org = new QLineEdit(cred.org, m_editBox);
    ef->addRow(QStringLiteral("验 证"), codeRow);
    ef->addRow(QStringLiteral("姓 名"), m_name);
    ef->addRow(QStringLiteral("单 位"), m_org);
    m_submitBtn = new QPushButton(QStringLiteral("提交修改"), m_editBox);
    ef->addRow(QString(), m_submitBtn);
    root->addWidget(m_editBox);

    if (m_isInvite) {
        m_editBox->setEnabled(false);
        m_editBox->setToolTip(QStringLiteral("邀请码账号资料由提供方管理，如需修改请联系提供方"));
    }

    // ---- 默认署名（本地值，免验证；案件/报告/点位图等落款接入点统一取它）----
    auto* sigBox = new QGroupBox(QStringLiteral("默认署名"), this);
    auto* sf = new QFormLayout(sigBox);
    m_sigName = new QLineEdit(SignatureStore::name(), sigBox);
    m_sigOrg = new QLineEdit(SignatureStore::org(), sigBox);
    sf->addRow(QStringLiteral("姓 名"), m_sigName);
    sf->addRow(QStringLiteral("单 位"), m_sigOrg);
    m_sigSaveBtn = new QPushButton(QStringLiteral("保存署名"), sigBox);
    sf->addRow(QString(), m_sigSaveBtn);
    auto* sigHint = new QLabel(QStringLiteral("署名会自动用于案件录入、分析报告生成、点位图等处的落款；"
                                              "此处修改立即生效，不需要短信验证。"), sigBox);
    sigHint->setWordWrap(true);
    sigHint->setStyleSheet(QStringLiteral("color:#888;"));
    sf->addRow(sigHint);
    root->addWidget(sigBox);
    connect(m_sigSaveBtn, &QPushButton::clicked, this, &AccountDialog::onSaveSignature);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto* btnRow = new QHBoxLayout();
    m_signoutBtn = new QPushButton(QStringLiteral("退出登录"), this);
    m_signoutBtn->setStyleSheet(QStringLiteral("color:#c0392b;"));
    auto* closeBtn = new QPushButton(QStringLiteral("关 闭"), this);
    closeBtn->setDefault(true);
    btnRow->addWidget(m_signoutBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_sendBtn, &QPushButton::clicked, this, &AccountDialog::onSendCode);
    connect(m_submitBtn, &QPushButton::clicked, this, &AccountDialog::onSubmitProfile);
    connect(m_signoutBtn, &QPushButton::clicked, this, &AccountDialog::onSignOut);

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

void AccountDialog::setBusy(bool busy, const QString& hint) {
    m_editBox->setEnabled(!busy && !m_isInvite);
    m_signoutBtn->setEnabled(!busy);
    if (busy) {
        m_status->setStyleSheet(QStringLiteral("color:#666;"));
        m_status->setText(hint.isEmpty() ? QStringLiteral("处理中…") : hint);
    }
}

void AccountDialog::fail(const QString& machineCode, const QString& serverMsg) {
    setBusy(false);
    m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
    m_status->setText(tr_err(machineCode, serverMsg));
}

void AccountDialog::onSendCode() {
    const QString phone = m_uid.mid(4);  // 去 "+86 "
    setBusy(true, QStringLiteral("正在发送验证码…"));
    CloudAccount::instance().sendSmsCode(phone, [this](const CloudAccount::Result& r) {
        setBusy(false);
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        m_status->setStyleSheet(QStringLiteral("color:#27ae60;"));
        m_status->setText(QStringLiteral("验证码已发送到当前手机号（5 分钟内有效）"));
        m_countdownLeft = 60;
        m_sendBtn->setEnabled(false);
        m_countdown->start(1000);
    });
}

void AccountDialog::onSubmitProfile() {
    const QString code = m_code->text().trimmed();
    const QString name = m_name->text().trimmed();
    const QString org = m_org->text().trimmed();
    if (code.length() < 4) { fail(QStringLiteral("bad_code"), QStringLiteral("请输入短信验证码")); return; }
    if (name.isEmpty() || org.isEmpty()) { fail(QStringLiteral("missing_fields"), QString()); return; }

    const QString phone = m_uid.mid(4);
    setBusy(true, QStringLiteral("正在验证身份…"));
    CloudAccount::instance().reauthPhone(phone, code, [=](const CloudAccount::Result& r) {
        if (!r.ok) {
            fail(r.error, r.message);
            return;
        }
        const QString accessToken = r.data.value(QStringLiteral("access_token")).toString();
        const Credential cred = CredentialStore::load();
        setBusy(true, QStringLiteral("正在提交修改…"));
        CloudAccount::instance().updateProfile(cred.token, accessToken, name, org,
                                               [=](const CloudAccount::Result& r2) {
            if (!r2.ok) {
                fail(r2.error, r2.message);
                return;
            }
            Credential c = CredentialStore::load();
            c.name = name;
            c.org = org;
            CredentialStore::save(c);
            SignatureStore::initIfEmpty(name, org);  // 署名为空才跟随账号档案
            setBusy(false);
            m_status->setStyleSheet(QStringLiteral("color:#27ae60;"));
            m_status->setText(QStringLiteral("资料已更新。新建案件与报告将使用新的姓名和单位。"));
            m_infoLabel->setText(QStringLiteral("当前账号：%1（%2）\n登录方式：手机号 %3")
                                     .arg(name, org, maskPhone(m_uid)));
        });
    });
}

void AccountDialog::onSaveSignature() {
    SignatureStore::save(m_sigName->text(), m_sigOrg->text());
    m_status->setStyleSheet(QStringLiteral("color:#27ae60;"));
    m_status->setText(QStringLiteral("署名已保存，之后的案件、报告、点位图落款将使用新署名。"));
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
