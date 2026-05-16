#include "ui/SshKeyDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QDir>

#include "git/SshKeyInfo.h"

namespace ghm::ui {

SshKeyDialog::SshKeyDialog(QWidget* parent)
    : QDialog(parent)
    , pathEdit_      (new QLineEdit(this))
    , passphraseEdit_(new QLineEdit(this))
    , statusLabel_   (new QLabel(this))
    , okBtn_         (new QPushButton(tr("Use this key"), this))
    , cancelBtn_     (new QPushButton(tr("Cancel"), this))
{
    setWindowTitle(tr("SSH key"));
    setModal(true);
    setMinimumWidth(540);

    // Default to ~/.ssh/id_ed25519, the modern recommended key type.
    // If the user has only an old id_rsa, they can browse to it.
    const QString home = QDir::homePath();
    pathEdit_->setText(home + QStringLiteral("/.ssh/id_ed25519"));

    passphraseEdit_->setEchoMode(QLineEdit::Password);
    passphraseEdit_->setPlaceholderText(tr("(leave empty for unencrypted keys)"));

    auto* browseBtn = new QPushButton(tr("Browse…"), this);
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit_, 1);
    pathRow->addWidget(browseBtn);

    auto* form = new QFormLayout;
    form->addRow(tr("Private key path:"), pathRow);
    form->addRow(tr("Passphrase:"),       passphraseEdit_);
    form->addRow(QString(),               statusLabel_);

    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(QStringLiteral("color: #8aa3c2;"));

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancelBtn_);
    buttons->addWidget(okBtn_);
    okBtn_->setDefault(true);

    auto* root = new QVBoxLayout(this);
    // Optional message label, hidden until setMessage() is called.
    // Sits above the form so it reads as a header / preamble.
    messageLabel_ = new QLabel(this);
    messageLabel_->setWordWrap(true);
    messageLabel_->setVisible(false);
    root->addWidget(messageLabel_);
    root->addLayout(form);
    root->addStretch();
    root->addLayout(buttons);

    connect(browseBtn,  &QPushButton::clicked, this, &SshKeyDialog::onBrowse);
    connect(pathEdit_,  &QLineEdit::textChanged, this, &SshKeyDialog::onPathChanged);
    connect(okBtn_,     &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);

    refreshKeyInfo();
}

void SshKeyDialog::setKeyPath(const QString& path)
{
    if (!path.isEmpty()) pathEdit_->setText(path);
}

void SshKeyDialog::setMessage(const QString& message)
{
    if (!messageLabel_) return;
    if (message.isEmpty()) {
        messageLabel_->clear();
        messageLabel_->setVisible(false);
    } else {
        messageLabel_->setText(message);
        messageLabel_->setVisible(true);
    }
}

QString SshKeyDialog::keyPath() const
{
    return pathEdit_->text();
}

QString SshKeyDialog::passphrase() const
{
    return passphraseEdit_->text();
}

bool SshKeyDialog::keyIsEncrypted() const
{
    return encrypted_;
}

void SshKeyDialog::onBrowse()
{
    const QString picked = QFileDialog::getOpenFileName(this,
        tr("Choose SSH private key"),
        QDir::homePath() + QStringLiteral("/.ssh"));
    if (!picked.isEmpty()) pathEdit_->setText(picked);
}

void SshKeyDialog::onPathChanged(const QString&)
{
    refreshKeyInfo();
}

void SshKeyDialog::refreshKeyInfo()
{
    const auto info = ghm::git::inspectSshKey(pathEdit_->text());
    encrypted_ = info.encrypted;

    if (!info.exists) {
        statusLabel_->setText(tr("⚠ File not found."));
        passphraseEdit_->setEnabled(false);
        okBtn_->setEnabled(false);
        return;
    }
    if (!info.isReadable) {
        statusLabel_->setText(tr("⚠ Cannot read file (permissions?)."));
        passphraseEdit_->setEnabled(false);
        okBtn_->setEnabled(false);
        return;
    }

    okBtn_->setEnabled(true);
    if (info.encrypted) {
        statusLabel_->setText(tr(
            "🔒 Encrypted key — passphrase required."));
        passphraseEdit_->setEnabled(true);
        passphraseEdit_->setFocus();
    } else {
        statusLabel_->setText(tr(
            "🔓 Unencrypted key — no passphrase needed."));
        passphraseEdit_->setEnabled(false);
        passphraseEdit_->clear();
    }
}

} // namespace ghm::ui
