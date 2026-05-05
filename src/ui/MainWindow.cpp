#include "ui/MainWindow.h"

#include "ui/LoginDialog.h"
#include "ui/CloneDialog.h"
#include "ui/IdentityDialog.h"
#include "ui/AddRemoteDialog.h"
#include "ui/PublishToGitHubDialog.h"
#include "ui/SupportDialog.h"
#include "ui/CreateBranchDialog.h"
#include "ui/RepositoryListWidget.h"
#include "ui/RepositoryDetailWidget.h"
#include "ui/LocalRepositoryWidget.h"

#include "core/SecureStorage.h"
#include "core/Settings.h"
#include "github/GitHubClient.h"
#include "git/GitWorker.h"

#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QMenuBar>
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QProgressBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QCloseEvent>
#include <QTimer>

namespace ghm::ui {

namespace {
constexpr const char* kAppTitle = "GitHub Manager";
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , storage_  (new ghm::core::SecureStorage())
    , settings_ (new ghm::core::Settings())
    , client_   (new ghm::github::GitHubClient(this))
    , worker_   (new ghm::git::GitWorker(this))
    , splitter_(nullptr)
    , repoList_(nullptr)
    , detailStack_(nullptr)
    , repoDetail_(nullptr)
    , localDetail_(nullptr)
    , userLabel_(nullptr)
    , statusMessage_(nullptr)
    , progress_(nullptr)
    , refreshAction_(nullptr)
    , logoutAction_(nullptr)
    , quitAction_(nullptr)
    , addLocalAction_(nullptr)
{
    qRegisterMetaType<ghm::github::Repository>("ghm::github::Repository");
    qRegisterMetaType<ghm::git::StatusSummary>("ghm::git::StatusSummary");

    setWindowTitle(QString::fromLatin1(kAppTitle));
    resize(1200, 760);

    buildUi();
    buildActions();
    wireSignals();

    // Restore window geometry / state.
    if (auto g = settings_->mainWindowGeometry(); !g.isEmpty()) restoreGeometry(g);
    if (auto s = settings_->mainWindowState();    !s.isEmpty()) restoreState(s);

    // Populate initial state from settings.
    repoList_->setLocalFolders(settings_->localFolders());
    localDetail_->setDefaultInitBranch(settings_->defaultInitBranch());
    pushIdentityToWidget();
}

MainWindow::~MainWindow()
{
    delete storage_;
    delete settings_;
}

// ---------------------------------------------------------------------------

void MainWindow::buildUi()
{
    splitter_ = new QSplitter(Qt::Horizontal, this);
    repoList_    = new RepositoryListWidget(splitter_);
    detailStack_ = new QStackedWidget(splitter_);
    repoDetail_  = new RepositoryDetailWidget(detailStack_);
    localDetail_ = new LocalRepositoryWidget(detailStack_);
    detailStack_->addWidget(repoDetail_);
    detailStack_->addWidget(localDetail_);

    splitter_->addWidget(repoList_);
    splitter_->addWidget(detailStack_);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 3);
    setCentralWidget(splitter_);

    statusMessage_ = new QLabel(this);
    progress_      = new QProgressBar(this);
    progress_->setVisible(false);
    progress_->setMaximumWidth(220);
    progress_->setRange(0, 0);
    userLabel_     = new QLabel(this);
    userLabel_->setStyleSheet(QStringLiteral("color: #bcbcbc; padding-right: 8px;"));

    statusBar()->addWidget(statusMessage_, 1);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(userLabel_);
}

void MainWindow::buildActions()
{
    auto* tb = addToolBar(tr("Main"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setMovable(false);

    refreshAction_ = tb->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                                   tr("Refresh"));
    refreshAction_->setShortcut(QKeySequence::Refresh);
    refreshAction_->setEnabled(false);

    addLocalAction_ = tb->addAction(QIcon::fromTheme(QStringLiteral("folder-new")),
                                    tr("Add Local Folder…"));
    addLocalAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+L")));

    tb->addSeparator();

    logoutAction_ = tb->addAction(QIcon::fromTheme(QStringLiteral("system-log-out")),
                                  tr("Sign out"));
    logoutAction_->setEnabled(false);

    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(addLocalAction_);
    fileMenu->addSeparator();
    quitAction_ = fileMenu->addAction(tr("&Quit"));
    quitAction_->setShortcut(QKeySequence::Quit);

    auto* accountMenu = menuBar()->addMenu(tr("&Account"));
    accountMenu->addAction(refreshAction_);
    accountMenu->addAction(logoutAction_);

    // Settings → Language. Languages are exclusive radio actions whose
    // checkmark reflects the currently-installed translation. Picking a
    // different one persists the choice and prompts for a restart.
    auto* settingsMenu = menuBar()->addMenu(tr("&Settings"));
    auto* langMenu     = settingsMenu->addMenu(tr("&Language"));
    auto* langGroup    = new QActionGroup(this);
    langGroup->setExclusive(true);
    const QString currentLang = settings_->language();
    for (const QString& code : ghm::core::Settings::supportedLanguages()) {
        auto* act = langMenu->addAction(
            ghm::core::Settings::languageDisplayName(code));
        act->setCheckable(true);
        act->setChecked(code == currentLang);
        act->setData(code);
        langGroup->addAction(act);
        connect(act, &QAction::triggered, this,
                [this, code] { onLanguageChosen(code); });
    }

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    auto* aboutAct = helpMenu->addAction(tr("&About"));
    connect(aboutAct, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About GitHub Manager"),
            tr("<h3>GitHub Manager %1</h3>"
               "<p>A native Linux client for managing GitHub repositories "
               "and local Git folders.</p>"
               "<p>Built with Qt 6, libgit2 and libsecret.</p>")
                .arg(QString::fromLatin1(GHM_VERSION)));
    });
    helpMenu->addSeparator();
    auto* supportAct = helpMenu->addAction(tr("&Support / Donate…"));
    connect(supportAct, &QAction::triggered,
            this, &MainWindow::onShowSupportDialog);
}

void MainWindow::wireSignals()
{
    connect(refreshAction_,  &QAction::triggered, this, &MainWindow::refreshRepositories);
    connect(logoutAction_,   &QAction::triggered, this, &MainWindow::logout);
    connect(quitAction_,     &QAction::triggered, this, &QMainWindow::close);
    connect(addLocalAction_, &QAction::triggered, this, &MainWindow::onAddLocalFolderClicked);

    // GitHub client.
    connect(client_, &ghm::github::GitHubClient::authenticated,
            this, &MainWindow::onAuthenticated);
    connect(client_, &ghm::github::GitHubClient::authenticationFailed,
            this, &MainWindow::onAuthFailed);
    connect(client_, &ghm::github::GitHubClient::repositoriesReady,
            this, &MainWindow::onRepositoriesReady);
    connect(client_, &ghm::github::GitHubClient::repositoryCreated,
            this, &MainWindow::onRepositoryCreated);
    connect(client_, &ghm::github::GitHubClient::networkError,
            this, &MainWindow::onNetworkError);

    // Sidebar.
    connect(repoList_, &RepositoryListWidget::repositoryActivated, this,
            [this](const ghm::github::Repository& repo) {
                detailStack_->setCurrentWidget(repoDetail_);
                repoDetail_->showRepository(repo);
                if (!repo.localPath.isEmpty()) {
                    worker_->listBranches(repo.localPath);
                }
            });
    connect(repoList_, &RepositoryListWidget::localFolderActivated,
            this, &MainWindow::onLocalFolderActivated);
    connect(repoList_, &RepositoryListWidget::addLocalFolderClicked,
            this, &MainWindow::onAddLocalFolderClicked);
    connect(repoList_, &RepositoryListWidget::removeLocalFolderRequested,
            this, &MainWindow::onRemoveLocalFolderRequested);

    // Detail panel (GitHub clones).
    connect(repoDetail_, &RepositoryDetailWidget::cloneRequested,
            this, &MainWindow::onCloneRequested);
    connect(repoDetail_, &RepositoryDetailWidget::openLocallyRequested,
            this, &MainWindow::onOpenLocallyRequested);
    connect(repoDetail_, &RepositoryDetailWidget::pullRequested,
            this, &MainWindow::onPullRequested);
    connect(repoDetail_, &RepositoryDetailWidget::pushRequested,
            this, &MainWindow::onPushRequested);
    connect(repoDetail_, &RepositoryDetailWidget::refreshRequested,
            this, &MainWindow::onRefreshRequested);
    connect(repoDetail_, &RepositoryDetailWidget::switchBranchRequested,
            this, &MainWindow::onSwitchBranchRequested);

    // Detail panel (local folders).
    connect(localDetail_, &LocalRepositoryWidget::initRequested,
            this, &MainWindow::onLocalInitRequested);
    connect(localDetail_, &LocalRepositoryWidget::stageAllRequested,
            this, &MainWindow::onLocalStageAllRequested);
    connect(localDetail_, &LocalRepositoryWidget::stagePathsRequested,
            this, &MainWindow::onLocalStagePathsRequested);
    connect(localDetail_, &LocalRepositoryWidget::unstagePathsRequested,
            this, &MainWindow::onLocalUnstagePathsRequested);
    connect(localDetail_, &LocalRepositoryWidget::commitRequested,
            this, &MainWindow::onLocalCommitRequested);
    connect(localDetail_, &LocalRepositoryWidget::addRemoteRequested,
            this, &MainWindow::onLocalAddRemoteRequested);
    connect(localDetail_, &LocalRepositoryWidget::removeRemoteRequested,
            this, &MainWindow::onLocalRemoveRemoteRequested);
    connect(localDetail_, &LocalRepositoryWidget::pushLocalRequested,
            this, &MainWindow::onLocalPushRequested);
    connect(localDetail_, &LocalRepositoryWidget::publishToGitHubRequested,
            this, &MainWindow::onPublishToGitHubRequested);
    connect(localDetail_, &LocalRepositoryWidget::refreshRequested,
            this, &MainWindow::onLocalRefreshRequested);
    connect(localDetail_, &LocalRepositoryWidget::historyRequested,
            this, &MainWindow::onLocalHistoryRequested);
    connect(localDetail_, &LocalRepositoryWidget::diffRequested,
            this, &MainWindow::onLocalDiffRequested);
    connect(localDetail_, &LocalRepositoryWidget::commitDiffRequested,
            this, &MainWindow::onLocalCommitDiffRequested);
    connect(localDetail_, &LocalRepositoryWidget::branchSwitchRequested,
            this, &MainWindow::onLocalBranchSwitchRequested);
    connect(localDetail_, &LocalRepositoryWidget::branchCreateRequested,
            this, &MainWindow::onLocalBranchCreateRequested);
    connect(localDetail_, &LocalRepositoryWidget::branchDeleteRequested,
            this, &MainWindow::onLocalBranchDeleteRequested);
    connect(localDetail_, &LocalRepositoryWidget::editIdentityRequested,
            this, &MainWindow::onEditIdentityRequested);

    // Worker - GitHub-clone flow.
    connect(worker_, &ghm::git::GitWorker::cloneFinished,
            this, &MainWindow::onCloneFinished);
    connect(worker_, &ghm::git::GitWorker::pullFinished,
            this, &MainWindow::onPullFinished);
    connect(worker_, &ghm::git::GitWorker::pushFinished,
            this, &MainWindow::onPushFinished);
    connect(worker_, &ghm::git::GitWorker::statusReady,
            this, &MainWindow::onStatusReady);
    connect(worker_, &ghm::git::GitWorker::branchSwitched,
            this, &MainWindow::onBranchSwitched);
    connect(worker_, &ghm::git::GitWorker::branchesReady,
            this, &MainWindow::onBranchesReady);
    connect(worker_, &ghm::git::GitWorker::progress,
            this, &MainWindow::onWorkerProgress);

    // Worker - local-folder flow.
    connect(worker_, &ghm::git::GitWorker::initFinished,
            this, &MainWindow::onInitFinished);
    connect(worker_, &ghm::git::GitWorker::localStateReady,
            this, &MainWindow::onLocalStateReady);
    connect(worker_, &ghm::git::GitWorker::stageFinished,
            this, &MainWindow::onStageFinished);
    connect(worker_, &ghm::git::GitWorker::unstageFinished,
            this, &MainWindow::onUnstageFinished);
    connect(worker_, &ghm::git::GitWorker::commitFinished,
            this, &MainWindow::onCommitFinished);
    connect(worker_, &ghm::git::GitWorker::historyReady,
            this, &MainWindow::onHistoryReady);
    connect(worker_, &ghm::git::GitWorker::fileDiffReady,
            this, &MainWindow::onFileDiffReady);
    connect(worker_, &ghm::git::GitWorker::commitDiffReady,
            this, &MainWindow::onCommitDiffReady);
    connect(worker_, &ghm::git::GitWorker::branchInfosReady,
            this, &MainWindow::onBranchInfosReady);
    connect(worker_, &ghm::git::GitWorker::branchCreated,
            this, &MainWindow::onBranchCreated);
    connect(worker_, &ghm::git::GitWorker::branchDeleted,
            this, &MainWindow::onBranchDeleted);
    connect(worker_, &ghm::git::GitWorker::remoteOpFinished,
            this, &MainWindow::onRemoteOpFinished);
}

// ---------------------------------------------------------------------------

void MainWindow::showEvent(QShowEvent* e)
{
    QMainWindow::showEvent(e);
    static bool didInitialPrompt = false;
    if (didInitialPrompt) return;
    didInitialPrompt = true;

    QTimer::singleShot(0, this, [this] {
        const QString u = settings_->lastUsername();
        if (!u.isEmpty()) {
            QString err;
            if (auto t = storage_->loadToken(u, &err); t) {
                username_ = u;
                token_    = *t;
                client_->setToken(token_);
                setStatus(tr("Validating saved credentials…"));
                client_->validateToken();
                return;
            } else if (!err.isEmpty()) {
                setStatus(tr("Keyring unavailable: %1").arg(err), 6000);
            }
        }
        promptLogin();
    });
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    settings_->setMainWindowGeometry(saveGeometry());
    settings_->setMainWindowState(saveState());
    QMainWindow::closeEvent(e);
}

// ----- Helpers -------------------------------------------------------------

void MainWindow::setStatus(const QString& text, int timeoutMs)
{
    if (timeoutMs > 0) statusBar()->showMessage(text, timeoutMs);
    else               statusMessage_->setText(text);
}

void MainWindow::setBusy(bool busy, const QString& label)
{
    progress_->setVisible(busy);
    if (busy && !label.isEmpty()) setStatus(label);
}

void MainWindow::rememberLocalPath(const QString& fullName, const QString& localPath)
{
    localPathByFullName_[fullName] = localPath;
    repoList_->setLocalPath(fullName, localPath);
}

bool MainWindow::ensureIdentity()
{
    if (settings_->hasIdentity()) return true;

    IdentityDialog dlg(settings_->authorName(), settings_->authorEmail(), this);
    dlg.setWindowTitle(tr("Set git author identity"));
    if (dlg.exec() != QDialog::Accepted) return false;

    settings_->setAuthorName (dlg.name());
    settings_->setAuthorEmail(dlg.email());
    pushIdentityToWidget();
    return true;
}

void MainWindow::pushIdentityToWidget()
{
    localDetail_->setIdentity(settings_->authorName(), settings_->authorEmail());
}

// ----- Auth lifecycle ------------------------------------------------------

void MainWindow::promptLogin()
{
    LoginDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        onLoggedIn(dlg.verifiedUsername(), dlg.token());
    } else {
        if (token_.isEmpty()) close();
    }
}

void MainWindow::onLoggedIn(const QString& username, const QString& token)
{
    username_ = username;
    token_    = token;
    client_->setToken(token_);
    settings_->setLastUsername(username_);

    if (auto r = storage_->saveToken(username_, token_); !r.ok) {
        QMessageBox::warning(this, tr("Could not store credential"),
            tr("Your token could not be saved to the system keyring:\n\n%1\n\n"
               "You'll be asked to sign in again next time.").arg(r.error));
    }
    onAuthenticated(username_);
}

void MainWindow::logout()
{
    if (QMessageBox::question(this, tr("Sign out"),
            tr("Sign out of %1? Your local clones and folders will be left intact.")
                .arg(username_)) != QMessageBox::Yes) {
        return;
    }
    storage_->clearToken(username_);
    token_.clear();
    username_.clear();
    repoList_->setRepositories({});
    repoDetail_->showRepository({});
    userLabel_->clear();
    refreshAction_->setEnabled(false);
    logoutAction_->setEnabled(false);
    setStatus(tr("Signed out."), 4000);
    promptLogin();
}

void MainWindow::onAuthenticated(const QString& login)
{
    username_ = login;
    settings_->setLastUsername(login);
    setWindowTitle(QStringLiteral("%1 — %2").arg(QString::fromLatin1(kAppTitle), login));
    userLabel_->setText(tr("Signed in as %1").arg(login));
    refreshAction_->setEnabled(true);
    logoutAction_->setEnabled(true);
    refreshRepositories();
}

void MainWindow::onAuthFailed(const QString& reason)
{
    QMessageBox::warning(this, tr("Authentication failed"),
        tr("GitHub rejected the stored token:\n\n%1\n\nPlease sign in again.")
            .arg(reason));
    if (!username_.isEmpty()) storage_->clearToken(username_);
    token_.clear();
    promptLogin();
}

// ----- GitHub repo listing -------------------------------------------------

void MainWindow::refreshRepositories()
{
    if (!client_->hasToken()) return;
    setBusy(true, tr("Loading repositories…"));
    client_->fetchRepositories();
}

void MainWindow::onRepositoriesReady(const QList<ghm::github::Repository>& repos)
{
    QList<ghm::github::Repository> annotated = repos;
    for (auto& r : annotated) {
        const auto it = localPathByFullName_.constFind(r.fullName);
        if (it != localPathByFullName_.cend()) r.localPath = it.value();
    }
    reposCache_ = annotated;
    repoList_->setRepositories(annotated);
    setBusy(false);
    setStatus(tr("Loaded %1 repositories.").arg(annotated.size()), 4000);
}

void MainWindow::onNetworkError(const QString& msg)
{
    setBusy(false);

    // If the failure interrupted a Publish-to-GitHub flow (e.g. POST
    // /user/repos returned 422 "name already exists"), unwind the
    // pending state so the next attempt starts clean.
    if (!pendingPublish_.path.isEmpty()) {
        const QString path = pendingPublish_.path;
        pendingPublish_ = {};
        localDetail_->setBusy(false);
        QMessageBox::warning(this, tr("Publish failed"), msg);
        if (activeLocalPath_ == path) worker_->refreshLocalState(path);
        return;
    }
    QMessageBox::warning(this, tr("Network error"), msg);
}

// ----- GitHub-clone flow ---------------------------------------------------

void MainWindow::onCloneRequested(const ghm::github::Repository& repo)
{
    if (!repo.isValid()) return;
    CloneDialog dlg(repo, settings_->defaultCloneDirectory(), this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString target = dlg.targetPath();
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(this, tr("Cannot clone"),
            tr("'%1' already exists. Choose a different folder.").arg(target));
        return;
    }
    settings_->setDefaultCloneDirectory(QFileInfo(target).absolutePath());
    setBusy(true, tr("Cloning %1…").arg(repo.fullName));
    worker_->clone(repo.cloneUrl, target, token_);
    localPathByFullName_.insert(repo.fullName, target);
}

void MainWindow::onOpenLocallyRequested(const ghm::github::Repository& repo)
{
    if (!repo.isValid()) return;
    const QString picked = QFileDialog::getExistingDirectory(this,
        tr("Choose existing local clone of %1").arg(repo.fullName),
        settings_->defaultCloneDirectory());
    if (picked.isEmpty()) return;

    if (!QFileInfo(QDir(picked).filePath(QStringLiteral(".git"))).exists()) {
        QMessageBox::warning(this, tr("Not a git repository"),
            tr("'%1' does not contain a .git directory.").arg(picked));
        return;
    }
    rememberLocalPath(repo.fullName, picked);

    auto updated = repo;
    updated.localPath = picked;
    repoDetail_->showRepository(updated);
    worker_->listBranches(picked);
}

void MainWindow::onPullRequested(const QString& localPath)
{
    if (localPath.isEmpty()) return;
    setBusy(true, tr("Pulling %1…").arg(QFileInfo(localPath).fileName()));
    worker_->pull(localPath, token_);
}

void MainWindow::onPushRequested(const QString& localPath)
{
    if (localPath.isEmpty()) return;
    setBusy(true, tr("Pushing %1…").arg(QFileInfo(localPath).fileName()));
    worker_->push(localPath, token_);
}

void MainWindow::onRefreshRequested(const QString& localPath)
{
    if (localPath.isEmpty()) return;
    worker_->refreshStatus(localPath);
    worker_->listBranches(localPath);
}

void MainWindow::onSwitchBranchRequested(const QString& localPath, const QString& branch)
{
    if (localPath.isEmpty() || branch.isEmpty()) return;
    setBusy(true, tr("Switching to %1…").arg(branch));
    worker_->switchBranch(localPath, branch);
}

// ----- Worker callbacks (GitHub-clone) -------------------------------------

void MainWindow::onCloneFinished(bool ok, const QString& localPath, const QString& error)
{
    setBusy(false);
    if (!ok) {
        for (auto it = localPathByFullName_.begin(); it != localPathByFullName_.end(); ) {
            if (it.value() == localPath) it = localPathByFullName_.erase(it);
            else ++it;
        }
        QMessageBox::critical(this, tr("Clone failed"), error);
        return;
    }
    setStatus(tr("Cloned to %1").arg(localPath), 5000);

    QString fullName;
    for (auto it = localPathByFullName_.cbegin(); it != localPathByFullName_.cend(); ++it) {
        if (it.value() == localPath) { fullName = it.key(); break; }
    }
    if (!fullName.isEmpty()) repoList_->setLocalPath(fullName, localPath);

    auto current = repoList_->currentRepository();
    if (current.fullName == fullName) {
        current.localPath = localPath;
        repoDetail_->showRepository(current);
        worker_->listBranches(localPath);
    }
}

void MainWindow::onPullFinished(bool ok, const QString& localPath, const QString& error)
{
    setBusy(false);
    if (!ok) QMessageBox::warning(this, tr("Pull failed"), error);
    else     setStatus(tr("Pull complete."), 4000);

    // Refresh whichever view is showing this path.
    if (activeLocalPath_ == localPath) {
        worker_->refreshLocalState(localPath);
    } else {
        worker_->refreshStatus(localPath);
    }
}

void MainWindow::onPushFinished(bool ok, const QString& localPath, const QString& error)
{
    setBusy(false);
    localDetail_->setBusy(false);

    // Was this push the final step of a Publish-to-GitHub flow?
    const bool wasPublishPush = !pendingPublish_.path.isEmpty()
                             &&  pendingPublish_.path == localPath;
    if (wasPublishPush) pendingPublish_ = {};

    if (!ok) {
        QMessageBox::warning(this,
            wasPublishPush ? tr("Publish failed at push") : tr("Push failed"),
            wasPublishPush
                ? tr("The remote is connected, but pushing your commits failed:\n\n%1\n\n"
                     "You can retry from the Remotes tab.").arg(error)
                : error);
    } else {
        setStatus(wasPublishPush ? tr("Published to GitHub.") : tr("Push complete."), 5000);
    }

    if (activeLocalPath_ == localPath) {
        worker_->refreshLocalState(localPath);
        worker_->loadHistory(localPath, /*maxCount*/ 200);
    } else {
        worker_->refreshStatus(localPath);
    }
}

void MainWindow::onStatusReady(const QString& localPath,
                               const QString& branch,
                               const ghm::git::StatusSummary& s)
{
    Q_UNUSED(localPath);
    repoDetail_->updateStatus(branch, s);
}

void MainWindow::onBranchSwitched(bool ok, const QString& localPath,
                                  const QString& branch, const QString& error)
{
    setBusy(false);
    localDetail_->setBusy(false);
    if (!ok) QMessageBox::warning(this, tr("Branch switch failed"), error);
    else     setStatus(tr("Now on %1").arg(branch), 4000);

    if (activeLocalPath_ == localPath) {
        worker_->refreshLocalState(localPath);
        worker_->listBranchInfos(localPath);
    } else {
        // GitHub-clone view: reuse the old per-status-summary path.
        worker_->refreshStatus(localPath);
        worker_->listBranches(localPath);
    }
}

void MainWindow::onBranchesReady(const QString& localPath,
                                 const QStringList& branches)
{
    Q_UNUSED(localPath);
    QString currentBranch;
    if (auto current = repoList_->currentRepository(); current.isValid()) {
        currentBranch = current.defaultBranch;
    }
    repoDetail_->setBranches(branches, currentBranch);
}

void MainWindow::onWorkerProgress(const QString& phase, qint64 cur, qint64 tot)
{
    if (tot > 0) {
        progress_->setRange(0, static_cast<int>(tot));
        progress_->setValue(static_cast<int>(cur));
    } else {
        progress_->setRange(0, 0);
    }
    progress_->setVisible(true);
    setStatus(QStringLiteral("%1 (%2/%3)").arg(phase).arg(cur).arg(tot));
}

// ----- Local folder workflow ----------------------------------------------

void MainWindow::onAddLocalFolderClicked()
{
    const QString picked = QFileDialog::getExistingDirectory(this,
        tr("Add local folder"),
        settings_->defaultCloneDirectory(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (picked.isEmpty()) return;

    const QString abs = QDir(picked).absolutePath();
    if (!settings_->addLocalFolder(abs)) {
        // Already in the list — just select it.
        repoList_->setLocalFolders(settings_->localFolders());
        repoList_->selectLocalFolder(abs);
        setStatus(tr("Folder is already in the sidebar."), 4000);
        return;
    }
    repoList_->setLocalFolders(settings_->localFolders());
    repoList_->selectLocalFolder(abs);  // triggers onLocalFolderActivated
}

void MainWindow::onLocalFolderActivated(const QString& path)
{
    if (path.isEmpty()) return;
    activeLocalPath_ = path;
    detailStack_->setCurrentWidget(localDetail_);
    localDetail_->setFolder(path);
    pushIdentityToWidget();
    worker_->refreshLocalState(path);
    worker_->listBranchInfos(path);
}

void MainWindow::onRemoveLocalFolderRequested(const QString& path)
{
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, tr("Remove from sidebar"),
            tr("Remove '%1' from the sidebar? The folder on disk is not affected.")
                .arg(QFileInfo(path).fileName())) != QMessageBox::Yes) {
        return;
    }
    settings_->removeLocalFolder(path);
    repoList_->setLocalFolders(settings_->localFolders());
    if (activeLocalPath_ == path) {
        activeLocalPath_.clear();
        detailStack_->setCurrentWidget(repoDetail_);
    }
}

void MainWindow::onLocalInitRequested(const QString& path, const QString& branch)
{
    if (path.isEmpty()) return;
    settings_->setDefaultInitBranch(branch);
    localDetail_->setBusy(true);
    setBusy(true, tr("Initializing %1…").arg(QFileInfo(path).fileName()));
    worker_->initRepository(path, branch);
}

void MainWindow::onLocalStageAllRequested(const QString& path)
{
    if (path.isEmpty()) return;
    localDetail_->setBusy(true);
    worker_->stageAll(path);
}

void MainWindow::onLocalStagePathsRequested(const QString& path, const QStringList& paths)
{
    if (path.isEmpty() || paths.isEmpty()) return;
    localDetail_->setBusy(true);
    worker_->stagePaths(path, paths);
}

void MainWindow::onLocalUnstagePathsRequested(const QString& path, const QStringList& paths)
{
    if (path.isEmpty() || paths.isEmpty()) return;
    localDetail_->setBusy(true);
    worker_->unstagePaths(path, paths);
}

void MainWindow::onLocalCommitRequested(const QString& path, const QString& message)
{
    if (path.isEmpty() || message.trimmed().isEmpty()) return;
    if (!ensureIdentity()) {
        setStatus(tr("Commit cancelled — author identity is required."), 5000);
        return;
    }
    localDetail_->setBusy(true);
    setBusy(true, tr("Committing…"));
    worker_->commitChanges(path, message,
                           settings_->authorName(), settings_->authorEmail());
}

void MainWindow::onLocalAddRemoteRequested(const QString& path)
{
    if (path.isEmpty()) return;

    AddRemoteDialog dlg(/*suggestedBranch*/ QStringLiteral("master"), this);
    if (dlg.exec() != QDialog::Accepted) return;

    localDetail_->setBusy(true);
    worker_->addRemote(path, dlg.name(), dlg.url());
}

void MainWindow::onLocalRemoveRemoteRequested(const QString& path, const QString& name)
{
    if (path.isEmpty() || name.isEmpty()) return;
    if (QMessageBox::question(this, tr("Remove remote"),
            tr("Remove the '%1' remote? This only affects the local "
               "configuration.").arg(name)) != QMessageBox::Yes) {
        return;
    }
    localDetail_->setBusy(true);
    worker_->removeRemote(path, name);
}

void MainWindow::onLocalPushRequested(const QString& path,
                                      const QString& remoteName,
                                      const QString& branch,
                                      bool           setUpstream)
{
    if (path.isEmpty() || remoteName.isEmpty() || branch.isEmpty()) return;
    if (token_.isEmpty()) {
        QMessageBox::information(this, tr("Sign in required"),
            tr("Push uses your GitHub personal access token. Please sign in first."));
        promptLogin();
        if (token_.isEmpty()) return;
    }
    localDetail_->setBusy(true);
    setBusy(true, tr("Pushing %1 → %2…").arg(branch, remoteName));
    worker_->pushTo(path, remoteName, branch, setUpstream, token_);
}

void MainWindow::onLocalRefreshRequested(const QString& path)
{
    if (path.isEmpty()) return;
    worker_->refreshLocalState(path);
    worker_->listBranchInfos(path);
}

void MainWindow::onLocalHistoryRequested(const QString& path)
{
    if (path.isEmpty()) return;
    worker_->loadHistory(path, /*maxCount*/ 200);
}

void MainWindow::onLocalDiffRequested(const QString& path,
                                      const QString& repoRelPath,
                                      ghm::git::DiffScope scope)
{
    if (path.isEmpty() || repoRelPath.isEmpty()) return;
    worker_->loadFileDiff(path, repoRelPath, scope);
}

void MainWindow::onLocalCommitDiffRequested(const QString& path, const QString& sha)
{
    if (path.isEmpty() || sha.isEmpty()) return;
    worker_->loadCommitDiff(path, sha);
}

void MainWindow::onCommitDiffReady(
    const QString&                          path,
    const QString&                          sha,
    const std::vector<ghm::git::FileDiff>&  files,
    const QString&                          error)
{
    // Late results from a previous folder shouldn't leak into the
    // currently-active one's UI.
    if (path != activeLocalPath_) return;
    localDetail_->setCommitDiff(sha, files, error);
}

// ----- Branch management ----------------------------------------------

void MainWindow::onLocalBranchSwitchRequested(const QString& path,
                                              const QString& branch)
{
    if (path.isEmpty() || branch.isEmpty()) return;
    setBusy(true, tr("Switching to %1…").arg(branch));
    localDetail_->setBusy(true);
    worker_->switchBranch(path, branch);
}

void MainWindow::onLocalBranchCreateRequested(const QString& path)
{
    if (path.isEmpty()) return;

    // Pull the current branch + existing names off the worker via a
    // fresh sync call. We could cache them in MainWindow, but since
    // branches change in lots of places (init, commit, remote ops),
    // re-fetching keeps the dialog honest without bookkeeping.
    ghm::git::GitResult err;
    const QString currentName = worker_->handler().currentBranch(path, &err);
    std::vector<ghm::git::BranchInfo> infos;
    (void)worker_->handler().listLocalBranches(path, infos);
    QStringList existing;
    for (const auto& b : infos) existing << b.name;

    CreateBranchDialog dlg(currentName, existing, this);
    if (dlg.exec() != QDialog::Accepted) return;

    setBusy(true, tr("Creating branch %1…").arg(dlg.name()));
    localDetail_->setBusy(true);
    worker_->createBranch(path, dlg.name(), dlg.checkoutAfter());
}

void MainWindow::onLocalBranchDeleteRequested(const QString& path,
                                              const QString& branch)
{
    if (path.isEmpty() || branch.isEmpty()) return;

    // First-pass non-force delete. If libgit2 says the branch isn't
    // merged into HEAD, we get a clear error back via branchDeleted
    // and re-prompt with a force-delete confirmation.
    auto reply = QMessageBox::question(this, tr("Delete branch"),
        tr("Delete the local branch <b>%1</b>?<br><br>"
           "This is a local-only operation; nothing on GitHub is affected.")
            .arg(branch.toHtmlEscaped()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    setBusy(true, tr("Deleting branch %1…").arg(branch));
    localDetail_->setBusy(true);
    worker_->deleteBranch(path, branch, /*force*/ false);
}

void MainWindow::onBranchInfosReady(
    const QString& path,
    const std::vector<ghm::git::BranchInfo>& branches)
{
    if (path != activeLocalPath_) return;
    localDetail_->setBranches(branches);
}

void MainWindow::onBranchCreated(bool ok, const QString& path,
                                 const QString& name, const QString& error)
{
    setBusy(false);
    localDetail_->setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, tr("Create branch failed"), error);
    } else {
        setStatus(tr("Created branch %1.").arg(name), 4000);
    }
    if (activeLocalPath_ == path) {
        // refreshLocalState pokazuje stan plików; listBranchInfos
        // odświeża popup z gałęziami.
        worker_->refreshLocalState(path);
        worker_->listBranchInfos(path);
    }
}

void MainWindow::onBranchDeleted(bool ok, const QString& path,
                                 const QString& name, const QString& error)
{
    setBusy(false);
    localDetail_->setBusy(false);

    if (!ok) {
        // Detect "not merged" failure and offer force-delete. Our
        // GitHandler emits a deterministic message starting with
        // "Branch '...' has N commit(s) not merged" — match against
        // that prefix.
        if (error.contains(QStringLiteral("not merged"))) {
            const auto reply = QMessageBox::warning(this,
                tr("Branch is not merged"),
                tr("<b>%1</b> contains commits that aren't reachable from "
                   "your current branch.<br><br>%2<br><br>"
                   "Force-delete anyway? <b>The unique commits will be lost</b> "
                   "unless they're referenced from another branch.")
                    .arg(name.toHtmlEscaped(), error.toHtmlEscaped()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                setBusy(true, tr("Force-deleting branch %1…").arg(name));
                localDetail_->setBusy(true);
                worker_->deleteBranch(path, name, /*force*/ true);
                return;
            }
        } else {
            QMessageBox::warning(this, tr("Delete branch failed"), error);
        }
    } else {
        setStatus(tr("Deleted branch %1.").arg(name), 4000);
    }

    if (activeLocalPath_ == path) {
        worker_->refreshLocalState(path);
        worker_->listBranchInfos(path);
    }
}

void MainWindow::onFileDiffReady(const QString& path,
                                 const QString& repoRelPath,
                                 const ghm::git::FileDiff& diff,
                                 const QString& error)
{
    // Late results from a previous folder shouldn't leak into the
    // currently-active one's UI.
    if (path != activeLocalPath_) return;
    localDetail_->setFileDiff(repoRelPath, diff, error);
}

void MainWindow::onPublishToGitHubRequested(const QString& path)
{
    if (path.isEmpty()) return;
    if (token_.isEmpty()) {
        QMessageBox::information(this, tr("Sign in required"),
            tr("Publishing requires being signed in to GitHub. Please sign in first."));
        promptLogin();
        if (token_.isEmpty()) return;
    }
    if (!pendingPublish_.path.isEmpty()) {
        QMessageBox::information(this, tr("Already publishing"),
            tr("Another publish operation is in progress — please wait for it to finish."));
        return;
    }

    const QString folderName = QFileInfo(path).fileName();
    PublishToGitHubDialog dlg(folderName,
                              /*suggestedRepoName*/ QString(),
                              /*accountLogin*/      username_,
                              /*knownRepos*/        reposCache_,
                              this);
    if (dlg.exec() != QDialog::Accepted) return;

    // Snapshot dialog state into pending publish.
    pendingPublish_.path      = path;
    pendingPublish_.cloneUrl  .clear();
    pendingPublish_.pushAfter = dlg.pushAfterPublish();

    if (dlg.mode() == PublishToGitHubDialog::Mode::CreateNew) {
        // Step 1: create the repo on GitHub. We resume the flow in
        // onRepositoryCreated() once the API call comes back.
        setBusy(true, tr("Creating GitHub repository \"%1\"…").arg(dlg.name()));
        client_->createRepository(dlg.name(), dlg.description(),
                                  dlg.isPrivate(), /*autoInit*/ false);
    } else {
        // Skip step 1 — we already have the repo metadata.
        const auto repo = dlg.existingRepo();
        if (!repo.isValid() || repo.cloneUrl.isEmpty()) {
            pendingPublish_ = {};
            QMessageBox::warning(this, tr("Publish failed"),
                tr("The selected repository has no clone URL. Try refreshing the list."));
            return;
        }
        pendingPublish_.cloneUrl = repo.cloneUrl;
        // Track this clone so the sidebar's GitHub section shows the badge.
        rememberLocalPath(repo.fullName, path);

        setBusy(true, tr("Linking %1 → %2…").arg(folderName, repo.fullName));
        localDetail_->setBusy(true);
        worker_->addRemote(path, QStringLiteral("origin"), repo.cloneUrl);
    }
}

void MainWindow::onRepositoryCreated(const ghm::github::Repository& repo)
{
    // We only ever reach this path through the publish flow, but we
    // guard regardless: if a stray repositoryCreated arrives without a
    // pending publish, just refresh the listing and treat it as info.
    if (pendingPublish_.path.isEmpty() || !repo.isValid()) {
        setBusy(false);
        if (repo.isValid()) {
            setStatus(tr("Created %1.").arg(repo.fullName), 5000);
            refreshRepositories();
        }
        return;
    }

    pendingPublish_.cloneUrl = repo.cloneUrl;
    // Optimistically reflect in our caches so the sidebar updates the
    // moment the worker confirms the local remote-add.
    rememberLocalPath(repo.fullName, pendingPublish_.path);

    // Insert into the cached list as well so PublishToGitHubDialog
    // future-opens see the new repo without a manual refresh.
    auto annotated = repo;
    annotated.localPath = pendingPublish_.path;
    reposCache_.prepend(annotated);
    repoList_->setRepositories(reposCache_);

    setStatus(tr("Created %1 — wiring up origin…").arg(repo.fullName));
    localDetail_->setBusy(true);
    worker_->addRemote(pendingPublish_.path,
                       QStringLiteral("origin"), repo.cloneUrl);
}

void MainWindow::onEditIdentityRequested()
{
    IdentityDialog dlg(settings_->authorName(), settings_->authorEmail(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    settings_->setAuthorName (dlg.name());
    settings_->setAuthorEmail(dlg.email());
    pushIdentityToWidget();
}

// ----- Worker callbacks (local-folder flow) --------------------------------

void MainWindow::onInitFinished(bool ok, const QString& path, const QString& error)
{
    setBusy(false);
    localDetail_->setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, tr("Initialize failed"), error);
        return;
    }
    setStatus(tr("Initialized empty repository in %1").arg(path), 5000);
    if (activeLocalPath_ == path) {
        worker_->refreshLocalState(path);
    }
}

void MainWindow::onLocalStateReady(
    const QString& path,
    bool                                       isRepository,
    const QString&                             branch,
    const std::vector<ghm::git::StatusEntry>&  entries,
    const std::vector<ghm::git::RemoteInfo>&   remotes)
{
    // The publish state machine doesn't care which folder the user has
    // currently focused — it tracks paths independently. Resolve it
    // before applying the activeLocalPath filter below.
    if (!pendingPublish_.path.isEmpty()
        && pendingPublish_.path == path
        && pendingPublish_.pushAfter)
    {
        const bool unborn = branch.isEmpty() || branch.startsWith(QLatin1Char('('));
        if (unborn) {
            pendingPublish_ = {};
            setBusy(false);
            localDetail_->setBusy(false);
            QMessageBox::information(this, tr("Nothing to push yet"),
                tr("The remote is connected, but this branch has no commits. "
                   "Make a commit and then push from the Remotes tab."));
        } else {
            // pushAfter is one-shot — clear it before triggering push so
            // the push completion handler treats it like any other push.
            pendingPublish_.pushAfter = false;
            setBusy(true, tr("Pushing %1 → origin…").arg(branch));
            localDetail_->setBusy(true);
            worker_->pushTo(path, QStringLiteral("origin"), branch,
                            /*setUpstreamAfter*/ true, token_);
            // Push fired; don't return — still want to refresh the UI
            // for the active path if it matches.
        }
    }

    if (path != activeLocalPath_) return;
    localDetail_->setBusy(false);
    localDetail_->setLocalState(isRepository, branch, entries, remotes);

    if (!isRepository) {
        setStatus(tr("Folder is not a Git repository — initialize to start."), 4000);
        return;
    }
    QString summary;
    if (entries.empty()) summary = tr("Working tree clean");
    else                 summary = tr("%1 changed file(s)").arg(entries.size());
    setStatus(tr("On %1 — %2").arg(branch.isEmpty() ? QStringLiteral("(unborn)") : branch,
                                   summary), 4000);
}

void MainWindow::onStageFinished(bool ok, const QString& path, const QString& error)
{
    if (!ok) QMessageBox::warning(this, tr("Stage failed"), error);
    if (activeLocalPath_ == path) worker_->refreshLocalState(path);
}

void MainWindow::onUnstageFinished(bool ok, const QString& path, const QString& error)
{
    if (!ok) QMessageBox::warning(this, tr("Unstage failed"), error);
    if (activeLocalPath_ == path) worker_->refreshLocalState(path);
}

void MainWindow::onCommitFinished(bool ok, const QString& path,
                                  const QString& sha, const QString& error)
{
    setBusy(false);
    localDetail_->setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, tr("Commit failed"), error);
        return;
    }
    setStatus(tr("Committed %1").arg(sha.left(7)), 5000);
    if (activeLocalPath_ == path) {
        worker_->refreshLocalState(path);
        // Invalidate history so the next visit reloads.
        worker_->loadHistory(path, /*maxCount*/ 200);
    }
}

void MainWindow::onHistoryReady(const QString& path,
                                const std::vector<ghm::git::CommitInfo>& commits)
{
    if (path != activeLocalPath_) return;
    localDetail_->setHistory(commits);
}

void MainWindow::onRemoteOpFinished(bool ok, const QString& path, const QString& error)
{
    localDetail_->setBusy(false);

    // Is this the addRemote step of a publish flow?
    const bool isPublishStep = !pendingPublish_.path.isEmpty()
                            &&  pendingPublish_.path == path
                            && !pendingPublish_.cloneUrl.isEmpty();

    if (!ok) {
        if (isPublishStep) {
            const auto cloneUrl = pendingPublish_.cloneUrl;
            pendingPublish_ = {};
            setBusy(false);
            QMessageBox::warning(this, tr("Publish failed"),
                tr("The GitHub repository was created (or selected), but wiring up "
                   "the local 'origin' remote failed:\n\n%1\n\n"
                   "You can add it manually with:\n  git remote add origin %2")
                    .arg(error, cloneUrl));
            if (activeLocalPath_ == path) worker_->refreshLocalState(path);
            return;
        }
        QMessageBox::warning(this, tr("Remote operation failed"), error);
        if (activeLocalPath_ == path) worker_->refreshLocalState(path);
        return;
    }

    if (isPublishStep) {
        // Remote successfully added. Either push next, or wrap up.
        if (pendingPublish_.pushAfter) {
            const QString branch = localDetail_->currentPath() == path
                                       ? QString() : QString();
            // Read the branch name straight from the worker's last
            // localStateReady — we cached it via setLocalState. The
            // simplest way is to refresh first, but that's an extra
            // round-trip; since we just successfully ran addRemote,
            // we can rely on localDetail_'s current branch_ field via
            // a fresh refresh call:
            setStatus(tr("Pushing to GitHub…"));
            // Refresh state to pick up the new remote, then in the
            // callback chain push. Easier: just push using whichever
            // branch the user is currently on. We grab it from libgit2.
            // For robustness we go through refreshLocalState first so
            // the UI reflects the new remote, *then* push.
            worker_->refreshLocalState(path);
            // Use the just-confirmed snapshot — we'll trigger push from
            // onLocalStateReady by checking pendingPublish_.
            return;
        }
        const auto fullClone = pendingPublish_.cloneUrl;
        pendingPublish_ = {};
        setBusy(false);
        setStatus(tr("Connected to %1.").arg(fullClone), 6000);
        if (activeLocalPath_ == path) worker_->refreshLocalState(path);
        return;
    }

    // Plain manual remote add/remove from the Remotes tab.
    if (activeLocalPath_ == path) worker_->refreshLocalState(path);
}

// ----- App-level UI --------------------------------------------------------

void MainWindow::onLanguageChosen(const QString& code)
{
    if (code.isEmpty() || code == settings_->language()) return;

    settings_->setLanguage(code);

    // Hot-swapping QTranslators in a running Qt app is fiddly: every
    // hand-written widget would have to override changeEvent() and
    // re-translate its strings. Rather than ship a half-translated UI
    // until something is fixed, we tell the user to restart and let the
    // next launch come up cleanly in the new language.
    QMessageBox::information(this, tr("Language changed"),
        tr("Restart the application to apply the new language."));
}

void MainWindow::onShowSupportDialog()
{
    SupportDialog dlg(this);
    dlg.exec();
}

} // namespace ghm::ui
