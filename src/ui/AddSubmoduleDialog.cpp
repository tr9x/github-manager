#include "ui/AddSubmoduleDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>

namespace ghm::ui {

namespace {
// Detect SSH-looking URLs so we know when to offer the explicit-key
// option. Same heuristic as SshUrlConverter — git@ prefix or ssh://
// scheme. We don't try to be exhaustive (some users have port-specific
// SSH URLs via .ssh/config aliases) — being too narrow just means the
// checkbox stays available for HTTPS, which is harmless.
bool looksLikeSsh(const QString& url)
{
    return url.startsWith(QLatin1String("git@")) ||
           url.startsWith(QLatin1String("ssh://"));
}
} // namespace

AddSubmoduleDialog::AddSubmoduleDialog(QWidget* parent)
    : QDialog(parent)
    , urlEdit_  (new QLineEdit(this))
    , pathEdit_ (new QLineEdit(this))
    , hintLabel_(new QLabel(this))
    , explicitKeyCheckbox_(new QCheckBox(
        tr("Use explicit key file (otherwise: ssh-agent)"), this))
    , okBtn_    (new QPushButton(tr("Add submodule"), this))
    , cancelBtn_(new QPushButton(tr("Cancel"), this))
{
    setWindowTitle(tr("Add submodule"));
    setModal(true);
    setMinimumWidth(540);

    urlEdit_->setPlaceholderText(QStringLiteral(
        "https://github.com/owner/repo.git  or  git@github.com:owner/repo.git"));
    pathEdit_->setPlaceholderText(QStringLiteral("vendor/foo"));

    hintLabel_->setText(tr(
        "The submodule will be cloned into the path you specify, "
        "relative to the parent repository's root. After this completes, "
        "you'll need to commit the new <code>.gitmodules</code> entry "
        "and gitlink in the parent repo."));
    hintLabel_->setTextFormat(Qt::RichText);
    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet(QStringLiteral("color: #8aa3c2;"));

    explicitKeyCheckbox_->setToolTip(tr(
        "Enable when your SSH key is encrypted or you want to use a "
        "specific key file. A prompt for the key path and passphrase "
        "appears after you click Add submodule. Only meaningful for "
        "SSH URLs."));
    explicitKeyCheckbox_->setEnabled(false);  // off until URL is SSH

    auto* form = new QFormLayout;
    form->addRow(tr("Repository URL:"), urlEdit_);
    form->addRow(tr("Path in repo:"),   pathEdit_);
    form->addRow(QString(),             explicitKeyCheckbox_);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancelBtn_);
    buttons->addWidget(okBtn_);
    okBtn_->setDefault(true);
    okBtn_->setEnabled(false);  // until both fields non-empty

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(hintLabel_);
    root->addStretch();
    root->addLayout(buttons);

    connect(urlEdit_,   &QLineEdit::textChanged, this, &AddSubmoduleDialog::onUrlChanged);
    connect(pathEdit_,  &QLineEdit::textChanged, this, &AddSubmoduleDialog::onSubPathChanged);
    connect(okBtn_,     &QPushButton::clicked,   this, &QDialog::accept);
    connect(cancelBtn_, &QPushButton::clicked,   this, &QDialog::reject);
}

QString AddSubmoduleDialog::url() const
{
    return urlEdit_->text().trimmed();
}

QString AddSubmoduleDialog::subPath() const
{
    return pathEdit_->text().trimmed();
}

bool AddSubmoduleDialog::useExplicitKey() const
{
    return explicitKeyCheckbox_->isChecked() &&
           explicitKeyCheckbox_->isEnabled();
}

void AddSubmoduleDialog::onUrlChanged(const QString& url)
{
    explicitKeyCheckbox_->setEnabled(looksLikeSsh(url.trimmed()));
    if (!explicitKeyCheckbox_->isEnabled()) {
        // Auto-uncheck when URL stops looking like SSH, so a stale
        // checked state doesn't carry over.
        explicitKeyCheckbox_->setChecked(false);
    }
    updateOkEnabled();
}

void AddSubmoduleDialog::onSubPathChanged(const QString&)
{
    updateOkEnabled();
}

void AddSubmoduleDialog::updateOkEnabled()
{
    okBtn_->setEnabled(!url().isEmpty() && !subPath().isEmpty());
}

} // namespace ghm::ui
