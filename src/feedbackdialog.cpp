#include "feedbackdialog.h"

#include "infrastructure/cloud_account.h"
#include "infrastructure/crash_handler.h"
#include "infrastructure/credential_store.h"

#include <QCheckBox>
#include <QJsonObject>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

FeedbackDialog::FeedbackDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("意见反馈"));
    setModal(true);
    setMinimumSize(460, 340);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("请描述您遇到的问题或建议（必填，4000 字内）："), this));

    m_text = new QPlainTextEdit(this);
    m_text->setPlaceholderText(QStringLiteral("例如：某段录像导入后时间轴错位；希望支持 XX 格式……"));
    root->addWidget(m_text, 1);

    m_diag = new QCheckBox(QStringLiteral("附带诊断信息（软件版本、系统信息、上次异常退出记录；不含任何案件数据）"), this);
    m_diag->setChecked(true);
    root->addWidget(m_diag);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    m_submit = new QPushButton(QStringLiteral("提交反馈"), this);
    m_submit->setDefault(true);
    root->addWidget(m_submit, 0, Qt::AlignRight);
    connect(m_submit, &QPushButton::clicked, this, &FeedbackDialog::onSubmit);
}

void FeedbackDialog::onSubmit() {
    const QString text = m_text->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("请填写反馈内容"));
        return;
    }
    const Credential cred = CredentialStore::load();
    if (!cred.valid()) {
        m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
        m_status->setText(QStringLiteral("登录状态异常，请重启软件"));
        return;
    }

    QJsonObject diag;
    if (m_diag->isChecked()) {
        diag.insert(QStringLiteral("version"), QStringLiteral(APP_VERSION));
        diag.insert(QStringLiteral("os"), QSysInfo::prettyProductName());
        diag.insert(QStringLiteral("arch"), QSysInfo::currentCpuArchitecture());
        diag.insert(QStringLiteral("prevCrash"), CrashHandler::previousSessionCrashed());
    }

    m_submit->setEnabled(false);
    m_status->setStyleSheet(QStringLiteral("color:#666;"));
    m_status->setText(QStringLiteral("正在提交…"));

    CloudAccount::instance().submitFeedback(cred.token, text, diag, [this](const CloudAccount::Result& r) {
        m_submit->setEnabled(true);
        if (!r.ok) {
            m_status->setStyleSheet(QStringLiteral("color:#c0392b;"));
            m_status->setText(r.error == QLatin1String("network") || r.error == QLatin1String("timeout")
                                  ? QStringLiteral("网络连接失败，请检查网络后重试")
                                  : QStringLiteral("提交失败：") + r.message);
            return;
        }
        m_status->setStyleSheet(QStringLiteral("color:#27ae60;"));
        m_status->setText(QStringLiteral("已收到您的反馈，感谢支持！"));
        m_text->clear();
        m_text->setEnabled(false);
        m_diag->setEnabled(false);
        m_submit->setEnabled(false);
    });
}
