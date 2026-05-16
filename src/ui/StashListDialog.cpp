#include "ui/StashListDialog.h"
#include "core/TimeFormatting.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QMessageBox>
#include <QFontDatabase>

namespace ghm::ui {

namespace {
// Carries the stash-stack index so the action buttons can find it
// without searching the list by message.
constexpr int kStashIndexRole = Qt::UserRole + 1;
} // namespace

StashListDialog::StashListDialog(QWidget* parent)
    : QDialog(parent)
    , list_       (new QListWidget(this))
    , placeholder_(new QLabel(tr("No stashes — your working tree is clean."), this))
    , applyBtn_   (new QPushButton(tr("Apply"), this))
    , popBtn_     (new QPushButton(tr("Pop"), this))
    , dropBtn_    (new QPushButton(tr("Drop"), this))
    , closeBtn_   (new QPushButton(tr("Close"), this))
{
    setWindowTitle(tr("Manage stashes"));
    setModal(true);
    resize(600, 400);

    list_->setAlternatingRowColors(true);
    list_->setUniformItemSizes(true);
    list_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &StashListDialog::onSelectionChanged);

    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setStyleSheet(QStringLiteral("color: #707680; padding: 24px;"));

    applyBtn_->setToolTip(
        tr("Apply this stash to the working tree but keep it on the stack."));
    popBtn_->setToolTip(
        tr("Apply this stash and then drop it from the stack (apply + drop)."));
    dropBtn_->setToolTip(
        tr("Discard this stash without applying it. The changes are lost."));
    popBtn_->setDefault(true);

    connect(applyBtn_, &QPushButton::clicked, this, &StashListDialog::onApplyClicked);
    connect(popBtn_,   &QPushButton::clicked, this, &StashListDialog::onPopClicked);
    connect(dropBtn_,  &QPushButton::clicked, this, &StashListDialog::onDropClicked);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(applyBtn_);
    btnRow->addWidget(popBtn_);
    btnRow->addWidget(dropBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);

    // Stack list/placeholder so empty state is obvious without leaving
    // an empty-but-not-empty list widget on screen.
    auto* stack = new QStackedWidget(this);
    stack->addWidget(placeholder_);
    stack->addWidget(list_);
    stack->setCurrentIndex(0);
    list_->setObjectName(QStringLiteral("stashList"));
    stack->setObjectName(QStringLiteral("stashStack"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(stack, 1);
    root->addLayout(btnRow);

    // Keep a pointer to the stack so setEntries can flip pages — we
    // grab it via objectName to avoid adding another field. Cleaner
    // than carrying through a QStackedWidget* member just for this.
    onSelectionChanged();
}

void StashListDialog::setEntries(const std::vector<ghm::git::StashEntry>& entries)
{
    list_->clear();
    auto* stack = findChild<QStackedWidget*>(QStringLiteral("stashStack"));
    if (stack) stack->setCurrentIndex(entries.empty() ? 0 : 1);

    for (const auto& e : entries) {
        const QString when = ghm::core::relativeTime(e.when);
        // Format mirrors `git stash list`:
        //   stash@{N}: <message>   <when>
        const QString text = QStringLiteral("stash@{%1}  %2  —  %3")
                                .arg(e.index)
                                .arg(e.message, when);
        auto* item = new QListWidgetItem(text, list_);
        item->setData(kStashIndexRole, e.index);
        item->setToolTip(QStringLiteral("commit %1").arg(e.shortId));
    }
    onSelectionChanged();
}

void StashListDialog::setBusy(bool busy)
{
    applyBtn_->setEnabled(!busy && currentIndex() >= 0);
    popBtn_  ->setEnabled(!busy && currentIndex() >= 0);
    dropBtn_ ->setEnabled(!busy && currentIndex() >= 0);
}

int StashListDialog::currentIndex() const
{
    auto* item = list_->currentItem();
    if (!item) return -1;
    return item->data(kStashIndexRole).toInt();
}

void StashListDialog::onSelectionChanged()
{
    const bool hasSelection = currentIndex() >= 0;
    applyBtn_->setEnabled(hasSelection);
    popBtn_  ->setEnabled(hasSelection);
    dropBtn_ ->setEnabled(hasSelection);
}

void StashListDialog::onApplyClicked()
{
    const int idx = currentIndex();
    if (idx < 0) return;
    Q_EMIT applyRequested(idx);
}

void StashListDialog::onPopClicked()
{
    const int idx = currentIndex();
    if (idx < 0) return;
    Q_EMIT popRequested(idx);
}

void StashListDialog::onDropClicked()
{
    const int idx = currentIndex();
    if (idx < 0) return;
    // Drop is destructive — confirm before firing. Apply/pop are
    // recoverable (pop can be undone via reflog, apply doesn't lose
    // anything), but drop genuinely loses changes.
    auto* item = list_->currentItem();
    const QString summary = item ? item->text() : QString::number(idx);
    const auto reply = QMessageBox::question(this, tr("Drop stash"),
        tr("Discard this stash? The changes will be lost.\n\n%1").arg(summary),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    Q_EMIT dropRequested(idx);
}

} // namespace ghm::ui
