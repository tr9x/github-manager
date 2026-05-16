#pragma once

// MainWindow - top-level shell. Owns the GitHub client, the secure
// storage, the git worker, and the two main panels. It coordinates
// signals between them so that lower-level components don't know about
// each other.

#include <QMainWindow>
#include <QString>
#include <QHash>
#include <vector>

#include "github/Repository.h"
#include "git/GitHandler.h"
// SecureStorage and Settings are held by-value below, so we need
// their complete definitions here (not just forward declarations) so
// the compiler knows their size.
#include "core/SecureStorage.h"
#include "core/Settings.h"
#include "session/SessionController.h"
#include "workspace/LocalWorkspaceController.h"
#include "workspace/PublishController.h"
#include "workspace/ConflictController.h"
#include "workspace/GitHubCloneController.h"

class QLabel;
class QProgressBar;
class QSplitter;
class QStackedWidget;
class QAction;

// Forward declarations for Qt-owned types accessed via pointer (no
// size needed in the header).
namespace ghm::github { class GitHubClient; }
namespace ghm::git    { class GitWorker; }

namespace ghm::ui {
class StashListDialog;
class TagsDialog;
class ReflogDialog;
class HostKeyApprover;
class TlsCertApprover;
}

namespace ghm::ui {

class RepositoryListWidget;
class RepositoryDetailWidget;
class LocalRepositoryWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;

private Q_SLOTS:
    // -- Session controller relays --------------------------------------
    // SessionController owns auth logic; MainWindow just reflects state
    // changes in the UI (window title, status bar, repo list, etc.)
    // via these slots.
    void onSessionSignedIn       (const QString& username);
    void onSessionRestored       (const QString& username);
    void onSessionSignedOut      ();
    void onSessionAuthError      (const QString& reason);
    void onSessionNetworkError   (const QString& message);
    void onSessionKeyringError   (const QString& message);
    void onSessionBusy           (const QString& message);
    void onSessionReposLoaded    (const QList<ghm::github::Repository>& repos);

    // User-initiated sign-out (with confirmation dialog).
    void requestSignOut();

    // -- GitHub-clone flow (existing) -----------------------------------
    // GitHubCloneController owns the clone state machine and the
    // open-existing validation. MainWindow keeps the dialog handling
    // and translates controller signals to UI side-effects.
    void onCloneRequested(const ghm::github::Repository& repo);
    void onOpenLocallyRequested(const ghm::github::Repository& repo);
    void onPullRequested(const QString& localPath);
    void onPushRequested(const QString& localPath);
    void onRefreshRequested(const QString& localPath);
    void onSwitchBranchRequested(const QString& localPath, const QString& branch);

    // Clone controller feedback
    void onCloneStarted(const ghm::github::Repository& repo,
                        const QString& localPath);
    void onCloneSucceeded(const ghm::github::Repository& repo,
                          const QString& localPath);
    void onOpenSucceeded(const ghm::github::Repository& repo,
                         const QString& localPath);
    void onCloneFailed(const ghm::github::Repository& repo,
                       const QString& localPath,
                       const QString& title,
                       const QString& detail);
    void onDefaultCloneDirectoryChanged(const QString& newDirectory);

    void onPullFinished (bool ok, const QString& localPath, const QString& error);
    void onPushFinished (bool ok, const QString& localPath, const QString& error);
    void onStatusReady  (const QString& localPath,
                         const QString& branch,
                         const ghm::git::StatusSummary& s);
    void onBranchSwitched(bool ok, const QString& localPath,
                          const QString& branch, const QString& error);
    void onBranchesReady(const QString& localPath, const QStringList& branches);

    // -- Local folder workflow ------------------------------------------
    void onAddLocalFolderClicked();
    void onLocalFolderActivated(const QString& path);
    void onRemoveLocalFolderRequested(const QString& path);

    // Visibility change flow. Triggered when the user picks
    // "Make public" / "Make private" from the GitHub list's
    // context menu. We pop a confirmation dialog with the
    // appropriate warnings (private→public exposes the code;
    // public→private erases stars/watchers) and only call the
    // API on confirm. onVisibilityChanged is the success path,
    // patches the cache + sidebar.
    void onChangeVisibilityRequested(const ghm::github::Repository& repo,
                                       bool makePrivate);
    void onVisibilityChanged(const ghm::github::Repository& updated);

    // -- Slots that pass through directly to LocalWorkspaceController --
    // Most of the local-folder request signals are forwarded to the
    // controller via Qt signal-to-slot wiring in wireSignals();
    // these slots remain in MainWindow because they pop dialogs that
    // need a QWidget parent (AddRemote, push) or interact with the
    // GitHub session (push uses the PAT).
    void onLocalAddRemoteRequested (const QString& path);
    void onLocalRemoveRemoteRequested(const QString& path, const QString& name);
    void onLocalPushRequested      (const QString& path,
                                    const QString& remoteName,
                                    const QString& branch,
                                    bool           setUpstream);
    // Delete branch goes through MainWindow first because we want a
    // confirmation dialog before any worker call. After confirmation,
    // delegates to LocalWorkspaceController.
    void onLocalBranchDeleteRequested(const QString& path, const QString& branch);

    // -- Repository menu (stash + tags) --------------------------------
    void onStashSaveRequested();
    void onStashListRequested();
    void onStashApplyRequested(int index);
    void onStashPopRequested  (int index);
    void onStashDropRequested (int index);
    void onTagsRequested();
    void onTagCreateRequested(const QString& name, const QString& message);
    void onTagDeleteRequested(const QString& name);

    // Fetch / undo commit
    void onFetchRequested();
    void onFetchFinished(bool ok, const QString& path,
                         const QString& remoteName, const QString& error);
    void onUndoLastCommitRequested();
    void onUndoLastCommitFinished(bool ok, const QString& path, const QString& error);

    // Reflog viewer / recovery
    void onReflogRequested();
    void onReflogReady(const QString& path,
                       const std::vector<ghm::git::ReflogEntry>& entries);
    void onReflogRestoreRequested(const QString& sha);
    void onSoftResetFinished(bool ok, const QString& path,
                             const QString& sha, const QString& error);

    // -- Conflict resolution -------------------------------------------
    // ConflictController owns the dialog and the per-flow state.
    // MainWindow only triggers the flow and translates controller
    // signals into UI side-effects.
    void onResolveConflictsRequested(const QString& path);
    void onConflictStatusChanged    (const QString& message);
    void onConflictSucceeded        (const QString& message);
    void onConflictFailed           (const QString& title, const QString& error);
    void onConflictAllResolved      ();
    void onConflictWorkingTreeChanged(const QString& path);

    // -- LocalWorkspaceController feedback -----------------------------
    // The controller does the heavy lifting (validation + worker calls
    // + path filtering + force-delete escalation detection). MainWindow
    // only handles UI reactions: status bar updates, error dialogs,
    // and the few prompts the controller can't show itself.
    void onWorkspaceStateRefreshed(const QString& path,
                                   bool isRepository,
                                   const QString& branch,
                                   const std::vector<ghm::git::StatusEntry>& entries,
                                   const std::vector<ghm::git::RemoteInfo>& remotes);
    void onWorkspaceStageOpFinished  (bool ok, const QString& path, const QString& error);
    void onWorkspaceUnstageOpFinished(bool ok, const QString& path, const QString& error);
    void onWorkspaceCommitFinished   (bool ok, const QString& path,
                                      const QString& sha, const QString& error);
    void onWorkspaceInitFinished     (bool ok, const QString& path, const QString& error);
    void onWorkspaceHistoryReady     (const QString& path,
                                      const std::vector<ghm::git::CommitInfo>& commits,
                                      bool isAppend);
    void onWorkspaceFileDiffReady    (const QString& path,
                                      const QString& repoRelPath,
                                      const ghm::git::FileDiff& diff,
                                      const QString& error);
    void onWorkspaceCommitDiffReady  (const QString& path,
                                      const QString& sha,
                                      const std::vector<ghm::git::FileDiff>& files,
                                      const QString& error);
    void onWorkspaceBranchInfosReady (const QString& path,
                                      const std::vector<ghm::git::BranchInfo>& infos);
    void onWorkspaceBranchSwitched   (bool ok, const QString& path,
                                      const QString& branch, const QString& error);
    void onWorkspaceBranchCreated    (bool ok, const QString& path,
                                      const QString& branch, const QString& error);
    void onWorkspaceBranchDeleted    (bool ok, const QString& path,
                                      const QString& branch, const QString& error);
    void onWorkspaceBranchRenamed    (bool ok, const QString& path,
                                      const QString& oldName,
                                      const QString& newName,
                                      const QString& error);

    // -- Dialog prompts requested by LocalWorkspaceController ----------
    void onWorkspaceIdentityRequiredForCommit(const QString& path,
                                              const QString& pendingMessage);
    void onWorkspaceBranchCreateDialogRequested(const QString& path,
                                                const QStringList& existing);
    void onWorkspaceBranchForceDeleteConfirmation(const QString& path,
                                                  const QString& branch,
                                                  const QString& reason);
    void onWorkspaceBranchRenameDialogRequested(const QString& path,
                                                const QString& oldName,
                                                const QStringList& existing);
    // -- Publish flow feedback -----------------------------------------
    // PublishController does the heavy lifting; MainWindow handles
    // UI side-effects (status bar, dialogs, repo cache updates).
    void onPublishProgress(const QString& message);
    void onPublishSucceeded(const QString& localPath,
                            const QString& cloneUrl,
                            const QString& repoFullName,
                            bool pushed);
    void onPublishFailed(const QString& localPath,
                         const QString& title,
                         const QString& detail);
    void onPublishRepoCreated(const ghm::github::Repository& repo,
                              const QString& localPath);
    void onPublishNeedNonEmptyBranch(const QString& localPath);

    void onPublishToGitHubRequested(const QString& path);
    void onEditIdentityRequested();

    // -- App-level UI ---------------------------------------------------
    void onLanguageChosen(const QString& code);
    void onShowSupportDialog();

    // Worker callbacks for local flow.
    //
    // Most local-flow worker callbacks moved into LocalWorkspaceController
    // (see workspace/LocalWorkspaceController.h); publish-related ones
    // moved into PublishController. MainWindow keeps only callbacks
    // that drive UI features the controllers don't own (stash, tags,
    // conflict resolution, manual push from Remotes tab, network errors).
    void onStashOpFinished(bool ok, const QString& path,
                           const QString& operation, const QString& error);
    void onStashListReady (const QString& path,
                           const std::vector<ghm::git::StashEntry>& entries);
    void onTagOpFinished  (bool ok, const QString& path,
                           const QString& operation, const QString& name,
                           const QString& error);
    void onTagsReady      (const QString& path,
                           const std::vector<ghm::git::TagInfo>& tags);

    // Submodule op result. We re-list submodules afterwards so the
    // status column refreshes — same pattern as tags/stash above.
    void onSubmoduleOpFinished(bool ok, const QString& path,
                                const QString& operation,
                                const QString& name,
                                const QString& error);

    // Add: pops AddSubmoduleDialog and optionally SshKeyDialog, then
    // dispatches to worker. Path is the active local repo path.
    void onSubmoduleAddRequested(const QString& path);

    // Remove: confirms with a destructive-action dialog, then
    // dispatches. Refuses if confirmation is declined.
    void onSubmoduleRemoveRequested(const QString& path,
                                     const QString& name);

    // Transfer progress for network ops (clone/pull/push/fetch).
    // Translates the worker's (phase, current, total) tuples into
    // status-bar progress bar updates. Indeterminate while total
    // is unknown, determinate (with object-count text) when known.
    void onWorkerProgress(const QString& phase, qint64 current, qint64 total);

    // Conflict worker signals are owned by ConflictController now.
    // MainWindow no longer subscribes to conflictsReady /
    // conflictBlobsReady / conflictOpFinished.

    void onRemoteOpFinished(bool ok, const QString& path, const QString& error);

private:
    void buildUi();
    void buildActions();
    void wireSignals();
    void setStatus(const QString& text, int timeoutMs = 0);

    // Reads gpg.ssh.allowedSignersFile from global git config and
    // expands a leading "~/" into $HOME. Empty when unset. Used by
    // signature verification to feed ssh-keygen -Y verify.
    QString sshAllowedSignersPath() const;
    void setBusy(bool busy, const QString& label = {});
    void rememberLocalPath(const QString& fullName, const QString& localPath);
    bool ensureIdentity();   // prompts via IdentityDialog if needed
    void pushIdentityToWidget();

    // -- collaborators --
    // Owned by-value: these classes hold no Qt parent-child semantics
    // and aren't polymorphic, so heap allocation buys us nothing. Stack
    // ownership eliminates the need for explicit delete in the destructor
    // and makes lifetime obvious from the field declaration.
    ghm::core::SecureStorage         storage_;
    ghm::core::Settings              settings_;
    ghm::session::SessionController* session_;
    ghm::workspace::LocalWorkspaceController* workspace_{nullptr};
    ghm::workspace::PublishController*        publish_{nullptr};
    ghm::workspace::ConflictController*       conflict_{nullptr};
    ghm::workspace::GitHubCloneController*    clone_{nullptr};
    HostKeyApprover*                          hostKeyApprover_{nullptr};
    TlsCertApprover*                          tlsCertApprover_{nullptr};
    ghm::git::GitWorker*             worker_;

    // -- session state --
    // username/token live in session_ now (access via session_->username()
    // and session_->token()).
    QHash<QString, QString> localPathByFullName_;  // GitHub clone tracking
    QString activeLocalPath_;                       // currently-shown local folder
    QList<ghm::github::Repository> reposCache_;     // last-fetched GitHub repos

    // Publish flow lives in PublishController (see workspace/).
    // MainWindow keeps only the dialog handling — controller does
    // the state machine, watchdog, and reset-on-abort.

    // -- widgets --
    QSplitter*               splitter_;
    RepositoryListWidget*    repoList_;
    QStackedWidget*          detailStack_;
    RepositoryDetailWidget*  repoDetail_;
    LocalRepositoryWidget*   localDetail_;

    QLabel*                  userLabel_;
    QLabel*                  statusMessage_;
    QProgressBar*            progress_;
    QAction*                 refreshAction_;
    QAction*                 logoutAction_;
    QAction*                 quitAction_;
    QAction*                 addLocalAction_;

    // Repository menu items.
    QAction*                 fetchAction_{nullptr};
    QAction*                 stashSaveAction_{nullptr};
    QAction*                 stashListAction_{nullptr};
    QAction*                 tagsAction_{nullptr};
    QAction*                 reflogAction_{nullptr};
    QAction*                 undoCommitAction_{nullptr};
    QAction*                 openFolderAction_{nullptr};

    // Modeless dialogs — kept as pointers so we can refresh their
    // contents while they're open (e.g. after a stash apply finishes,
    // we feed the new list back without the user having to reopen).
    // Created lazily on first use; lifetime tied to MainWindow's
    // QObject parent.
    StashListDialog*         stashListDialog_{nullptr};
    TagsDialog*              tagsDialog_{nullptr};
    ReflogDialog*            reflogDialog_{nullptr};
    // ConflictController owns the conflict dialog and the per-flow
    // state (last entries cache). MainWindow only triggers the flow
    // via conflict_->start(path) and reacts to its high-level signals.
};

} // namespace ghm::ui
