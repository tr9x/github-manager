#pragma once

// LocalRepositoryWidget - the right-hand panel for a local folder.
//
// Two top-level states:
//   * Folder is not a git repository  -> "Initialize Repository" prompt.
//   * Folder is a git repository      -> three tabs:
//        Changes  - per-file status, stage/unstage, commit form
//        History  - git log, full message of the selected commit
//        Remotes  - list of remotes, add/remove, push controls
//
// Git work goes through MainWindow via Q_SIGNALS; this widget is
// pure UI and never touches GitHandler.
//
// Read-only Settings access is allowed (via setSettings()) for UI
// affordances that change based on persisted state — e.g. context
// menu labels reflecting whether a remembered SSH key exists for
// a given submodule. Writes still flow through signals so that
// MainWindow remains the single mutator.

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QHash>
#include <vector>

#include "git/GitHandler.h"

class QLabel;
class QPushButton;
class QToolButton;
class QLineEdit;
class QPlainTextEdit;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QTabWidget;
class QTableWidget;
class QComboBox;
class QCheckBox;
class QFrame;
class QSplitter;
class QFileSystemWatcher;
class QTimer;

namespace ghm::core { class Settings; }

namespace ghm::ui {

class DiffViewWidget;

class LocalRepositoryWidget : public QWidget {
    Q_OBJECT
public:
    explicit LocalRepositoryWidget(QWidget* parent = nullptr);
    ~LocalRepositoryWidget() override;

    // Provide a Settings handle so the widget can READ persisted
    // affordances (e.g. remembered SSH keys per submodule).
    // MainWindow injects this right after construction. Writes
    // continue to flow through signals.
    void setSettings(ghm::core::Settings* settings) { settings_ = settings; }

    // Switch the panel to a different folder and clear stale state.
    // The MainWindow follows this with a refreshLocalState() request.
    void setFolder(const QString& path);
    QString currentPath() const { return path_; }

    // Snapshot from GitWorker::localStateReady.
    void setLocalState(bool                                       isRepository,
                       const QString&                             branch,
                       const std::vector<ghm::git::StatusEntry>&  entries,
                       const std::vector<ghm::git::RemoteInfo>&   remotes);

    // Fed by GitWorker::historyReady when isAppend=false. REPLACES
    // the entire list. If `commits.size() >= page size`, also appends
    // a "Load more older commits…" sentinel that fires
    // loadMoreHistoryRequested() when clicked.
    void setHistory(const std::vector<ghm::git::CommitInfo>& commits);

    // Fed by GitWorker::historyReady when isAppend=true. APPENDS to
    // the existing list, removing/re-adding the sentinel as
    // appropriate based on whether more pages remain.
    void appendHistory(const std::vector<ghm::git::CommitInfo>& commits);

    // Fed by GitWorker::signatureVerified. Stores the result in the
    // local cache and rewrites the affected commit row's text + tooltip.
    // Safe to call for commits no longer visible (just updates the
    // cache; rewrite is a no-op when the row isn't found).
    void setSignatureVerifyResult(const QString& sha,
                                   const ghm::git::VerifyResult& result);

    // Updates the branch picker contents. Called after worker fires
    // branchInfosReady; entries are already sorted (current first).
    void setBranches(const std::vector<ghm::git::BranchInfo>& branches);

    // Fed by GitWorker::commitDiffReady. Populates the file list in
    // the History tab and clears any previously-shown commit diff.
    void setCommitDiff(const QString& sha,
                       const std::vector<ghm::git::FileDiff>& files,
                       const QString& error);

    // Fed by GitWorker::fileDiffReady. The widget filters out late
    // results that don't match the currently-selected file.
    void setFileDiff(const QString& repoRelPath,
                     const ghm::git::FileDiff& diff,
                     const QString& error);

    // Fed by GitWorker::submodulesReady. Replaces the Submodules tab
    // content. Empty vector → tab shows "No submodules" empty state.
    void setSubmodules(const std::vector<ghm::git::SubmoduleInfo>& subs);

    // Fed by Settings on startup and after IdentityDialog accept.
    void setIdentity(const QString& name, const QString& email);

    // Disable destructive controls while an operation is in flight.
    void setBusy(bool busy);

    // Default branch name to prefill in the init prompt (from Settings).
    void setDefaultInitBranch(const QString& branch);

    // Show or hide the "you have N conflicts" banner. Pass 0 to hide.
    void setConflictCount(int count);

Q_SIGNALS:
    void initRequested        (const QString& path, const QString& initialBranch);
    void stageAllRequested    (const QString& path);
    void stagePathsRequested  (const QString& path, const QStringList& paths);
    void unstagePathsRequested(const QString& path, const QStringList& paths);
    void commitRequested      (const QString& path, const QString& message);
    void addRemoteRequested   (const QString& path);   // opens AddRemoteDialog in MainWindow
    void removeRemoteRequested(const QString& path, const QString& name);
    void pushLocalRequested   (const QString& path,
                               const QString& remoteName,
                               const QString& branch,
                               bool           setUpstream);
    void publishToGitHubRequested(const QString& path);
    void refreshRequested     (const QString& path);
    void historyRequested     (const QString& path);

    // Fired when the user clicks the "Load more older commits…"
    // sentinel at the bottom of the History list. The host should
    // call worker_->loadHistory(path, page, afterSha) and feed the
    // result back via appendHistory().
    void loadMoreHistoryRequested(const QString& path,
                                   const QString& afterSha);

    void editIdentityRequested();

    // Submodule signals — host wires them to the worker and re-runs
    // listSubmodules once the op completes (see setSubmodules()).
    void submodulesListRequested  (const QString& path);
    void submoduleInitRequested   (const QString& path, const QString& name);
    void submoduleUpdateRequested (const QString& path, const QString& name);
    void submoduleSyncRequested   (const QString& path, const QString& name);
    void submoduleAddRequested    (const QString& path);
    void submoduleRemoveRequested (const QString& path, const QString& name);

    // Explicit-key variants of init/update. Emitted when the user
    // chooses "Init with explicit key…" / "Update with explicit
    // key…" from the submodule context menu. Host pops SshKeyDialog
    // and forwards to the *WithCreds worker call. Used for
    // submodules that require a different key than what's loaded
    // in ssh-agent (e.g. deploy keys scoped to one specific repo).
    void submoduleInitWithExplicitKeyRequested  (const QString& path,
                                                   const QString& name);
    void submoduleUpdateWithExplicitKeyRequested(const QString& path,
                                                   const QString& name);

    // Emitted when user chooses "Forget remembered key" from the
    // submodule context menu. Host clears the persisted mapping.
    void submoduleForgetRememberedKeyRequested(const QString& path,
                                                  const QString& name);

    // Asks the host to run signature verification for the given
    // commit. Emitted from setHistory/appendHistory for commits that
    // have hasSignature=true. The host throttles dispatch and
    // forwards to the worker; results return via
    // setSignatureVerifyResult() above.
    void verifyCommitSignatureRequested(const QString& path,
                                         const QString& sha);

    // Asks the host (MainWindow) to compute a diff for `repoRelPath`
    // in the given scope. The widget itself is dumb about diffs —
    // displays whatever comes back via setFileDiff().
    void diffRequested(const QString& path,
                       const QString& repoRelPath,
                       ghm::git::DiffScope scope);

    // Fired when the user picks a commit in the History tab. The host
    // (MainWindow) responds with setCommitDiff() once the worker
    // returns.
    void commitDiffRequested(const QString& path, const QString& sha);

    // Fired when the user has two commits selected (Ctrl+click) and
    // wants to see the diff between them. Host runs an "a..b" diff.
    // Result also lands via setCommitDiff(), with the synthetic SHA
    // "shaA..shaB" so the widget can tell the two cases apart.
    void commitCompareRequested(const QString& path,
                                const QString& shaA,
                                const QString& shaB);

    // Branch operations. The host opens any required dialogs and calls
    // back into this widget via setBranches() once the worker reports.
    void branchSwitchRequested(const QString& path, const QString& branch);
    void branchCreateRequested(const QString& path);
    void branchDeleteRequested(const QString& path, const QString& branch);

    // Rename request — host opens an input dialog (so we don't pull
    // QtWidgets dependencies into this widget for the rename prompt
    // beyond what we already use). Result lands back via the worker's
    // refresh path.
    void branchRenameRequested(const QString& path, const QString& oldName);

    // User clicked the "Resolve conflicts" CTA in the in-progress
    // merge banner. Host opens ConflictResolutionDialog.
    void resolveConflictsRequested(const QString& path);

private Q_SLOTS:
    void onInitClicked();
    void onStageSelectedClicked();
    void onUnstageSelectedClicked();
    void onStageAllClicked();
    void onRefreshClicked();
    void onCommitClicked();
    void onChangesItemDoubleClicked(QListWidgetItem* item);
    void onChangesSelectionChanged();
    void onTabChanged(int index);
    void onHistoryRefreshClicked();
    void onHistorySelectionChanged();
    // Filter the visible history entries by user-typed text. Matches
    // anywhere in summary / short SHA / author name (case-insensitive).
    void onHistoryFilterChanged(const QString& filter);
    // Selecting a file in the commit-files list shows that file's
    // diff in the lower diff pane.
    void onCommitFileSelectionChanged();
    void onAddRemoteClicked();
    void onRemoveRemoteClicked();
    void onPushClicked();
    void onPublishToGitHubClicked();
    // Auto-refresh: fired by QFileSystemWatcher; debounced via timer.
    void onWatchedPathChanged();
    void onAutoRefreshTimeout();
    // Branch picker — opens the popup menu when the button is clicked.
    void onBranchButtonClicked();

    // Submodules
    void onSubmoduleInitClicked();
    void onSubmoduleUpdateClicked();
    void onSubmoduleSyncClicked();
    void onSubmoduleRemoveClicked();
    void onSubmoduleUpdateAllClicked();
    void onSubmoduleSelectionChanged();

    // Throttled callback fired from historyList_'s scrollbar
    // valueChanged and after setHistory/appendHistory. Starts a
    // 150ms one-shot timer (debounce) which then runs
    // dispatchVerifyForVisibleRows. Direct dispatch on every
    // scroll pixel would spam emit signals during fast scrolling.
    void onHistoryScrolled();

private:
    void buildUi();
    void buildHeader(QWidget* container);
    QWidget* buildNotRepoPage();
    QWidget* buildRepoPage();
    QWidget* buildChangesTab();
    QWidget* buildHistoryTab();
    QWidget* buildRemotesTab();
    QWidget* buildSubmodulesTab();

    QString  selectedRemote() const;
    QString  selectedSubmoduleName() const;
    QStringList selectedChangedPaths(bool stagedOnly, bool unstagedOnly) const;
    void rebuildChangesList(const std::vector<ghm::git::StatusEntry>& entries);
    void rebuildRemotesList(const std::vector<ghm::git::RemoteInfo>& remotes);
    void updateCommitButton();
    void updatePushPanel();
    void updateIdentityBar();

    // Re-points the QFileSystemWatcher at the new repo. Called from
    // setFolder() and from refresh callbacks where the .git directory
    // might have just been created (init flow).
    void setupWatcher();

    // Empties the watcher of all paths.
    void teardownWatcher();

    // Asks for a fresh diff for whatever the user has selected. Called
    // when the selection changes and after auto-refresh fires.
    void requestDiffForSelection();

    // History pagination helpers (see setHistory / appendHistory).
    void appendCommitRow(const ghm::git::CommitInfo& c);
    void appendLoadMoreSentinel();

    // Iterates historyList_, finds rows that:
    //   * intersect the current viewport rect
    //   * have hasSignature=true (data role + 6, set by appendCommitRow)
    //   * aren't already in sigCache_
    // Emits verifyCommitSignatureRequested for each. Called on
    // initial history load, after scroll (debounced via timer),
    // after filter changes, and after row mutations.
    //
    // O(N) over the history list. For N=2000 (large monorepo
    // history) it's still ~microseconds — the row visibility check
    // (visualItemRect intersect viewport) is cheap. We don't bother
    // with index structures.
    void dispatchVerifyForVisibleRows();

    // -- state --
    QString                                path_;
    QString                                branch_;
    QString                                identityName_;
    QString                                identityEmail_;
    QString                                defaultInitBranch_{QStringLiteral("master")};
    std::vector<ghm::git::RemoteInfo>      remotes_;
    bool                                   isRepository_{false};
    bool                                   busy_{false};
    bool                                   historyLoaded_{false};

    // Pagination state for the history list. oldestLoadedSha_ is the
    // SHA of the last row currently displayed — i.e. what we pass as
    // `afterSha` for the next "load more". isLoadingMore_ blocks
    // double-click on the sentinel while a request is in flight.
    QString                                oldestLoadedSha_;
    bool                                   isLoadingMore_{false};
    // Page size for both initial load and each "load more" batch.
    // 200 keeps the GUI snappy and matches the prior hard cap.
    static constexpr int                   kHistoryPageSize = 200;

    // -- header (always visible) --
    QLabel*    folderLabel_;
    QLabel*    pathLabel_;
    QToolButton* branchButton_;       // the branch picker
    QLabel*    identityLabel_;
    QToolButton* identityEditBtn_;

    // Cached branch list, populated by setBranches(). The popup menu
    // is built fresh from this on each open.
    std::vector<ghm::git::BranchInfo> branchInfos_;

    // -- pages --
    QStackedWidget* pages_;
    QWidget*        notRepoPage_;
    QWidget*        repoPage_;

    // -- not-repo controls --
    QLineEdit*   initBranchEdit_;
    QPushButton* initBtn_;

    // -- tabs --
    QTabWidget* tabs_;

    // -- Changes tab --
    QFrame*         publishBanner_;        // shown when no `origin` remote
    QLabel*         publishBannerLabel_;
    QPushButton*    publishBannerBtn_;

    // Shown when the repo is mid-merge / mid-rebase / etc and has
    // conflicted files. Clicking the CTA opens ConflictResolutionDialog
    // via the host. Hidden whenever isClean() or no conflicts.
    QFrame*         conflictBanner_{nullptr};
    QLabel*         conflictBannerLabel_{nullptr};
    QPushButton*    conflictBannerBtn_{nullptr};
    QSplitter*      changesSplitter_;      // top: file list, bottom: diff
    QListWidget*    changesList_;
    QPushButton*    stageSelectedBtn_;
    QPushButton*    unstageSelectedBtn_;
    QPushButton*    stageAllBtn_;
    QPushButton*    refreshBtn_;
    QPlainTextEdit* commitMessageEdit_;
    QPushButton*    commitBtn_;
    QLabel*         commitHintLabel_;
    DiffViewWidget* diffView_;
    QString         currentDiffPath_;       // currently-shown file's repo-rel path

    // -- History tab --
    QListWidget*    historyList_;
    QLineEdit*      commitFilter_;     // filter on commit summary / sha / author
    QLabel*         historyFilterCounter_{nullptr};  // "N of M" beside filter
    QPlainTextEdit* historyDetail_;
    QPushButton*    historyRefreshBtn_;
    // Files changed by the selected commit + per-file diff pane.
    QListWidget*    commitFilesList_;
    DiffViewWidget* commitDiffView_;
    QString         currentCommitSha_;          // commit being shown
    std::vector<ghm::git::FileDiff> currentCommitFiles_;

    // -- Remotes tab --
    QListWidget*    remotesList_;
    QPushButton*    publishRemoteBtn_;     // "Publish to GitHub…"
    QPushButton*    addRemoteBtn_;
    QPushButton*    removeRemoteBtn_;
    QComboBox*      pushRemoteCombo_;
    QLabel*         pushBranchLabel_;
    QCheckBox*      pushSetUpstreamBox_;
    QPushButton*    pushBtn_;

    // -- Submodules tab --
    QTableWidget*   submoduleTable_{nullptr};
    QLabel*         submoduleEmptyLabel_{nullptr};
    QPushButton*    submoduleAddBtn_{nullptr};
    QPushButton*    submoduleRefreshBtn_{nullptr};
    QPushButton*    submoduleUpdateAllBtn_{nullptr};
    QPushButton*    submoduleInitBtn_{nullptr};
    QPushButton*    submoduleUpdateBtn_{nullptr};
    QPushButton*    submoduleSyncBtn_{nullptr};
    QPushButton*    submoduleRemoveBtn_{nullptr};
    // Cache of the last setSubmodules() payload; lets Update-all
    // dispatch over the existing list without re-querying.
    std::vector<ghm::git::SubmoduleInfo> submodules_;

    // Signature verify cache. Key: full commit SHA. Populated by
    // setSignatureVerifyResult() as the worker streams results in.
    // Lookup is cheap because appendCommitRow consults it inline.
    // We keep it for the lifetime of the widget — verification of a
    // given SHA + key state is deterministic, so a re-render
    // (history reload) can re-use existing entries.
    QHash<QString, ghm::git::VerifyResult> sigCache_;

    // -- Auto-refresh infrastructure ----------------------------------
    // Watches .git/HEAD, .git/index, and the working-tree root for
    // external changes (file edits, CLI git operations, etc.). Hits
    // are debounced through autoRefreshTimer_ so a `git add .` from
    // the terminal doesn't trigger N refreshes back-to-back.
    QFileSystemWatcher* watcher_;
    QTimer*             autoRefreshTimer_;

    // Read-only Settings handle injected by MainWindow via
    // setSettings(). May be nullptr in test/embedded scenarios —
    // every consumer guards with `if (settings_)`.
    ghm::core::Settings* settings_{nullptr};

    // Debounce timer for viewport-only signature verify dispatch.
    // Scrollbar valueChanged fires on every pixel; we coalesce
    // bursts into a single dispatch after the user stops scrolling
    // (150ms quiet period). Single-shot, restarted on each
    // scrollbar change.
    QTimer*             verifyDebounce_{nullptr};
};

} // namespace ghm::ui
