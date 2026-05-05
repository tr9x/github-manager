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
// All git work goes through MainWindow via Q_SIGNALS; this widget is
// pure UI and never touches GitHandler or Settings directly.

#include <QWidget>
#include <QString>
#include <QStringList>
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
class QComboBox;
class QCheckBox;
class QFrame;
class QSplitter;
class QFileSystemWatcher;
class QTimer;

namespace ghm::ui {

class DiffViewWidget;

class LocalRepositoryWidget : public QWidget {
    Q_OBJECT
public:
    explicit LocalRepositoryWidget(QWidget* parent = nullptr);
    ~LocalRepositoryWidget() override;

    // Switch the panel to a different folder and clear stale state.
    // The MainWindow follows this with a refreshLocalState() request.
    void setFolder(const QString& path);
    QString currentPath() const { return path_; }

    // Snapshot from GitWorker::localStateReady.
    void setLocalState(bool                                       isRepository,
                       const QString&                             branch,
                       const std::vector<ghm::git::StatusEntry>&  entries,
                       const std::vector<ghm::git::RemoteInfo>&   remotes);

    // Fed by GitWorker::historyReady.
    void setHistory(const std::vector<ghm::git::CommitInfo>& commits);

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

    // Fed by Settings on startup and after IdentityDialog accept.
    void setIdentity(const QString& name, const QString& email);

    // Disable destructive controls while an operation is in flight.
    void setBusy(bool busy);

    // Default branch name to prefill in the init prompt (from Settings).
    void setDefaultInitBranch(const QString& branch);

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
    void editIdentityRequested();

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

    // Branch operations. The host opens any required dialogs and calls
    // back into this widget via setBranches() once the worker reports.
    void branchSwitchRequested(const QString& path, const QString& branch);
    void branchCreateRequested(const QString& path);
    void branchDeleteRequested(const QString& path, const QString& branch);

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

private:
    void buildUi();
    void buildHeader(QWidget* container);
    QWidget* buildNotRepoPage();
    QWidget* buildRepoPage();
    QWidget* buildChangesTab();
    QWidget* buildHistoryTab();
    QWidget* buildRemotesTab();

    QString  selectedRemote() const;
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

    // -- Auto-refresh infrastructure ----------------------------------
    // Watches .git/HEAD, .git/index, and the working-tree root for
    // external changes (file edits, CLI git operations, etc.). Hits
    // are debounced through autoRefreshTimer_ so a `git add .` from
    // the terminal doesn't trigger N refreshes back-to-back.
    QFileSystemWatcher* watcher_;
    QTimer*             autoRefreshTimer_;
};

} // namespace ghm::ui
