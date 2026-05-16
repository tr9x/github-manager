#pragma once

// ConflictController — manages the merge-conflict resolution flow.
//
// Triggered when the user clicks "Resolve conflicts" on a repository
// that's mid-merge (or mid-rebase/cherry-pick) with conflicted files.
// From there the controller orchestrates:
//
//   1. List conflicts via the worker → display in ConflictResolutionDialog
//   2. For each user selection, load the three blobs (ancestor/ours/theirs)
//      so the dialog can show side-by-side diffs
//   3. When the user picks a resolution and clicks Mark Resolved, write
//      the chosen content via the worker and re-list
//   4. When all conflicts are resolved, auto-close the dialog and prompt
//      to commit the merge
//   5. If the user aborts, run abortMerge via the worker
//
// Owns the dialog instance (created lazily on first start()). MainWindow
// kicks off the flow with start(path) and listens to high-level
// statusChanged/done signals for status-bar + QMessageBox side-effects.
//
// State held here:
//   * activePath_ — which repo's conflicts we're currently resolving
//   * lastEntries_ — cache so entry selection can look up the OIDs
//     without re-listing (saves a round trip per selection click)

#include <QObject>
#include <QString>
#include <vector>

#include "git/GitHandler.h"  // for ConflictEntry types in slot signatures

namespace ghm::git { class GitWorker; }
namespace ghm::ui  { class ConflictResolutionDialog; }

namespace ghm::workspace {

class ConflictController : public QObject {
    Q_OBJECT
public:
    ConflictController(ghm::git::GitWorker& worker,
                       QWidget*             dialogParent,
                       QObject*             parent = nullptr);

    // True while the dialog is open and we're driving the flow.
    bool isActive() const { return !activePath_.isEmpty(); }

    QString activePath() const { return activePath_; }

    // Open the conflict resolution dialog for `path` and kick off
    // the first listConflicts. Lazy-creates the dialog on first call.
    // Returns false if path is empty.
    bool start(const QString& path);

    // Drop state and close the dialog if open. Used when the user
    // switches active folder or the host wants a clean slate.
    void reset();

Q_SIGNALS:
    // Transient status to flash in the host's status bar — e.g.
    // "Marking foo.cpp as resolved…" while the worker is busy.
    void statusChanged(const QString& message);

    // Brief success message for the status bar with a 4-second
    // timeout suggestion (host decides actual rendering).
    void operationSucceeded(const QString& message);

    // Operation failed — host shows a QMessageBox::warning with the
    // given title and the worker's error text. `title` is already
    // translated.
    void operationFailed(const QString& title, const QString& error);

    // Every conflict has been marked resolved. Host should prompt the
    // user to commit the merge with a default message.
    void allResolved();

    // The merge was aborted via the dialog's button. Host typically
    // wants to refresh status display.
    void mergeAborted();

    // Working tree changed (after resolve or abort). Host should
    // refresh the local-folder view.
    void workingTreeChanged(const QString& path);

private Q_SLOTS:
    // Reactions to worker signals.
    void onConflictsReady(const QString& path,
                          const std::vector<ghm::git::ConflictEntry>& entries);
    void onConflictBlobsReady(const QString& path,
                              const ghm::git::ConflictEntry& entry);
    void onConflictOpFinished(bool ok, const QString& path,
                              const QString& operation,
                              const QString& filePath,
                              const QString& error);

    // Reactions to dialog user actions.
    void onDialogEntrySelectionChanged(const QString& filePath);
    void onDialogMarkResolved(const QString& filePath);
    void onDialogAbortMerge();

private:
    void ensureDialog();

    ghm::git::GitWorker&                     worker_;
    QWidget*                                 dialogParent_{nullptr};
    ghm::ui::ConflictResolutionDialog*       dialog_{nullptr};

    QString                                  activePath_;
    std::vector<ghm::git::ConflictEntry>     lastEntries_;
};

} // namespace ghm::workspace
