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

class QLabel;
class QProgressBar;
class QSplitter;
class QStackedWidget;
class QAction;

namespace ghm::core   { class SecureStorage; class Settings; }
namespace ghm::github { class GitHubClient; }
namespace ghm::git    { class GitWorker; }

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
    // -- Auth / lifecycle -----------------------------------------------
    void promptLogin();
    void onLoggedIn(const QString& username, const QString& token);
    void logout();

    // -- GitHub client --------------------------------------------------
    void onAuthenticated(const QString& login);
    void onAuthFailed(const QString& reason);
    void onRepositoriesReady(const QList<ghm::github::Repository>& repos);
    void onNetworkError(const QString& msg);
    void refreshRepositories();

    // -- GitHub-clone flow (existing) -----------------------------------
    void onCloneRequested(const ghm::github::Repository& repo);
    void onOpenLocallyRequested(const ghm::github::Repository& repo);
    void onPullRequested(const QString& localPath);
    void onPushRequested(const QString& localPath);
    void onRefreshRequested(const QString& localPath);
    void onSwitchBranchRequested(const QString& localPath, const QString& branch);

    void onCloneFinished(bool ok, const QString& localPath, const QString& error);
    void onPullFinished (bool ok, const QString& localPath, const QString& error);
    void onPushFinished (bool ok, const QString& localPath, const QString& error);
    void onStatusReady  (const QString& localPath,
                         const QString& branch,
                         const ghm::git::StatusSummary& s);
    void onBranchSwitched(bool ok, const QString& localPath,
                          const QString& branch, const QString& error);
    void onBranchesReady(const QString& localPath, const QStringList& branches);
    void onWorkerProgress(const QString& phase, qint64 cur, qint64 tot);

    // -- Local folder workflow ------------------------------------------
    void onAddLocalFolderClicked();
    void onLocalFolderActivated(const QString& path);
    void onRemoveLocalFolderRequested(const QString& path);

    void onLocalInitRequested      (const QString& path, const QString& branch);
    void onLocalStageAllRequested  (const QString& path);
    void onLocalStagePathsRequested(const QString& path, const QStringList& paths);
    void onLocalUnstagePathsRequested(const QString& path, const QStringList& paths);
    void onLocalCommitRequested    (const QString& path, const QString& message);
    void onLocalAddRemoteRequested (const QString& path);
    void onLocalRemoveRemoteRequested(const QString& path, const QString& name);
    void onLocalPushRequested      (const QString& path,
                                    const QString& remoteName,
                                    const QString& branch,
                                    bool           setUpstream);
    void onLocalRefreshRequested   (const QString& path);
    void onLocalHistoryRequested   (const QString& path);
    void onLocalDiffRequested      (const QString& path,
                                    const QString& repoRelPath,
                                    ghm::git::DiffScope scope);
    void onLocalCommitDiffRequested(const QString& path, const QString& sha);
    // Branch management on a local repo.
    void onLocalBranchSwitchRequested(const QString& path, const QString& branch);
    void onLocalBranchCreateRequested(const QString& path);
    void onLocalBranchDeleteRequested(const QString& path, const QString& branch);
    void onPublishToGitHubRequested(const QString& path);
    void onRepositoryCreated       (const ghm::github::Repository& repo);
    void onEditIdentityRequested();

    // -- App-level UI ---------------------------------------------------
    void onLanguageChosen(const QString& code);
    void onShowSupportDialog();

    // Worker callbacks for local flow.
    void onInitFinished(bool ok, const QString& path, const QString& error);
    void onLocalStateReady(const QString& path,
                           bool                                       isRepository,
                           const QString&                             branch,
                           const std::vector<ghm::git::StatusEntry>&  entries,
                           const std::vector<ghm::git::RemoteInfo>&   remotes);
    void onStageFinished  (bool ok, const QString& path, const QString& error);
    void onUnstageFinished(bool ok, const QString& path, const QString& error);
    void onCommitFinished (bool ok, const QString& path,
                           const QString& sha, const QString& error);
    void onHistoryReady   (const QString& path,
                           const std::vector<ghm::git::CommitInfo>& commits);
    void onFileDiffReady  (const QString& path,
                           const QString& repoRelPath,
                           const ghm::git::FileDiff& diff,
                           const QString& error);
    void onCommitDiffReady(const QString& path,
                           const QString& sha,
                           const std::vector<ghm::git::FileDiff>& files,
                           const QString& error);
    void onBranchInfosReady(const QString& path,
                            const std::vector<ghm::git::BranchInfo>& branches);
    void onBranchCreated  (bool ok, const QString& path,
                           const QString& name, const QString& error);
    void onBranchDeleted  (bool ok, const QString& path,
                           const QString& name, const QString& error);
    void onRemoteOpFinished(bool ok, const QString& path, const QString& error);

private:
    void buildUi();
    void buildActions();
    void wireSignals();
    void setStatus(const QString& text, int timeoutMs = 0);
    void setBusy(bool busy, const QString& label = {});
    void rememberLocalPath(const QString& fullName, const QString& localPath);
    bool ensureIdentity();   // prompts via IdentityDialog if needed
    void pushIdentityToWidget();

    // -- collaborators --
    ghm::core::SecureStorage*  storage_;
    ghm::core::Settings*       settings_;
    ghm::github::GitHubClient* client_;
    ghm::git::GitWorker*       worker_;

    // -- session state --
    QString username_;
    QString token_;
    QHash<QString, QString> localPathByFullName_;  // GitHub clone tracking
    QString activeLocalPath_;                       // currently-shown local folder
    QList<ghm::github::Repository> reposCache_;     // last-fetched GitHub repos

    // Tracks an in-flight "Publish to GitHub" sequence:
    //   1. (optional) POST /user/repos
    //   2. worker addRemote(origin, cloneUrl)
    //   3. (optional) worker pushTo(origin, branch, setUpstream=true)
    // Each step's completion checks pendingPublishPath_ to decide
    // whether the result belongs to a publish flow.
    struct PendingPublish {
        QString path;          // empty == none in flight
        QString cloneUrl;
        bool    pushAfter{false};
    };
    PendingPublish pendingPublish_;

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
};

} // namespace ghm::ui
