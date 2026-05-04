#include "ui/CloneDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
    , okBtn_(new QPushButton(tr("Clone"), this))
    , cancelBtn_(new QPushButton(tr("Cancel"), this))
{
    setWindowTitle(tr("Clone %1").arg(repo.fullName));
    setModal(true);
    setMinimumWidth(520);

    parentDirEdit_->setText(defaultParentDir);
    folderNameEdit_->setText(repo.name);

    auto* browseBtn = new QPushButton(tr("Browse…"), this);
    auto* parentRow = new QHBoxLayout;
    parentRow->addWidget(parentDirEdit_, 1);
    parentRow->addWidget(browseBtn);

    auto* form = new QFormLayout;
    form->addRow(tr("Repository:"), new QLabel(repo.fullName, this));
    form->addRow(tr("Parent folder:"), parentRow);
    form->addRow(tr("Folder name:"),   folderNameEdit_);
    form->addRow(tr("Will clone to:"), finalPathLabel_);

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
