#pragma once

// LocalWorkspaceController — orchestrates everything to do with a
// single open local Git folder.
//
// Why this exists: MainWindow had ~30 onLocalXxx slots that all
// followed the same shape: "validate args → call GitWorker → on result,
// flash a status message and maybe show an error dialog". The
// validation and "ask the worker" half belongs together (it knows
// about identity prompts, force-delete escalations, branch name
// collisions); only the UI feedback half — status bar, error dialogs
// — really wants to live next to the QMainWindow.
//
// Design choices:
//   * Controller does NOT own the GitWorker. MainWindow does, because
//     other features (publish flow, conflict resolution, GitHub-clone
//     view) also use it. The controller composes it and is given a
//     reference at construction.
//   * Controller does NOT own the LocalRepositoryWidget either — that
//     widget belongs to the QMainWindow's layout. The controller
//     receives the widget by reference so it can query it (for things
//     like existing branch names) and toggle its busy state, but it
//     never calls show() / hide() on it.
//   * Output is via Q_SIGNALS, not direct calls into MainWindow. This
//     keeps the controller testable in isolation (a mock host could
//     subscribe to the signals to assert on what the controller asked
//     for) and avoids any coupling back to the QMainWindow type.
//
// State tracked here:
//   * activePath_ — the folder currently shown in the local-repo widget.
//     Set by setActivePath() from MainWindow's onLocalFolderActivated
//     hook. Worker callbacks are filtered against this so that a
//     stale result for a previously-active folder doesn't crash into
//     the current view.
//
// State NOT tracked here:
//   * Author identity. That's a Settings property, fetched at the
//     point of use. Caching here would complicate identity-edit
//     flows (the user can change identity mid-session).
//   * Pending publish state. Belongs to a future PublishController.

#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

#include "git/GitHandler.h"  // for StatusEntry / RemoteInfo / etc

namespace ghm::core { class Settings; }
namespace ghm::git  { class GitWorker; }
namespace ghm::ui   { class LocalRepositoryWidget; }

namespace ghm::workspace {

class LocalWorkspaceController : public QObject {
    Q_OBJECT
public:
    LocalWorkspaceController(ghm::git::GitWorker&        worker,
                             ghm::core::Settings&        settings,
                             ghm::ui::LocalRepositoryWidget& widget,
                             QObject*                    parent = nullptr);

    // Tell the controller which folder is currently visible. Worker
    // callbacks for other folders are silently ignored. Pass empty
    // string to indicate "no local folder shown" (e.g. user switched
    // to GitHub-clone view).
    void setActivePath(const QString& path);

    QString activePath() const { return activePath_; }

    // Re-read the active folder's status. Used by the host after
    // operations that originate outside the controller (e.g. publish
    // flow, conflict mark-resolved) to keep the local view in sync.
    void refreshActiveFolder();

public Q_SLOTS:
    // -- Wired up by host to LocalRepositoryWidget signals -------------
    // Each slot validates inputs and delegates to the worker. Results
    // arrive on the worker's signals which we re-emit as the
    // higher-level signals below.

    void onInitRequested      (const QString& path, const QString& branch);
    void onStageAllRequested  (const QString& path);
    void onStagePathsRequested(const QString& path, const QStringList& paths);
    void onUnstagePathsRequested(const QString& path, const QStringList& paths);
    void onCommitRequested    (const QString& path, const QString& message);
    void onRefreshRequested   (const QString& path);
    void onHistoryRequested   (const QString& path);

    // Fetch the next page of older commits, starting after `afterSha`.
    // Triggered by the user clicking the "Load more older commits…"
    // sentinel in the History tab.
    void onLoadMoreHistoryRequested(const QString& path,
                                     const QString& afterSha);
    void onDiffRequested      (const QString& path,
                               const QString& repoRelPath,
                               ghm::git::DiffScope scope);
    void onCommitDiffRequested(const QString& path, const QString& sha);
    void onCommitCompareRequested(const QString& path,
                                  const QString& shaA,
                                  const QString& shaB);

    // Branch ops
    void onBranchSwitchRequested(const QString& path, const QString& branch);
    void onBranchCreateRequested(const QString& path);
    void onBranchDeleteRequested(const QString& path, const QString& branch);
    void onBranchRenameRequested(const QString& path, const QString& oldName);

Q_SIGNALS:
    // -- High-level outcomes the host (MainWindow) reacts to -----------
    // The host uses these to flash status messages, raise error
    // dialogs, and update the local-repo widget. The controller
    // intentionally doesn't touch any of those itself.

    // Fired when the worker's localStateReady comes in for the active
    // folder. Args mirror the worker signal so the host can pass
    // straight through to the widget.
    void stateRefreshed(const QString& path,
                        bool isRepository,
                        const QString& branch,
                        const std::vector<ghm::git::StatusEntry>& entries,
                        const std::vector<ghm::git::RemoteInfo>& remotes);

    // Stage/unstage finished. Errors carry the worker's message verbatim.
    void stageOpFinished  (bool ok, const QString& path, const QString& error);
    void unstageOpFinished(bool ok, const QString& path, const QString& error);

    // Commit + init lifecycle.
    void commitFinished(bool ok, const QString& path,
                        const QString& sha, const QString& error);
    void initFinished  (bool ok, const QString& path, const QString& error);

    // History / diff results pass through.
    void historyReady   (const QString& path,
                         const std::vector<ghm::git::CommitInfo>& commits,
                         bool isAppend);
    void fileDiffReady  (const QString& path,
                         const QString& repoRelPath,
                         const ghm::git::FileDiff& diff,
                         const QString& error);
    void commitDiffReady(const QString& path,
                         const QString& sha,
                         const std::vector<ghm::git::FileDiff>& files,
                         const QString& error);

    // Branch operations.
    void branchInfosReady(const QString& path,
                          const std::vector<ghm::git::BranchInfo>& infos);
    void branchSwitched  (bool ok, const QString& path,
                          const QString& branch, const QString& error);
    void branchCreated   (bool ok, const QString& path,
                          const QString& branch, const QString& error);
    void branchDeleted   (bool ok, const QString& path,
                          const QString& branch, const QString& error);
    void branchRenamed   (bool ok, const QString& path,
                          const QString& oldName, const QString& newName,
                          const QString& error);

    // Identity prompt request — controller noticed the user tried to
    // commit without configured author identity. Host opens its
    // IdentityDialog; on success it persists to Settings and re-fires
    // onCommitRequested.
    void identityRequiredForCommit(const QString& path, const QString& pendingMessage);

    // Branch-create dialog request — controller wants the host to
    // open CreateBranchDialog with the existing branch list and call
    // back via createBranchAccepted() when done.
    void branchCreateDialogRequested(const QString& path,
                                     const QStringList& existingNames);

    // Branch-delete escalation: a non-force delete failed because the
    // branch wasn't merged. Host should ask the user whether to
    // force-delete and call back via forceDeleteConfirmed().
    void branchForceDeleteConfirmation(const QString& path,
                                       const QString& branch,
                                       const QString& reason);

    // Branch-rename dialog request — controller wants the host to
    // prompt for the new name (with existing names for collision
    // checking).
    void branchRenameDialogRequested(const QString& path,
                                     const QString& oldName,
                                     const QStringList& existingNames);

    // Operation-in-progress hint. Host shows this in the status bar
    // and progress indicator. Empty message means "operation done"
    // and the host clears the indicator.
    void busy(const QString& message);
    void idle();

public Q_SLOTS:
    // -- Callbacks from host after handling dialog requests -----------

    // Continuation of onCommitRequested after identity was supplied.
    void commitWithKnownIdentity(const QString& path, const QString& message);

    // The host prompted CreateBranchDialog and the user accepted.
    void createBranchAccepted(const QString& path, const QString& name,
                              bool checkout);

    // The host prompted "force delete?" and the user confirmed.
    void forceDeleteConfirmed(const QString& path, const QString& branch);

    // The host prompted for a new branch name and the user accepted.
    void renameBranchAccepted(const QString& path,
                              const QString& oldName,
                              const QString& newName);

private Q_SLOTS:
    // -- Worker callbacks ---------------------------------------------
    void onWorkerLocalStateReady(const QString& path,
                                 bool                                       isRepository,
                                 const QString&                             branch,
                                 const std::vector<ghm::git::StatusEntry>&  entries,
                                 const std::vector<ghm::git::RemoteInfo>&   remotes);
    void onWorkerStageFinished  (bool ok, const QString& path, const QString& error);
    void onWorkerUnstageFinished(bool ok, const QString& path, const QString& error);
    void onWorkerCommitFinished (bool ok, const QString& path,
                                 const QString& sha, const QString& error);
    void onWorkerInitFinished   (bool ok, const QString& path, const QString& error);
    void onWorkerHistoryReady   (const QString& path,
                                 const std::vector<ghm::git::CommitInfo>& commits,
                                 bool isAppend);
    void onWorkerFileDiffReady  (const QString& path,
                                 const QString& repoRelPath,
                                 const ghm::git::FileDiff& diff,
                                 const QString& error);
    void onWorkerCommitDiffReady(const QString& path,
                                 const QString& sha,
                                 const std::vector<ghm::git::FileDiff>& files,
                                 const QString& error);
    void onWorkerBranchInfosReady(const QString& path,
                                  const std::vector<ghm::git::BranchInfo>& infos);
    void onWorkerBranchSwitched (bool ok, const QString& path,
                                 const QString& branch, const QString& error);
    void onWorkerBranchCreated  (bool ok, const QString& path,
                                 const QString& branch, const QString& error);
    void onWorkerBranchDeleted  (bool ok, const QString& path,
                                 const QString& branch, const QString& error);
    void onWorkerBranchRenamed  (bool ok, const QString& path,
                                 const QString& oldName, const QString& newName,
                                 const QString& error);

private:
    ghm::git::GitWorker&        worker_;
    ghm::core::Settings&        settings_;
    ghm::ui::LocalRepositoryWidget& widget_;

    QString activePath_;
};

} // namespace ghm::workspace
