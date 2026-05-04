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

namespace ghm::ui {

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

private Q_SLOTS:
    void onInitClicked();
    void onStageSelectedClicked();
    void onUnstageSelectedClicked();
    void onStageAllClicked();
    void onRefreshClicked();
    void onCommitClicked();
    void onChangesItemDoubleClicked(QListWidgetItem* item);
    void onTabChanged(int index);
    void onHistoryRefreshClicked();
    void onHistorySelectionChanged();
    void onAddRemoteClicked();
    void onRemoveRemoteClicked();
    void onPushClicked();
    void onPublishToGitHubClicked();

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
    QLabel*    branchLabel_;
    QLabel*    identityLabel_;
    QToolButton* identityEditBtn_;

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
    QListWidget*    changesList_;
    QPushButton*    stageSelectedBtn_;
    QPushButton*    unstageSelectedBtn_;
    QPushButton*    stageAllBtn_;
    QPushButton*    refreshBtn_;
    QPlainTextEdit* commitMessageEdit_;
    QPushButton*    commitBtn_;
    QLabel*         commitHintLabel_;

    // -- History tab --
    QListWidget*    historyList_;
    QPlainTextEdit* historyDetail_;
    QPushButton*    historyRefreshBtn_;

    // -- Remotes tab --
    QListWidget*    remotesList_;
    QPushButton*    publishRemoteBtn_;     // "Publish to GitHub…"
    QPushButton*    addRemoteBtn_;
    QPushButton*    removeRemoteBtn_;
    QComboBox*      pushRemoteCombo_;
    QLabel*         pushBranchLabel_;
    QCheckBox*      pushSetUpstreamBox_;
    QPushButton*    pushBtn_;
};

} // namespace ghm::ui
