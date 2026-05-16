#include "ui/ReflogDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>

#include "core/TimeFormatting.h"

namespace ghm::ui {

// Custom role for stashing the new SHA on each list item, so the
// restore action can read it without parsing the visible text.
constexpr int kNewShaRole = Qt::UserRole + 1;

ReflogDialog::ReflogDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Reflog — HEAD history"));
    resize(720, 520);

    auto* layout = new QVBoxLayout(this);

    // Header notice. Explains what reflog is and that it's a local
    // safety net with limits — we'd rather the user understand than
    // discover later that their commits are actually gone for good.
    notice_ = new QLabel(tr(
        "Reflog is git's local journal of HEAD moves — every commit, "
        "checkout, reset, merge and rebase appends one entry. If you "
        "want to recover from a destructive operation, pick the entry "
        "showing the state you want and click <b>Restore HEAD here</b>.<br><br>"
        "<b>Caveats:</b> reflog is local only (not synced to remotes), "
        "expires after 90 days by default, and a manual <code>git gc</code> "
        "or push-fail-induced cleanup can reap the underlying commits "
        "earlier."), this);
    notice_->setWordWrap(true);
    notice_->setStyleSheet(QStringLiteral(
        "padding: 8px; background: #2a2d33; border-radius: 4px;"));
    layout->addWidget(notice_);

    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setUniformItemSizes(true);
    layout->addWidget(list_, 1);

    auto* btnRow = new QHBoxLayout;
    restoreBtn_ = new QPushButton(tr("Restore HEAD here (soft reset)"), this);
    restoreBtn_->setEnabled(false);
    restoreBtn_->setToolTip(tr(
        "Move HEAD to the new SHA from the selected entry. Working "
        "tree is not touched — any changes from intermediate commits "
        "reappear as staged so you can review before re-committing."));
    refreshBtn_ = new QPushButton(tr("Refresh"), this);
    closeBtn_   = new QPushButton(tr("Close"), this);
    btnRow->addWidget(restoreBtn_);
    btnRow->addWidget(refreshBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);
    layout->addLayout(btnRow);

    connect(list_, &QListWidget::itemSelectionChanged,
            this, &ReflogDialog::onSelectionChanged);
    connect(restoreBtn_, &QPushButton::clicked,
            this, &ReflogDialog::onRestoreClicked);
    connect(refreshBtn_, &QPushButton::clicked,
            this, &ReflogDialog::refreshRequested);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);
}

void ReflogDialog::setEntries(const std::vector<ghm::git::ReflogEntry>& entries)
{
    list_->clear();

    if (entries.empty()) {
        // Either a brand-new repo (no operations yet) or a repo where
        // git wasn't told to keep a reflog (rare but possible via
        // `core.logAllRefUpdates=false`). Either way the user sees
        // why the list is empty.
        auto* item = new QListWidgetItem(
            tr("(no reflog entries — repository may be brand new "
               "or have core.logAllRefUpdates disabled)"),
            list_);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        return;
    }

    for (const auto& e : entries) {
        // Format: "<newSha-short>  <relative time>  <message>"
        // We don't show oldSha by default — most users care about
        // "where would I go if I restored", not "where was I before".
        // The full new SHA goes into the user role for the action.
        const QString shortSha = e.newSha.left(7);
        const QString line = QStringLiteral("%1  %2  —  %3")
            .arg(shortSha,
                 e.when.isValid()
                     ? ghm::core::relativeTime(e.when)
                     : QString(),
                 e.message);
        auto* item = new QListWidgetItem(line, list_);
        item->setData(kNewShaRole, e.newSha);
        // Tooltip carries the full SHA pair + committer for any user
        // who wants the precise context.
        QStringList ttLines;
        ttLines << tr("New: %1").arg(e.newSha);
        if (!e.oldSha.isEmpty()) {
            ttLines << tr("Was: %1").arg(e.oldSha);
        }
        if (!e.committerName.isEmpty()) {
            ttLines << tr("By:  %1 <%2>").arg(e.committerName, e.committerEmail);
        }
        item->setToolTip(ttLines.join(QLatin1Char('\n')));
    }
}

void ReflogDialog::setBusy(bool busy)
{
    restoreBtn_->setEnabled(!busy && list_->currentItem() != nullptr);
    refreshBtn_->setEnabled(!busy);
    list_->setEnabled(!busy);
}

void ReflogDialog::onSelectionChanged()
{
    restoreBtn_->setEnabled(list_->currentItem() != nullptr);
}

void ReflogDialog::onRestoreClicked()
{
    auto* item = list_->currentItem();
    if (!item) return;
    const QString sha = item->data(kNewShaRole).toString();
    if (sha.isEmpty()) return;

    const auto reply = QMessageBox::question(this,
        tr("Restore HEAD"),
        tr("Restore HEAD to <b>%1</b>?<br><br>"
           "This runs <code>git reset --soft</code> — your working tree "
           "is not touched, but the branch pointer moves to this commit. "
           "Any commits made after this point will be \"abandoned\" from "
           "the branch's history (they remain in the object database "
           "and in the reflog for another 30–90 days).<br><br>"
           "If this branch was already pushed, your next push will need "
           "<code>--force</code> — which rewrites history for everyone "
           "else with a copy. Be sure.").arg(sha.left(7)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    Q_EMIT restoreRequested(sha);
}

} // namespace ghm::ui
