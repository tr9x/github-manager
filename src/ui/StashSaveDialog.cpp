#include "ui/StashSaveDialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QLabel>

namespace ghm::ui {

StashSaveDialog::StashSaveDialog(QWidget* parent)
    : QDialog(parent)
    , messageEdit_  (new QLineEdit(this))
    , untrackedBox_ (new QCheckBox(tr("Include untracked files"), this))
    , keepIndexBox_ (new QCheckBox(tr("Keep staged changes in working tree"), this))
{
    setWindowTitle(tr("Stash changes"));
    setModal(true);
    resize(440, 0);

    messageEdit_->setPlaceholderText(
        tr("Optional: short description (auto-generated if empty)"));

    untrackedBox_->setToolTip(
        tr("Equivalent to 'git stash push -u'. New files that aren't yet "
           "tracked by git will be saved into the stash too."));
    keepIndexBox_->setToolTip(
        tr("Equivalent to 'git stash push --keep-index'. Anything you've "
           "already staged stays in the working tree after the stash."));

    auto* form = new QFormLayout;
    form->addRow(tr("Message:"), messageEdit_);

    auto* hint = new QLabel(
        tr("Stashing saves your uncommitted changes onto a stack so the "
           "working tree becomes clean. Pop them later with 'Manage stashes…'."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Stash"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(untrackedBox_);
    root->addWidget(keepIndexBox_);
    root->addSpacing(6);
    root->addWidget(hint);
    root->addWidget(buttons);

    messageEdit_->setFocus();
}

QString StashSaveDialog::message() const         { return messageEdit_->text().trimmed(); }
bool    StashSaveDialog::includeUntracked() const { return untrackedBox_->isChecked(); }
bool    StashSaveDialog::keepIndex() const        { return keepIndexBox_->isChecked(); }

} // namespace ghm::ui
