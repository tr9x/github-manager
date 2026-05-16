#include "ui/HostKeyApprovalDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace ghm::ui {

HostKeyApprovalDialog::HostKeyApprovalDialog(const QString& host,
                                              const QString& fingerprint,
                                              const QString& keyType,
                                              QWidget* parent)
    : QDialog(parent)
    , info_(new QLabel(this))
    , trustBtn_(new QPushButton(tr("Trust and continue"), this))
    , cancelBtn_(new QPushButton(tr("Cancel"), this))
{
    setWindowTitle(tr("Unknown SSH host"));
    setModal(true);
    setMinimumWidth(560);

    info_->setWordWrap(true);
    info_->setTextFormat(Qt::RichText);
    info_->setText(tr(
        "<p>This is the first time you're connecting to "
        "<b>%1</b> over SSH from this account.</p>"
        "<p>The server presented this host key:</p>"
        "<pre style='background:#1f2228; padding:8px; "
        "border-radius:4px;'>"
        "Key type:     %2<br>"
        "Fingerprint:  SHA256:%3</pre>"
        "<p>If you recognise this fingerprint — for example, from "
        "GitHub's published SSH fingerprints page — click "
        "<b>Trust and continue</b>. The fingerprint will be written "
        "to your <code>~/.ssh/known_hosts</code> file so you won't "
        "be asked again.</p>"
        "<p>If you're unsure, cancel and verify out-of-band first. "
        "Trusting the wrong key would let an attacker between you "
        "and the server read your traffic.</p>"
        ).arg(host, keyType, fingerprint));

    trustBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #d4732a; color: white; padding: 6px 14px; }"
        "QPushButton:hover { background: #e08139; }"));

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancelBtn_);
    buttons->addWidget(trustBtn_);
    cancelBtn_->setDefault(true);  // default to safe choice

    auto* root = new QVBoxLayout(this);
    root->addWidget(info_);
    root->addStretch();
    root->addLayout(buttons);

    connect(trustBtn_,  &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);
}

} // namespace ghm::ui
