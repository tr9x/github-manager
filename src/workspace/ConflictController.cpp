#include "workspace/ConflictController.h"

#include "git/GitWorker.h"
#include "ui/ConflictResolutionDialog.h"

namespace ghm::workspace {

ConflictController::ConflictController(ghm::git::GitWorker& worker,
                                       QWidget*             dialogParent,
                                       QObject*             parent)
    : QObject(parent)
    , worker_(worker)
    , dialogParent_(dialogParent)
{
    // Subscribe to every relevant worker signal up-front. Each handler
    // filters by activePath_ — if there's no active flow the events
    // just fall through. Same pattern as PublishController.
    connect(&worker_, &ghm::git::GitWorker::conflictsReady,
            this, &ConflictController::onConflictsReady);
    connect(&worker_, &ghm::git::GitWorker::conflictBlobsReady,
            this, &ConflictController::onConflictBlobsReady);
    connect(&worker_, &ghm::git::GitWorker::conflictOpFinished,
            this, &ConflictController::onConflictOpFinished);
}

bool ConflictController::start(const QString& path)
{
    if (path.isEmpty()) return false;

    activePath_ = path;
    ensureDialog();
    dialog_->setRepoPath(path);
    worker_.listConflicts(path);
    dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
    return true;
}

void ConflictController::reset()
{
    activePath_.clear();
    lastEntries_.clear();
    if (dialog_ && dialog_->isVisible()) dialog_->close();
}

void ConflictController::ensureDialog()
{
    if (dialog_) return;
    dialog_ = new ghm::ui::ConflictResolutionDialog(dialogParent_);
    connect(dialog_, &ghm::ui::ConflictResolutionDialog::entrySelectionChanged,
            this, &ConflictController::onDialogEntrySelectionChanged);
    connect(dialog_, &ghm::ui::ConflictResolutionDialog::markResolvedRequested,
            this, &ConflictController::onDialogMarkResolved);
    connect(dialog_, &ghm::ui::ConflictResolutionDialog::abortMergeRequested,
            this, &ConflictController::onDialogAbortMerge);
}

// ----- Worker signal handlers ----------------------------------------------

void ConflictController::onConflictsReady(
        const QString& path,
        const std::vector<ghm::git::ConflictEntry>& entries)
{
    if (path != activePath_) return;
    if (!dialog_) return;

    lastEntries_ = entries;
    dialog_->setEntries(entries);

    // Auto-load blobs for the first entry so the panes aren't empty
    // when the dialog first opens. Subsequent picks fire the same
    // request via entrySelectionChanged.
    if (!entries.empty()) {
        worker_.loadConflictBlobs(path, entries.front());
        return;
    }

    // No conflicts left ⇒ the merge is ready to commit. Close the
    // dialog and tell the host so it can prompt for the merge commit.
    if (dialog_->isVisible()) {
        dialog_->accept();
    }
    const QString completedPath = activePath_;
    reset();
    Q_EMIT allResolved();
    Q_EMIT workingTreeChanged(completedPath);
}

void ConflictController::onConflictBlobsReady(
        const QString& path,
        const ghm::git::ConflictEntry& entry)
{
    if (path != activePath_) return;
    if (dialog_) dialog_->setBlobsForEntry(entry);
}

void ConflictController::onConflictOpFinished(
        bool ok, const QString& path,
        const QString& operation,
        const QString& filePath,
        const QString& error)
{
    if (path != activePath_) return;
    if (dialog_) dialog_->setBusy(false);

    if (!ok) {
        const QString title = (operation == QLatin1String("resolve"))
            ? tr("Mark resolved failed")
            : tr("Abort merge failed");
        Q_EMIT operationFailed(title, error);
        // Working tree may still have changed (e.g. abort partially
        // ran) — fire workingTreeChanged so host refreshes anyway.
        Q_EMIT workingTreeChanged(path);
        return;
    }

    if (operation == QLatin1String("resolve")) {
        Q_EMIT operationSucceeded(
            tr("Marked %1 as resolved.").arg(filePath));
        // Re-list — the dialog updates and the "all resolved" branch
        // in onConflictsReady fires when the list becomes empty.
        worker_.listConflicts(path);
        Q_EMIT workingTreeChanged(path);
        return;
    }

    if (operation == QLatin1String("abort")) {
        Q_EMIT operationSucceeded(tr("Merge aborted."));
        if (dialog_) dialog_->accept();
        const QString abortedPath = activePath_;
        reset();
        Q_EMIT mergeAborted();
        Q_EMIT workingTreeChanged(abortedPath);
        return;
    }

    // Unknown operation tag — just refresh, don't pretend success.
    Q_EMIT workingTreeChanged(path);
}

// ----- Dialog handlers -----------------------------------------------------

void ConflictController::onDialogEntrySelectionChanged(const QString& filePath)
{
    if (activePath_.isEmpty() || filePath.isEmpty()) return;
    // Look up the entry in our cached list so we have the three OIDs.
    // The dialog itself doesn't carry them back through the signal,
    // and re-listing every selection would be wasteful.
    for (const auto& e : lastEntries_) {
        if (e.path == filePath) {
            worker_.loadConflictBlobs(activePath_, e);
            return;
        }
    }
}

void ConflictController::onDialogMarkResolved(const QString& filePath)
{
    if (activePath_.isEmpty() || filePath.isEmpty()) return;
    if (dialog_) dialog_->setBusy(true);
    Q_EMIT statusChanged(tr("Marking %1 as resolved…").arg(filePath));
    worker_.markResolved(activePath_, filePath);
}

void ConflictController::onDialogAbortMerge()
{
    if (activePath_.isEmpty()) return;
    if (dialog_) dialog_->setBusy(true);
    Q_EMIT statusChanged(tr("Aborting merge…"));
    worker_.abortMerge(activePath_);
}

} // namespace ghm::workspace
