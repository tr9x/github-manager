#include "ui/CloneDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QFileDialog>
#include <QDir>

namespace ghm::ui {

CloneDialog::CloneDialog(const ghm::github::Repository& repo,
                         const QString& defaultParentDir,
                         QWidget* parent)
    : QDialog(parent)
    , repo_(repo)
    , parentDirEdit_(new QLineEdit(this))
    , folderNameEdit_(new QLineEdit(this))
    , finalPathLabel_(new QLabel(this))
    , sshCheckbox_(new QCheckBox(tr("Use SSH (git@host:owner/repo) instead of HTTPS"), this))
    , sshExplicitKeyCheckbox_(new QCheckBox(tr("Use explicit key file (otherwise: ssh-agent)"), this))
    , okBtn_(new QPushButton(tr("Clone"), this))
    , cancelBtn_(new QPushButton(tr("Cancel"), this))
{
    setWindowTitle(tr("Clone %1").arg(repo.fullName));
    setModal(true);
    setMinimumWidth(520);

    parentDirEdit_->setText(defaultParentDir);
    folderNameEdit_->setText(repo.name);

    // SSH checkbox starts unchecked; the caller can override via
    // setSshDefault() based on the user's Settings preference. We
    // keep the checkbox even when SSH is the default so per-clone
    // overrides are always possible (some users need HTTPS for one
    // specific repo that's not on their SSH-allowlist).
    sshCheckbox_->setToolTip(tr(
        "Rewrites the HTTPS URL into git@host:owner/repo and "
        "authenticates via ssh-agent. Requires a working SSH key "
        "loaded into your agent."));

    sshExplicitKeyCheckbox_->setToolTip(tr(
        "If your key is encrypted or you want to pick a specific key "
        "file (other than the ones loaded into ssh-agent), enable this. "
        "A prompt for the key path and passphrase appears after you "
        "click Clone."));
    sshExplicitKeyCheckbox_->setEnabled(sshCheckbox_->isChecked());
    // The explicit-key checkbox only makes sense when SSH is selected.
    // Keep them in sync: HTTPS implies agent flow (no key needed).
    connect(sshCheckbox_, &QCheckBox::toggled,
            sshExplicitKeyCheckbox_, &QCheckBox::setEnabled);
    connect(sshCheckbox_, &QCheckBox::toggled, this,
            [this](bool checked) {
        if (!checked) sshExplicitKeyCheckbox_->setChecked(false);
    });

    auto* browseBtn = new QPushButton(tr("Browse…"), this);
    auto* parentRow = new QHBoxLayout;
    parentRow->addWidget(parentDirEdit_, 1);
    parentRow->addWidget(browseBtn);

    auto* form = new QFormLayout;
    form->addRow(tr("Repository:"), new QLabel(repo.fullName, this));
    form->addRow(tr("Parent folder:"), parentRow);
    form->addRow(tr("Folder name:"),   folderNameEdit_);
    form->addRow(tr("Will clone to:"), finalPathLabel_);
    form->addRow(QString(),            sshCheckbox_);
    form->addRow(QString(),            sshExplicitKeyCheckbox_);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancelBtn_);
    buttons->addWidget(okBtn_);
    okBtn_->setDefault(true);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addStretch();
    root->addLayout(buttons);

    connect(browseBtn,       &QPushButton::clicked, this, &CloneDialog::onBrowse);
    connect(parentDirEdit_,  &QLineEdit::textChanged, this, &CloneDialog::onPathChanged);
    connect(folderNameEdit_, &QLineEdit::textChanged, this, &CloneDialog::onPathChanged);
    connect(okBtn_,          &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn_,      &QPushButton::clicked, this, &QDialog::reject);

    onPathChanged({});
}

CloneDialog::~CloneDialog() = default;

QString CloneDialog::targetPath() const
{
    return QDir::cleanPath(parentDirEdit_->text() +
                           QDir::separator() +
                           folderNameEdit_->text());
}

void CloneDialog::setSshDefault(bool useSsh)
{
    sshCheckbox_->setChecked(useSsh);
}

bool CloneDialog::useSsh() const
{
    return sshCheckbox_->isChecked();
}

bool CloneDialog::useExplicitKey() const
{
    return sshExplicitKeyCheckbox_->isChecked();
}

void CloneDialog::onBrowse()
{
    const QString picked = QFileDialog::getExistingDirectory(
        this, tr("Choose parent folder"),
        parentDirEdit_->text(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!picked.isEmpty()) parentDirEdit_->setText(picked);
}

void CloneDialog::onPathChanged(const QString&)
{
    const QString p = targetPath();
    finalPathLabel_->setText(p);
    okBtn_->setEnabled(!parentDirEdit_->text().isEmpty() &&
                       !folderNameEdit_->text().isEmpty());
}

} // namespace ghm::ui
