#include "ui/MainWindow.h"

#include "ui/LoginDialog.h"
#include "ui/CloneDialog.h"
#include "ui/SshKeyDialog.h"
#include "ui/HostKeyApprover.h"
#include "ui/TlsCertApprover.h"
#include "ui/AddSubmoduleDialog.h"
#include "ui/SigningSettingsDialog.h"
#include "ui/TrustedServersDialog.h"
#include "ui/RememberedKeysDialog.h"
#include "ui/ChangelogDialog.h"

#include <git2.h>
#include <QStandardPaths>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include "ui/IdentityDialog.h"
#include "ui/AddRemoteDialog.h"
#include "ui/PublishToGitHubDialog.h"
#include "ui/SupportDialog.h"
#include "ui/CreateBranchDialog.h"
#include "ui/BranchNameValidator.h"
#include "ui/StashSaveDialog.h"
#include "ui/StashListDialog.h"
#include "ui/TagsDialog.h"
#include "ui/ReflogDialog.h"
// ConflictResolutionDialog is owned by ConflictController now;
// MainWindow doesn't include it directly.
#include "ui/RepositoryListWidget.h"
#include "ui/RepositoryDetailWidget.h"
#include "ui/LocalRepositoryWidget.h"

#include "core/SecureStorage.h"
#include "core/Settings.h"
#include "github/GitHubClient.h"
#include "github/SshUrlConverter.h"
#include "git/GitWorker.h"
#include "session/SessionController.h"

#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QMenuBar>
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <climits>  // INT_MAX, used when scaling huge libgit2 object counts
#include <QMessageBox>
#include <QAbstractButton>
#include <QInputDialog>
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
    , session_  (new ghm::session::SessionController(storage_, settings_, this))
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

    // LocalWorkspaceController needs localDetail_ which buildUi just
    // created. Created here so wireSignals can connect to it.
    workspace_ = new ghm::workspace::LocalWorkspaceController(
        *worker_, settings_, *localDetail_, this);

    // PublishController owns the "Publish to GitHub" state machine
    // (POST /user/repos → addRemote → refresh → push). It needs the
    // network client and the session for tokens; MainWindow keeps
    // dialogs and status-bar updates by listening to its signals.
    publish_ = new ghm::workspace::PublishController(
        *session_->client(), *worker_, *session_, this);

    // ConflictController owns the merge-conflict resolution flow:
    // the dialog, the listConflicts → loadBlobs → markResolved →
    // re-list loop, and the abort path. MainWindow keeps only the
    // user-action trigger (Resolve button) and translates the
    // controller's high-level signals into status-bar and dialog
    // side-effects.
    conflict_ = new ghm::workspace::ConflictController(*worker_, this, this);

    // GitHubCloneController owns the "clone from GitHub" and
    // "open existing local copy" flows. MainWindow keeps the
    // CloneDialog and QFileDialog handling — the controller takes
    // the resolved targetPath / localPath as parameters and does
    // the worker dispatch + path-existence validation.
    clone_ = new ghm::workspace::GitHubCloneController(*worker_, this);

    // Host-key approver — lives on the GUI thread and serves as the
    // bridge between libgit2's certificate_check callback (running
    // on the worker thread) and the modal HostKeyApprovalDialog.
    // The approver registers itself as a singleton in its constructor
    // so the C-style callback in GitHandler.cpp can find it without
    // a payload pointer plumbed through every API.
    hostKeyApprover_ = new ghm::ui::HostKeyApprover(this, this);

    // Same singleton pattern for TLS X.509 cert approval. Used when
    // libgit2 hits a server cert that doesn't validate against the
    // system trust store (self-signed, internal CA, MITM).
    tlsCertApprover_ = new ghm::ui::TlsCertApprover(this, settings_, this);

    wireSignals();

    // Restore window geometry / state.
    if (auto g = settings_.mainWindowGeometry(); !g.isEmpty()) restoreGeometry(g);
    if (auto s = settings_.mainWindowState();    !s.isEmpty()) restoreState(s);

    // Populate initial state from settings.
    repoList_->setLocalFolders(settings_.localFolders());
    localDetail_->setDefaultInitBranch(settings_.defaultInitBranch());
    pushIdentityToWidget();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------

void MainWindow::buildUi()
{
    splitter_ = new QSplitter(Qt::Horizontal, this);
    repoList_    = new RepositoryListWidget(splitter_);
    detailStack_ = new QStackedWidget(splitter_);
    repoDetail_  = new RepositoryDetailWidget(detailStack_);
    localDetail_ = new LocalRepositoryWidget(detailStack_);
    localDetail_->setSettings(&settings_);
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

    // Permanent version button so users can see at a glance which
    // build they're running. Comes from the GHM_VERSION macro defined
    // by CMakeLists.txt — single source of truth, no risk of drift.
    //
    // Clicking opens the changelog. We use a QPushButton (flat,
    // styled as a link) rather than a QLabel because QLabel has no
    // native clicked() signal and event-filtering a click out of
    // QLabel is more code than just using the right widget.
    auto* versionBtn = new QPushButton(
        QStringLiteral("v") + QString::fromLatin1(GHM_VERSION), this);
    versionBtn->setFlat(true);
    versionBtn->setCursor(Qt::PointingHandCursor);
    versionBtn->setStyleSheet(QStringLiteral(
        "QPushButton { "
        "  color: #707680; "
        "  padding-right: 8px; "
        "  border: none; "
        "  background: transparent; "
        "  text-align: right; "
        "} "
        "QPushButton:hover { color: #79c0ff; text-decoration: underline; }"));
    versionBtn->setToolTip(tr(
        "Click to see what changed across versions"));
    connect(versionBtn, &QPushButton::clicked, this, [this] {
        ChangelogDialog dlg(this);
        dlg.exec();
    });

    statusBar()->addWidget(statusMessage_, 1);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(userLabel_);
    statusBar()->addPermanentWidget(versionBtn);
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

    // Repository menu — operations that act on the currently-active
    // local folder. Items are disabled when no local folder is
    // selected; that gating happens in updateRepoMenuEnabled() which
    // is fired whenever activeLocalPath_ changes.
    auto* repoMenu = menuBar()->addMenu(tr("&Repository"));
    fetchAction_     = repoMenu->addAction(tr("Fetch from origin"));
    fetchAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
    repoMenu->addSeparator();
    stashSaveAction_ = repoMenu->addAction(tr("Stash changes…"));
    stashListAction_ = repoMenu->addAction(tr("Manage stashes…"));
    repoMenu->addSeparator();
    tagsAction_      = repoMenu->addAction(tr("Tags…"));
    repoMenu->addSeparator();
    reflogAction_    = repoMenu->addAction(tr("Reflog — recover lost commits…"));
    undoCommitAction_ = repoMenu->addAction(tr("Undo last commit…"));
    undoCommitAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    repoMenu->addSeparator();
    openFolderAction_ = repoMenu->addAction(tr("Open folder in file manager"));
    openFolderAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    openFolderAction_->setToolTip(tr(
        "Open the repository folder in your system file manager "
        "(uses xdg-open, so respects your default app)."));

    connect(fetchAction_,     &QAction::triggered,
            this, &MainWindow::onFetchRequested);
    connect(stashSaveAction_, &QAction::triggered,
            this, &MainWindow::onStashSaveRequested);
    connect(stashListAction_, &QAction::triggered,
            this, &MainWindow::onStashListRequested);
    connect(tagsAction_,      &QAction::triggered,
            this, &MainWindow::onTagsRequested);
    connect(reflogAction_,    &QAction::triggered,
            this, &MainWindow::onReflogRequested);
    connect(undoCommitAction_, &QAction::triggered,
            this, &MainWindow::onUndoLastCommitRequested);
    connect(openFolderAction_, &QAction::triggered, this, [this] {
        if (activeLocalPath_.isEmpty()) return;
        // QDesktopServices uses xdg-open under the hood — respects
        // the user's MIME/desktop preferences. Async; we don't
        // block on the file manager opening.
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(activeLocalPath_));
    });
    // All start disabled — no folder active yet.
    fetchAction_     ->setEnabled(false);
    stashSaveAction_ ->setEnabled(false);
    stashListAction_ ->setEnabled(false);
    tagsAction_      ->setEnabled(false);
    reflogAction_    ->setEnabled(false);
    undoCommitAction_->setEnabled(false);
    openFolderAction_->setEnabled(false);

    // Settings → Language. Languages are exclusive radio actions whose
    // checkmark reflects the currently-installed translation. Picking a
    // different one persists the choice and prompts for a restart.
    auto* settingsMenu = menuBar()->addMenu(tr("&Settings"));
    auto* langMenu     = settingsMenu->addMenu(tr("&Language"));
    auto* langGroup    = new QActionGroup(this);
    langGroup->setExclusive(true);
    const QString currentLang = settings_.language();
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

    settingsMenu->addSeparator();

    // SSH preference toggle. When checked, the clone-from-GitHub flow
    // rewrites HTTPS URLs into their git@host:owner/repo equivalents
    // before handing them to libgit2. Auth then goes through ssh-agent.
    // Doesn't affect existing remotes — those keep whatever URL is
    // already in .git/config.
    auto* sshAct = settingsMenu->addAction(tr("&Prefer SSH for new clones"));
    sshAct->setCheckable(true);
    sshAct->setChecked(settings_.clonePreferSsh());
    sshAct->setToolTip(tr(
        "When enabled, cloning a repo from the sidebar will use the SSH "
        "URL (git@host:owner/repo) and authenticate via ssh-agent. "
        "Requires an SSH key registered with your GitHub account and "
        "loaded into ssh-agent. Existing remotes are unaffected."));
    connect(sshAct, &QAction::toggled, this, [this](bool checked) {
        settings_.setClonePreferSsh(checked);
        setStatus(checked
            ? tr("New clones will use SSH (ssh-agent).")
            : tr("New clones will use HTTPS."),
            4000);
    });

    // Commit signing — opens SigningSettingsDialog which configures
    // GPG/SSH/None mode and the signing key. No-op until the user
    // actually commits; we don't pre-test the signing setup here
    // because a pinentry popup during a settings change would feel
    // intrusive (and on headless hosts would block silently).
    auto* signingAct = settingsMenu->addAction(tr("&Commit signing…"));
    signingAct->setToolTip(tr(
        "Configure GPG or SSH commit signing. When enabled, every "
        "commit through this app is signed; signing failures abort "
        "the commit (no silent fallback to unsigned)."));
    connect(signingAct, &QAction::triggered, this, [this] {
        SigningSettingsDialog dlg(settings_, this);
        if (dlg.exec() != QDialog::Accepted) return;
        // Reflect the current state in the status bar so the user
        // gets feedback that something changed.
        const auto mode = settings_.signingMode();
        if (mode == ghm::core::Settings::SigningMode::None) {
            setStatus(tr("Commit signing disabled."), 4000);
        } else if (mode == ghm::core::Settings::SigningMode::Gpg) {
            setStatus(tr("Commit signing enabled (GPG)."), 4000);
        } else {
            setStatus(tr("Commit signing enabled (SSH)."), 4000);
        }
    });

    // Trusted TLS servers — opens TrustedServersDialog showing every
    // host fingerprint the user has approved via the TLS approval
    // dialog. Lets them inspect and revoke individual entries
    // without editing the config file by hand.
    auto* trustedAct = settingsMenu->addAction(tr("&Trusted TLS servers…"));
    trustedAct->setToolTip(tr(
        "View and remove TLS server certificates you've previously "
        "approved via 'Accept and remember'."));
    connect(trustedAct, &QAction::triggered, this, [this] {
        TrustedServersDialog dlg(settings_, this);
        dlg.exec();
    });

    // Remembered submodule SSH keys — opens RememberedKeysDialog.
    // Saved via the per-submodule "Init/Update with explicit key"
    // context menu (0.32.0). Same management affordances as
    // trusted servers: read-mostly table with per-row remove.
    auto* keysAct = settingsMenu->addAction(
        tr("&Remembered submodule keys…"));
    keysAct->setToolTip(tr(
        "View and forget SSH key mappings saved per submodule. "
        "Mappings are added when you choose 'Init/Update with "
        "explicit SSH key' on a submodule."));
    connect(keysAct, &QAction::triggered, this, [this] {
        RememberedKeysDialog dlg(settings_, this);
        dlg.exec();
    });

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
    connect(refreshAction_,  &QAction::triggered, this, [this] {
        session_->refreshRepositories();
    });
    connect(logoutAction_,   &QAction::triggered, this, &MainWindow::requestSignOut);
    connect(quitAction_,     &QAction::triggered, this, &QMainWindow::close);
    connect(addLocalAction_, &QAction::triggered, this, &MainWindow::onAddLocalFolderClicked);

    // SessionController — drives login state, repo listing, sign-out
    // confirmation. We just react to its signals here.
    connect(session_, &ghm::session::SessionController::signedIn,
            this, &MainWindow::onSessionSignedIn);
    connect(session_, &ghm::session::SessionController::sessionRestored,
            this, &MainWindow::onSessionRestored);
    connect(session_, &ghm::session::SessionController::signedOut,
            this, &MainWindow::onSessionSignedOut);
    connect(session_, &ghm::session::SessionController::authError,
            this, &MainWindow::onSessionAuthError);
    connect(session_, &ghm::session::SessionController::networkError,
            this, &MainWindow::onSessionNetworkError);
    connect(session_, &ghm::session::SessionController::keyringError,
            this, &MainWindow::onSessionKeyringError);
    connect(session_, &ghm::session::SessionController::busy,
            this, &MainWindow::onSessionBusy);
    connect(session_, &ghm::session::SessionController::repositoriesLoaded,
            this, &MainWindow::onSessionReposLoaded);

    // PublishController feedback. Controller does the state machine
    // and subscribes to GitHubClient/GitWorker signals itself;
    // MainWindow only handles dialogs and UI side-effects.
    connect(publish_, &ghm::workspace::PublishController::progress,
            this, &MainWindow::onPublishProgress);
    connect(publish_, &ghm::workspace::PublishController::succeeded,
            this, &MainWindow::onPublishSucceeded);
    connect(publish_, &ghm::workspace::PublishController::failed,
            this, &MainWindow::onPublishFailed);
    connect(publish_, &ghm::workspace::PublishController::repoCreated,
            this, &MainWindow::onPublishRepoCreated);
    connect(publish_, &ghm::workspace::PublishController::needNonEmptyBranch,
            this, &MainWindow::onPublishNeedNonEmptyBranch);

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
    connect(repoList_, &RepositoryListWidget::changeVisibilityRequested,
            this, &MainWindow::onChangeVisibilityRequested);

    // Visibility-change result wire. GitHubClient emits
    // visibilityChanged with the fresh Repository on success; we
    // patch the local cache, refresh the sidebar, and surface a
    // status message. Errors come via networkError which already
    // pipes into the status bar.
    if (session_->client()) {
        connect(session_->client(),
                &ghm::github::GitHubClient::visibilityChanged,
                this, &MainWindow::onVisibilityChanged);
    }

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

    // Repository-detail preview wiring. Detail widget emits fetch
    // requests; we forward to GitHubClient, then route the response
    // signals back. openInBrowserRequested goes through
    // QDesktopServices so widget stays Qt-only (no QtNetwork-vs-
    // QtGui mixing).
    if (auto* gh = session_->client()) {
        connect(repoDetail_, &RepositoryDetailWidget::readmeRequested,
                gh, &ghm::github::GitHubClient::fetchReadme);
        connect(repoDetail_, &RepositoryDetailWidget::contentsRequested,
                gh, &ghm::github::GitHubClient::fetchContents);
        connect(repoDetail_, &RepositoryDetailWidget::languagesRequested,
                gh, &ghm::github::GitHubClient::fetchLanguages);

        connect(gh, &ghm::github::GitHubClient::readmeFetched,
                repoDetail_, &RepositoryDetailWidget::setReadme);
        connect(gh, &ghm::github::GitHubClient::readmeNotFound,
                repoDetail_, &RepositoryDetailWidget::setReadmeUnavailable);
        connect(gh, &ghm::github::GitHubClient::contentsFetched,
                repoDetail_, &RepositoryDetailWidget::setRemoteContents);
        connect(gh, &ghm::github::GitHubClient::languagesFetched,
                repoDetail_, &RepositoryDetailWidget::setLanguages);
    }
    connect(repoDetail_, &RepositoryDetailWidget::openInBrowserRequested,
            this, [](const QString& url) {
                if (url.isEmpty()) return;
                QDesktopServices::openUrl(QUrl(url));
            });

    // Detail panel (local folders).
    // Forward most local-repo widget signals straight to the
    // workspace controller. The controller does the validation and
    // worker delegation; results come back via the controller's own
    // signals (wired below).
    //
    // Branch delete still routes through MainWindow first because
    // we want a confirmation dialog before the worker even sees the
    // request. Add/remove remote, push, and publish to GitHub also
    // stay in MainWindow — they pop dialogs that need a QWidget
    // parent or use the GitHub session token.
    connect(localDetail_, &LocalRepositoryWidget::initRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onInitRequested);
    connect(localDetail_, &LocalRepositoryWidget::stageAllRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onStageAllRequested);
    connect(localDetail_, &LocalRepositoryWidget::stagePathsRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onStagePathsRequested);
    connect(localDetail_, &LocalRepositoryWidget::unstagePathsRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onUnstagePathsRequested);
    connect(localDetail_, &LocalRepositoryWidget::commitRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onCommitRequested);
    connect(localDetail_, &LocalRepositoryWidget::addRemoteRequested,
            this, &MainWindow::onLocalAddRemoteRequested);
    connect(localDetail_, &LocalRepositoryWidget::removeRemoteRequested,
            this, &MainWindow::onLocalRemoveRemoteRequested);
    connect(localDetail_, &LocalRepositoryWidget::pushLocalRequested,
            this, &MainWindow::onLocalPushRequested);
    connect(localDetail_, &LocalRepositoryWidget::publishToGitHubRequested,
            this, &MainWindow::onPublishToGitHubRequested);
    connect(localDetail_, &LocalRepositoryWidget::refreshRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onRefreshRequested);
    connect(localDetail_, &LocalRepositoryWidget::historyRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onHistoryRequested);
    connect(localDetail_, &LocalRepositoryWidget::loadMoreHistoryRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onLoadMoreHistoryRequested);
    connect(localDetail_, &LocalRepositoryWidget::diffRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onDiffRequested);
    connect(localDetail_, &LocalRepositoryWidget::commitDiffRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onCommitDiffRequested);
    connect(localDetail_, &LocalRepositoryWidget::commitCompareRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onCommitCompareRequested);
    connect(localDetail_, &LocalRepositoryWidget::branchSwitchRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onBranchSwitchRequested);
    connect(localDetail_, &LocalRepositoryWidget::branchCreateRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onBranchCreateRequested);

    // -- Submodules ----------------------------------------------------
    //
    // We wire submodule signals directly to the worker, bypassing the
    // workspace controller — same pattern as Tags. The workspace
    // controller is a thin filter that gates background ops by
    // activePath_; for submodules the widget already knows the path
    // (it's path_ on the widget) so the indirection would be redundant.
    connect(localDetail_, &LocalRepositoryWidget::submodulesListRequested,
            this, [this](const QString& path) {
                if (path == activeLocalPath_) worker_->listSubmodules(path);
            });
    connect(localDetail_, &LocalRepositoryWidget::submoduleInitRequested,
            this, [this](const QString& path, const QString& name) {
                if (path != activeLocalPath_) return;
                setStatus(tr("Initializing submodule '%1'…").arg(name));
                worker_->initAndUpdateSubmodule(path, name, session_->token());
            });
    connect(localDetail_, &LocalRepositoryWidget::submoduleUpdateRequested,
            this, [this](const QString& path, const QString& name) {
                if (path != activeLocalPath_) return;
                setStatus(tr("Updating submodule '%1'…").arg(name));
                worker_->updateSubmodule(path, name, session_->token());
            });
    connect(localDetail_, &LocalRepositoryWidget::submoduleSyncRequested,
            this, [this](const QString& path, const QString& name) {
                if (path != activeLocalPath_) return;
                worker_->syncSubmoduleUrl(path, name);
            });

    // Explicit-SSH-key variants. Two-phase logic:
    //   * If we have a remembered key for this (parent, submodule)
    //     and the key file still exists on disk → reuse it without
    //     prompting (passphrase still prompted via the dialog if
    //     the key is encrypted, since we never persist passphrases)
    //   * Otherwise → pop SshKeyDialog as before, and SAVE the user's
    //     choice for next time
    //
    // The two action types (init vs update) differ only in which
    // worker method they invoke. Everything else is shared, so a
    // helper lambda captures the common dance.
    auto dispatchExplicitKey =
        [this](const QString& path, const QString& name,
               bool isUpdate) {
            if (path != activeLocalPath_) return;
            const QString remembered =
                settings_.rememberedSubmoduleKey(path, name);
            QString keyPath;
            QString passphrase;
            const bool rememberedExists = !remembered.isEmpty()
                                            && QFile::exists(remembered);

            if (rememberedExists) {
                // Reuse path. We still need a passphrase if the key
                // is encrypted; rather than try to detect that up
                // front (libgit2 will throw GIT_EAUTH; messy to
                // pre-check), we ask via the dialog but pre-fill
                // the key path so the user only types passphrase.
                // For unencrypted keys, user just hits Enter past
                // the empty passphrase field.
                SshKeyDialog keyDlg(this);
                keyDlg.setKeyPath(remembered);
                keyDlg.setMessage(tr(
                    "Using remembered key for this submodule. Enter "
                    "passphrase if the key is encrypted, otherwise "
                    "leave blank and click OK."));
                if (keyDlg.exec() != QDialog::Accepted) {
                    setStatus(tr("Submodule operation cancelled."), 3000);
                    return;
                }
                keyPath    = keyDlg.keyPath();
                passphrase = keyDlg.passphrase();
            } else {
                // Stale-remembered case: settings has an entry but the
                // file is gone. Tell the user and proceed to fresh
                // pick dialog. We'll overwrite the stale entry on
                // success.
                if (!remembered.isEmpty()) {
                    setStatus(tr("Remembered key %1 not found on disk; "
                                 "please pick a new one.").arg(remembered),
                              5000);
                    settings_.clearRememberedSubmoduleKey(path, name);
                }
                SshKeyDialog keyDlg(this);
                if (keyDlg.exec() != QDialog::Accepted) {
                    setStatus(tr("Submodule operation cancelled."), 3000);
                    return;
                }
                keyPath    = keyDlg.keyPath();
                passphrase = keyDlg.passphrase();
                // Persist the choice for next time. We do this BEFORE
                // dispatching the worker call so that even if the
                // operation fails (e.g. wrong passphrase), the user
                // doesn't need to re-pick on retry. They can still
                // forget the mapping via context menu if the key
                // turns out to be wrong.
                if (!keyPath.isEmpty()) {
                    settings_.setRememberedSubmoduleKey(path, name, keyPath);
                }
            }

            ghm::git::SshCredentials sshCreds;
            sshCreds.keyPath    = keyPath;
            sshCreds.passphrase = passphrase;

            if (isUpdate) {
                setStatus(tr("Updating submodule '%1' with explicit key…")
                            .arg(name));
                worker_->updateSubmodule(
                    path, name, session_->token(), sshCreds);
            } else {
                setStatus(tr("Initializing submodule '%1' with explicit key…")
                            .arg(name));
                worker_->initAndUpdateSubmodule(
                    path, name, session_->token(), sshCreds);
            }
        };

    connect(localDetail_,
            &LocalRepositoryWidget::submoduleInitWithExplicitKeyRequested,
            this, [dispatchExplicitKey](const QString& path,
                                          const QString& name) {
                dispatchExplicitKey(path, name, /*isUpdate*/ false);
            });
    connect(localDetail_,
            &LocalRepositoryWidget::submoduleUpdateWithExplicitKeyRequested,
            this, [dispatchExplicitKey](const QString& path,
                                          const QString& name) {
                dispatchExplicitKey(path, name, /*isUpdate*/ true);
            });

    // Forget a remembered (submodule → key file) mapping. Triggered
    // from the submodule context menu's "Forget remembered key"
    // entry; from the user's perspective, the next explicit-key
    // operation will re-prompt for a key file.
    connect(localDetail_,
            &LocalRepositoryWidget::submoduleForgetRememberedKeyRequested,
            this, [this](const QString& path, const QString& name) {
                if (path != activeLocalPath_) return;
                settings_.clearRememberedSubmoduleKey(path, name);
                setStatus(tr("Forgot remembered key for submodule '%1'.")
                            .arg(name), 3000);
            });

    // Add: spawns AddSubmoduleDialog + optional SshKeyDialog on the
    // GUI thread, then forwards to worker. Pre-flight (URL validation,
    // SshCredentials assembly) all happens here.
    connect(localDetail_, &LocalRepositoryWidget::submoduleAddRequested,
            this, &MainWindow::onSubmoduleAddRequested);

    // Remove: shows confirmation dialog. Worker is invoked only on
    // explicit Yes. Slot is shared because both UI buttons (per-row
    // Remove + future context menu) emit the same signal.
    connect(localDetail_, &LocalRepositoryWidget::submoduleRemoveRequested,
            this, &MainWindow::onSubmoduleRemoveRequested);

    connect(worker_, &ghm::git::GitWorker::submodulesReady,
            this, [this](const QString& path,
                          const std::vector<ghm::git::SubmoduleInfo>& subs) {
                if (path != activeLocalPath_) return;
                localDetail_->setSubmodules(subs);
            });

    connect(worker_, &ghm::git::GitWorker::submoduleOpFinished,
            this, &MainWindow::onSubmoduleOpFinished);

    // -- Signature verification ---------------------------------------
    //
    // Widget emits one verifyCommitSignatureRequested per signed
    // commit as soon as it's added to the history list. We forward
    // each to the worker, which queues them serially. The result
    // streams back via signatureVerified and we hand it to the
    // widget to re-render the affected row.
    //
    // We DON'T throttle here — the worker already runs tasks one at
    // a time, so concurrent gpg subprocesses aren't an issue. Worst
    // case for a 200-commit history of all-signed: 200 verifications
    // queued, ~15ms each for cached keys = ~3s of background work,
    // each result emitted as it completes (rows update progressively).
    connect(localDetail_, &LocalRepositoryWidget::verifyCommitSignatureRequested,
            this, [this](const QString& path, const QString& sha) {
                if (path != activeLocalPath_) return;
                // SSH allowed_signers path: read from git config
                // (gpg.ssh.allowedSignersFile). Empty if unset → SSH
                // verifier falls back to check-novalidate.
                worker_->verifyCommitSignature(
                    path, sha, sshAllowedSignersPath());
            });

    connect(worker_, &ghm::git::GitWorker::signatureVerified,
            this, [this](const QString& path, const QString& sha,
                          const ghm::git::VerifyResult& vr) {
                if (path != activeLocalPath_) return;
                localDetail_->setSignatureVerifyResult(sha, vr);
            });

    // Delete & rename go through MainWindow first — both want a
    // confirmation/input dialog before the worker is asked to act.
    connect(localDetail_, &LocalRepositoryWidget::branchDeleteRequested,
            this, &MainWindow::onLocalBranchDeleteRequested);
    connect(localDetail_, &LocalRepositoryWidget::branchRenameRequested,
            workspace_, &ghm::workspace::LocalWorkspaceController::onBranchRenameRequested);
    connect(localDetail_, &LocalRepositoryWidget::resolveConflictsRequested,
            this, &MainWindow::onResolveConflictsRequested);
    connect(localDetail_, &LocalRepositoryWidget::editIdentityRequested,
            this, &MainWindow::onEditIdentityRequested);

    // Worker - GitHub-clone flow.
    // cloneFinished is subscribed by GitHubCloneController; MainWindow
    // listens to that controller's higher-level signals instead.
    connect(clone_, &ghm::workspace::GitHubCloneController::cloneStarted,
            this, &MainWindow::onCloneStarted);
    connect(clone_, &ghm::workspace::GitHubCloneController::cloned,
            this, &MainWindow::onCloneSucceeded);
    connect(clone_, &ghm::workspace::GitHubCloneController::opened,
            this, &MainWindow::onOpenSucceeded);
    connect(clone_, &ghm::workspace::GitHubCloneController::failed,
            this, &MainWindow::onCloneFailed);
    connect(clone_, &ghm::workspace::GitHubCloneController::defaultCloneDirectoryChanged,
            this, &MainWindow::onDefaultCloneDirectoryChanged);

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

    // Worker - local-folder flow.
    //
    // These local-flow worker signals route through LocalWorkspaceController
    // now (it does the path filtering and force-delete escalation).
    // MainWindow listens to the controller's higher-level signals
    // for UI feedback (status bar, error dialogs, conflict count).
    //
    // localStateReady is consumed by PublishController for its
    // refresh-then-push step, and by LocalWorkspaceController for
    // the active-folder display refresh. MainWindow doesn't need
    // its own subscription anymore.

    connect(workspace_, &ghm::workspace::LocalWorkspaceController::stateRefreshed,
            this, &MainWindow::onWorkspaceStateRefreshed);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::stageOpFinished,
            this, &MainWindow::onWorkspaceStageOpFinished);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::unstageOpFinished,
            this, &MainWindow::onWorkspaceUnstageOpFinished);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::commitFinished,
            this, &MainWindow::onWorkspaceCommitFinished);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::initFinished,
            this, &MainWindow::onWorkspaceInitFinished);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::historyReady,
            this, &MainWindow::onWorkspaceHistoryReady);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::fileDiffReady,
            this, &MainWindow::onWorkspaceFileDiffReady);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::commitDiffReady,
            this, &MainWindow::onWorkspaceCommitDiffReady);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::branchInfosReady,
            this, &MainWindow::onWorkspaceBranchInfosReady);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::branchSwitched,
            this, &MainWindow::onWorkspaceBranchSwitched);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::branchCreated,
            this, &MainWindow::onWorkspaceBranchCreated);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::branchDeleted,
            this, &MainWindow::onWorkspaceBranchDeleted);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::branchRenamed,
            this, &MainWindow::onWorkspaceBranchRenamed);

    // Dialog prompts the controller can't show itself.
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::identityRequiredForCommit,
            this, &MainWindow::onWorkspaceIdentityRequiredForCommit);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::branchCreateDialogRequested,
            this, &MainWindow::onWorkspaceBranchCreateDialogRequested);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::branchForceDeleteConfirmation,
            this, &MainWindow::onWorkspaceBranchForceDeleteConfirmation);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::branchRenameDialogRequested,
            this, &MainWindow::onWorkspaceBranchRenameDialogRequested);
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::busy,
            this, [this](const QString& msg) { setBusy(true, msg); });
    connect(workspace_, &ghm::workspace::LocalWorkspaceController::idle,
            this, [this] { setBusy(false); });

    connect(worker_, &ghm::git::GitWorker::stashOpFinished,
            this, &MainWindow::onStashOpFinished);
    connect(worker_, &ghm::git::GitWorker::stashListReady,
            this, &MainWindow::onStashListReady);
    connect(worker_, &ghm::git::GitWorker::tagOpFinished,
            this, &MainWindow::onTagOpFinished);
    connect(worker_, &ghm::git::GitWorker::tagsReady,
            this, &MainWindow::onTagsReady);
    connect(worker_, &ghm::git::GitWorker::fetchFinished,
            this, &MainWindow::onFetchFinished);
    connect(worker_, &ghm::git::GitWorker::undoLastCommitFinished,
            this, &MainWindow::onUndoLastCommitFinished);
    connect(worker_, &ghm::git::GitWorker::reflogReady,
            this, &MainWindow::onReflogReady);
    connect(worker_, &ghm::git::GitWorker::softResetFinished,
            this, &MainWindow::onSoftResetFinished);
    // Transfer progress for clone/pull/push/fetch — the worker emits
    // these via the libgit2 transfer callback (see makeProgressFn).
    // Status-bar progress bar reflects current object count.
    connect(worker_, &ghm::git::GitWorker::progress,
            this, &MainWindow::onWorkerProgress);

    // Conflict worker signals are subscribed by ConflictController.
    // MainWindow listens to the controller's higher-level signals.
    connect(conflict_, &ghm::workspace::ConflictController::statusChanged,
            this, &MainWindow::onConflictStatusChanged);
    connect(conflict_, &ghm::workspace::ConflictController::operationSucceeded,
            this, &MainWindow::onConflictSucceeded);
    connect(conflict_, &ghm::workspace::ConflictController::operationFailed,
            this, &MainWindow::onConflictFailed);
    connect(conflict_, &ghm::workspace::ConflictController::allResolved,
            this, &MainWindow::onConflictAllResolved);
    connect(conflict_, &ghm::workspace::ConflictController::workingTreeChanged,
            this, &MainWindow::onConflictWorkingTreeChanged);

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

    // Defer one event loop tick so the window is fully on-screen before
    // any modal dialog (e.g. LoginDialog) appears — that way the dialog
    // gets a sane geometry to centre against.
    QTimer::singleShot(0, this, [this] {
        // SessionController either silently re-signs us in via a saved
        // token, fires signedOut() in which case onSessionSignedOut
        // calls signIn(), or reports an auth failure via authError.
        session_->tryRestoreSession();
    });
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    settings_.setMainWindowGeometry(saveGeometry());
    settings_.setMainWindowState(saveState());
    QMainWindow::closeEvent(e);
}

// ----- Helpers -------------------------------------------------------------

void MainWindow::setStatus(const QString& text, int timeoutMs)
{
    if (timeoutMs > 0) statusBar()->showMessage(text, timeoutMs);
    else               statusMessage_->setText(text);
}

QString MainWindow::sshAllowedSignersPath() const
{
    // Read gpg.ssh.allowedSignersFile from the GLOBAL git config.
    // This is where `git config --global gpg.ssh.allowedSignersFile
    // ~/.ssh/allowed_signers` puts it. We don't read repo-local
    // config here because the verify operation runs per-commit
    // across potentially many repos; using global keeps the
    // behaviour consistent.
    //
    // libgit2 path: open default config, read the string. Returns
    // empty when unset, which is what SSH verifier expects to
    // fall back to check-novalidate (signature valid but trust
    // unknown — shown as Signed rather than Verified).
    git_config* cfgRaw = nullptr;
    if (git_config_open_default(&cfgRaw) != 0 || !cfgRaw) {
        return {};
    }
    QString result;
    git_buf buf = GIT_BUF_INIT_CONST(nullptr, 0);
    if (git_config_get_string_buf(&buf, cfgRaw,
            "gpg.ssh.allowedsignersfile") == 0) {
        // libgit2's config keys are case-insensitive but stored
        // lowercased internally. The value can contain ~ for the
        // home directory — expand it.
        QString raw = QString::fromUtf8(buf.ptr,
            static_cast<int>(buf.size)).trimmed();
        if (raw.startsWith(QLatin1String("~/"))) {
            raw = QStandardPaths::writableLocation(
                    QStandardPaths::HomeLocation) + raw.mid(1);
        }
        result = raw;
    }
    git_buf_dispose(&buf);
    git_config_free(cfgRaw);
    return result;
}

void MainWindow::setBusy(bool busy, const QString& label)
{
    progress_->setVisible(busy);
    if (busy) {
        // Reset to indeterminate (range 0,0 → animating "marquee" bar).
        // Concrete progress numbers will arrive via onWorkerProgress
        // for network ops; for local ops they don't come and we want
        // the marquee animation as the visual cue.
        progress_->setRange(0, 0);
        progress_->setValue(0);
        progress_->setFormat(QString());  // no text overlay yet
        if (!label.isEmpty()) setStatus(label);
    }
    // When the operation finishes, clear the permanent label too.
    // Otherwise the temporary "Loaded N repositories" message (shown
    // with a 4-second timeout via showMessage) eventually expires
    // and the status bar falls back to whatever permanent label was
    // last set — which is the stale "Loading repositories…" we
    // pushed when busy started. The user sees the status flip from
    // "Loaded 12 repositories." → "Loading repositories…" with no
    // actual loading happening, which looks like a stuck app.
    if (!busy) {
        statusMessage_->clear();
        // Also reset to indeterminate so the next busy phase starts
        // cleanly — otherwise the bar could briefly show the last
        // operation's stale 87%.
        progress_->setRange(0, 0);
        progress_->setFormat(QString());
    }
}

void MainWindow::onWorkerProgress(const QString& phase,
                                  qint64 current, qint64 total)
{
    // Network ops (clone/pull/push/fetch) report progress via this
    // signal. We only update if the progress bar is currently
    // visible — if it isn't, setBusy(false) already fired and this
    // is a tail event from a finished op that we should ignore.
    if (!progress_->isVisible()) return;

    // GitHandler emits phase as a fixed English string (it's called
    // from a libgit2 C callback that can't reach tr()). Translate
    // here on the GUI thread; fall back to the source if the phase
    // is something we don't recognise.
    QString displayPhase;
    if      (phase == QLatin1String("Receiving objects")) displayPhase = tr("Receiving objects");
    else if (phase == QLatin1String("Pushing objects"))   displayPhase = tr("Pushing objects");
    else                                                  displayPhase = phase;

    if (total > 0 && current <= total) {
        // libgit2 deals in object counts, not bytes — but counts are
        // what users care about ("87/523 objects"). Switch the bar
        // to determinate mode and reflect the ratio.
        // QProgressBar uses int range, so for very large counts
        // (huge repos) we scale to a percentage to avoid overflow.
        if (total <= INT_MAX) {
            progress_->setRange(0, static_cast<int>(total));
            progress_->setValue(static_cast<int>(current));
        } else {
            progress_->setRange(0, 100);
            progress_->setValue(static_cast<int>(current * 100 / total));
        }
        // Phase + ratio in the bar itself, e.g. "Receiving objects 87/523".
        progress_->setFormat(QStringLiteral("%1 %v/%m").arg(displayPhase));
    } else {
        // Total unknown — leave the bar marquee'ing but at least flash
        // the phase so the user knows something is happening.
        progress_->setRange(0, 0);
        progress_->setFormat(displayPhase);
    }
}

void MainWindow::rememberLocalPath(const QString& fullName, const QString& localPath)
{
    localPathByFullName_[fullName] = localPath;
    repoList_->setLocalPath(fullName, localPath);
}

bool MainWindow::ensureIdentity()
{
    if (settings_.hasIdentity()) return true;

    IdentityDialog dlg(settings_.authorName(), settings_.authorEmail(), this);
    dlg.setWindowTitle(tr("Set git author identity"));
    if (dlg.exec() != QDialog::Accepted) return false;

    settings_.setAuthorName (dlg.name());
    settings_.setAuthorEmail(dlg.email());
    pushIdentityToWidget();
    return true;
}

void MainWindow::pushIdentityToWidget()
{
    localDetail_->setIdentity(settings_.authorName(), settings_.authorEmail());
}

// ----- Auth lifecycle ------------------------------------------------------
//
// Most auth logic moved into SessionController. The slots below are
// the UI-level relays that translate controller signals into status
// bar updates, dialog prompts, and action enable/disable state.

// ----- Session relays ------------------------------------------------------
//
// SessionController emits these signals; MainWindow translates them
// into UI updates: window title, status bar, action enable/disable,
// repo list contents.

void MainWindow::onSessionSignedIn(const QString& username)
{
    setWindowTitle(QStringLiteral("%1 — %2")
                       .arg(QString::fromLatin1(kAppTitle), username));
    userLabel_->setText(tr("Signed in as %1").arg(username));

    // Tooltip explains the auth mechanism. OAuth tokens are session-
    // scoped (typically 1h before refresh would be needed), PATs are
    // long-lived (60-90 days typical). Showing this lets the user
    // understand why they might need to re-sign-in unexpectedly.
    const auto type = session_->tokenType();
    QString tip;
    switch (type) {
        case ghm::core::TokenType::Oauth:
            tip = tr("Authenticated via GitHub OAuth (device flow).\n"
                     "Tokens issued this way are tied to your GitHub "
                     "account and can be revoked from your GitHub "
                     "settings → Applications page.");
            break;
        case ghm::core::TokenType::Pat:
            tip = tr("Authenticated via Personal Access Token.\n"
                     "Manage this token at GitHub → Settings → "
                     "Developer settings → Personal access tokens.");
            break;
        case ghm::core::TokenType::Unknown:
            // Legacy token from before 0.27.0. Don't claim either
            // mechanism — the user knows what they typed.
            tip = tr("Authenticated (token type unknown — legacy "
                     "credential from an older version of this app).");
            break;
    }
    userLabel_->setToolTip(tip);

    refreshAction_->setEnabled(true);
    logoutAction_->setEnabled(true);
}

void MainWindow::onSessionRestored(const QString& username)
{
    // Same UI treatment as a fresh sign-in. The distinction matters
    // only inside the controller (different signal so we know whether
    // to fire LoginDialog if validation later fails).
    onSessionSignedIn(username);
}

void MainWindow::onSessionSignedOut()
{
    repoList_->setRepositories({});
    repoDetail_->showRepository({});
    userLabel_->clear();
    refreshAction_->setEnabled(false);
    logoutAction_->setEnabled(false);
    setStatus(tr("Signed out."), 4000);

    // No saved token + no in-progress login → prompt the user. We do
    // this here rather than in the controller so the controller stays
    // free of QtWidgets dependencies for future testability.
    if (!session_->isSignedIn()) {
        session_->signIn(this);
        // If the user dismissed the dialog without signing in, there's
        // nothing more we can do — exit cleanly.
        if (!session_->isSignedIn()) close();
    }
}

void MainWindow::onSessionAuthError(const QString& reason)
{
    QMessageBox::warning(this, tr("Authentication failed"),
        tr("GitHub rejected the stored token:\n\n%1\n\nPlease sign in again.")
            .arg(reason));
    // The signedOut() signal will follow and trigger the login prompt.
}

void MainWindow::onSessionNetworkError(const QString& msg)
{
    // If a publish flow is active, PublishController has its own
    // networkError subscription and will translate this into a
    // failed() signal we'll handle in onPublishFailed(). Don't
    // double up on the dialog here.
    if (publish_->isActive()) {
        return;
    }
    setBusy(false);
    QMessageBox::warning(this, tr("Network error"), msg);
}

void MainWindow::onSessionKeyringError(const QString& message)
{
    // Non-fatal — surface as a transient status bar message rather
    // than blocking the UI with a modal box. Most causes (locked
    // wallet, no D-Bus session) the user can fix and retry next launch.
    setStatus(tr("Keyring unavailable: %1").arg(message), 6000);
}

void MainWindow::onSessionBusy(const QString& message)
{
    setStatus(message);
}

void MainWindow::onSessionReposLoaded(
    const QList<ghm::github::Repository>& repos)
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

void MainWindow::requestSignOut()
{
    if (QMessageBox::question(this, tr("Sign out"),
            tr("Sign out of %1? Your local clones and folders will be left intact.")
                .arg(session_->username())) != QMessageBox::Yes) {
        return;
    }
    session_->signOut();
}

// ----- GitHub-clone flow ---------------------------------------------------

void MainWindow::onCloneRequested(const ghm::github::Repository& repo)
{
    if (!repo.isValid()) return;
    if (clone_->isBusy()) {
        QMessageBox::information(this, tr("Already cloning"),
            tr("Another clone is in progress — please wait for it to finish."));
        return;
    }
    CloneDialog dlg(repo, settings_.defaultCloneDirectory(), this);
    // Prefill the SSH toggle from the user's saved preference. The
    // user can still flip it per-clone — useful when most repos go
    // over SSH but one happens to need HTTPS (or vice versa).
    dlg.setSshDefault(settings_.clonePreferSsh());
    if (dlg.exec() != QDialog::Accepted) return;

    // Apply the dialog's SSH choice for THIS clone only — we do NOT
    // write back to settings, because the dialog choice is per-clone,
    // not a profile change.
    auto effective = repo;
    ghm::git::SshCredentials sshCreds;
    if (dlg.useSsh()) {
        effective.cloneUrl = ghm::github::httpsToSsh(repo.cloneUrl);

        // If the user wants an explicit key (e.g. encrypted key not in
        // ssh-agent, or one of several keys), pop the SshKeyDialog to
        // collect path + passphrase BEFORE handing off to the worker.
        // Collecting on the GUI thread keeps libgit2's worker-thread
        // credCb prompt-free: it just reads the already-set bytes
        // from CallbackCtx.
        //
        // If they cancel the SshKeyDialog, we abort the whole clone —
        // they explicitly asked for explicit-key, so silently falling
        // back to ssh-agent would surprise them.
        if (dlg.useExplicitKey()) {
            SshKeyDialog keyDlg(this);
            if (keyDlg.exec() != QDialog::Accepted) {
                setStatus(tr("Clone cancelled."), 3000);
                return;
            }
            sshCreds.keyPath    = keyDlg.keyPath();
            sshCreds.passphrase = keyDlg.passphrase();
            // Public key path is empty — libssh2 derives it from the
            // private key for OpenSSH-format keys, and for PEM keys
            // the public material is reconstructable from the private.
        }
    }
    // Hand the resolved target path + creds to the controller. It does
    // the target-exists check, optimistically updates the default-clone
    // dir, and kicks off the worker.
    clone_->startClone(effective, dlg.targetPath(), session_->token(), sshCreds);
}

void MainWindow::onOpenLocallyRequested(const ghm::github::Repository& repo)
{
    if (!repo.isValid()) return;
    const QString picked = QFileDialog::getExistingDirectory(this,
        tr("Choose existing local clone of %1").arg(repo.fullName),
        settings_.defaultCloneDirectory());
    if (picked.isEmpty()) return;
    // Controller validates that picked contains a .git directory and
    // emits opened() or failed(). MainWindow updates UI in the slots.
    clone_->openExisting(repo, picked);
}

void MainWindow::onPullRequested(const QString& localPath)
{
    if (localPath.isEmpty()) return;
    setBusy(true, tr("Pulling %1…").arg(QFileInfo(localPath).fileName()));
    worker_->pull(localPath, session_->token());
}

void MainWindow::onPushRequested(const QString& localPath)
{
    if (localPath.isEmpty()) return;
    setBusy(true, tr("Pushing %1…").arg(QFileInfo(localPath).fileName()));
    worker_->push(localPath, session_->token());
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

void MainWindow::onCloneStarted(const ghm::github::Repository& repo,
                                 const QString& localPath)
{
    setBusy(true, tr("Cloning %1…").arg(repo.fullName));
    // Optimistically remember the mapping so the sidebar's "has local
    // copy" badge appears immediately. We roll back in onCloneFailed
    // if the clone doesn't finish.
    localPathByFullName_.insert(repo.fullName, localPath);
}

void MainWindow::onCloneSucceeded(const ghm::github::Repository& repo,
                                   const QString& localPath)
{
    setBusy(false);
    setStatus(tr("Cloned to %1").arg(localPath), 5000);

    // Update the sidebar's per-row badge with the resolved path.
    repoList_->setLocalPath(repo.fullName, localPath);

    // If the user is currently looking at this repo's detail view,
    // refresh it so the local-path field populates and the branches
    // load. Otherwise leave the view alone — switching it without
    // user intent would be jarring.
    auto current = repoList_->currentRepository();
    if (current.fullName == repo.fullName) {
        current.localPath = localPath;
        repoDetail_->showRepository(current);
        worker_->listBranches(localPath);
    }
}

void MainWindow::onOpenSucceeded(const ghm::github::Repository& repo,
                                  const QString& localPath)
{
    rememberLocalPath(repo.fullName, localPath);
    auto updated = repo;
    updated.localPath = localPath;
    repoDetail_->showRepository(updated);
    worker_->listBranches(localPath);
}

void MainWindow::onCloneFailed(const ghm::github::Repository& repo,
                                const QString& localPath,
                                const QString& title,
                                const QString& detail)
{
    setBusy(false);
    // Roll back the optimistic insert. If the failure happened before
    // startClone() ever did the insert (e.g. target-exists check),
    // this loop is a no-op.
    for (auto it = localPathByFullName_.begin();
              it != localPathByFullName_.end(); ) {
        if (it.value() == localPath && it.key() == repo.fullName) {
            it = localPathByFullName_.erase(it);
        } else {
            ++it;
        }
    }
    QMessageBox::critical(this, title, detail);
}

void MainWindow::onDefaultCloneDirectoryChanged(const QString& newDirectory)
{
    settings_.setDefaultCloneDirectory(newDirectory);
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
    // PublishController has its own pushFinished subscription. If the
    // push was part of a publish flow, the controller handles UI
    // feedback (success status, error dialog) — we stay out of the way.
    if (publish_->isActive() && publish_->activePath() == localPath) {
        return;
    }

    setBusy(false);
    localDetail_->setBusy(false);

    if (!ok) {
        QMessageBox::warning(this, tr("Push failed"), error);
    } else {
        setStatus(tr("Push complete."), 5000);
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

// ----- Local folder workflow ----------------------------------------------

void MainWindow::onAddLocalFolderClicked()
{
    const QString picked = QFileDialog::getExistingDirectory(this,
        tr("Add local folder"),
        settings_.defaultCloneDirectory(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (picked.isEmpty()) return;

    const QString abs = QDir(picked).absolutePath();
    if (!settings_.addLocalFolder(abs)) {
        // Already in the list — just select it.
        repoList_->setLocalFolders(settings_.localFolders());
        repoList_->selectLocalFolder(abs);
        setStatus(tr("Folder is already in the sidebar."), 4000);
        return;
    }
    repoList_->setLocalFolders(settings_.localFolders());
    repoList_->selectLocalFolder(abs);  // triggers onLocalFolderActivated
}

void MainWindow::onLocalFolderActivated(const QString& path)
{
    if (path.isEmpty()) return;
    activeLocalPath_ = path;
    // Switching folder: drop any in-flight conflict-resolution state.
    // Controller closes its dialog and clears its entries cache.
    conflict_->reset();
    detailStack_->setCurrentWidget(localDetail_);
    localDetail_->setFolder(path);
    pushIdentityToWidget();
    // Tell the workspace controller which folder it's now responsible
    // for. Worker callbacks for other paths are filtered out inside
    // the controller.
    workspace_->setActivePath(path);
    workspace_->refreshActiveFolder();
    // Fetch the submodule list eagerly. Cheap (no network) — just
    // reads .gitmodules and runs git_submodule_foreach. The result
    // populates the Submodules tab (or its "no submodules" state)
    // so the user doesn't have to click Refresh themselves.
    worker_->listSubmodules(path);
    // Repository menu becomes useful now that we have a folder.
    if (fetchAction_)      fetchAction_     ->setEnabled(true);
    if (stashSaveAction_)  stashSaveAction_ ->setEnabled(true);
    if (stashListAction_)  stashListAction_ ->setEnabled(true);
    if (tagsAction_)       tagsAction_      ->setEnabled(true);
    if (reflogAction_)     reflogAction_    ->setEnabled(true);
    if (undoCommitAction_) undoCommitAction_->setEnabled(true);
    if (openFolderAction_) openFolderAction_->setEnabled(true);
}

void MainWindow::onRemoveLocalFolderRequested(const QString& path)
{
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, tr("Remove from sidebar"),
            tr("Remove '%1' from the sidebar? The folder on disk is not affected.")
                .arg(QFileInfo(path).fileName())) != QMessageBox::Yes) {
        return;
    }
    settings_.removeLocalFolder(path);
    repoList_->setLocalFolders(settings_.localFolders());
    if (activeLocalPath_ == path) {
        activeLocalPath_.clear();
        detailStack_->setCurrentWidget(repoDetail_);
    }
}

void MainWindow::onChangeVisibilityRequested(
    const ghm::github::Repository& repo, bool makePrivate)
{
    if (!repo.isValid()) return;
    if (!session_->client()) return;

    // Confirm with strong warning text. Two distinct scenarios:
    //
    //  * private → public: "your code becomes visible to the world".
    //    Big deal if there are credentials, IP, or unfinished work.
    //  * public → private: "stars/watchers erased, forks detached".
    //    Less common but still worth a stop-and-think.
    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Warning);
    if (makePrivate) {
        confirm.setWindowTitle(tr("Make repository private?"));
        confirm.setText(tr("Make <b>%1</b> private?").arg(repo.fullName));
        confirm.setInformativeText(tr(
            "Switching from public to private has these effects:<br>"
            "• Stars and watchers will be erased<br>"
            "• Public forks will be detached and remain public<br>"
            "• GitHub Pages, if any, will be unpublished<br>"
            "• GitHub will no longer include the repo in the "
            "Archive Program<br><br>"
            "The repository content and history are preserved."));
    } else {
        confirm.setWindowTitle(tr("Make repository public?"));
        confirm.setText(tr("Make <b>%1</b> public?").arg(repo.fullName));
        confirm.setInformativeText(tr(
            "Switching from private to public has these effects:<br>"
            "• All commit history will be visible to anyone on the "
            "internet<br>"
            "• All issues, pull requests, and discussions become "
            "publicly visible<br>"
            "• Anyone can fork the repository<br>"
            "• Actions history and logs will be visible to everyone"
            "<br><br><b>Before confirming:</b> make sure there are "
            "no credentials, API keys, or other sensitive material "
            "in your code or commit history. Use BFG Repo-Cleaner "
            "or git filter-branch to scrub if needed."));
    }
    confirm.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.button(QMessageBox::Yes)->setText(makePrivate
        ? tr("Make private") : tr("Make public"));
    if (confirm.exec() != QMessageBox::Yes) return;

    setStatus(tr("Changing visibility of %1 to %2…")
                .arg(repo.fullName,
                     makePrivate ? tr("private") : tr("public")));
    session_->client()->updateRepositoryVisibility(repo.fullName, makePrivate);
}

void MainWindow::onVisibilityChanged(const ghm::github::Repository& updated)
{
    if (!updated.isValid()) return;

    // Patch the local cache so the sidebar's lock icon (or
    // whatever indicator the styling uses) flips right away.
    // reposCache_ holds the canonical list we feed back to the
    // sidebar; replacing the matching entry by fullName keeps
    // the rest of the order intact.
    bool patched = false;
    for (auto& r : reposCache_) {
        if (r.fullName == updated.fullName) {
            // Preserve localPath which the GitHub PATCH response
            // doesn't know about — it's our client-side annotation.
            const QString preservedLocal = r.localPath;
            r = updated;
            r.localPath = preservedLocal;
            patched = true;
            break;
        }
    }
    if (patched) {
        repoList_->setRepositories(reposCache_);
    }

    setStatus(tr("Visibility for %1 is now %2.").arg(
                updated.fullName,
                updated.isPrivate ? tr("private") : tr("public")),
              5000);
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
    if (!session_->isSignedIn()) {
        QMessageBox::information(this, tr("Sign in required"),
            tr("Push uses your GitHub personal access token. Please sign in first."));
        session_->signIn(this);
        if (!session_->isSignedIn()) return;
    }
    localDetail_->setBusy(true);
    setBusy(true, tr("Pushing %1 → %2…").arg(branch, remoteName));
    worker_->pushTo(path, remoteName, branch, setUpstream, session_->token());
}

// ----- Workspace controller feedback ---------------------------------------
//
// LocalWorkspaceController does the worker delegation; these slots
// translate its higher-level signals into UI feedback (status bar,
// error dialogs, view refresh) that requires direct widget access.

void MainWindow::onWorkspaceStateRefreshed(
    const QString& path,
    bool                                       isRepository,
    const QString&                             branch,
    const std::vector<ghm::git::StatusEntry>&  entries,
    const std::vector<ghm::git::RemoteInfo>&   remotes)
{
    // The controller already filtered by activePath_, so we don't
    // need a second check here. We do still need it though — the
    // GitHub-clone view is also visible via detailStack_ and stale
    // entries shouldn't bleed into local detail.
    if (path != activeLocalPath_) return;
    localDetail_->setLocalState(isRepository, branch, entries, remotes);

    // Surface in-progress merge state via the conflict banner.
    int conflictCount = 0;
    for (const auto& e : entries) {
        if (e.isConflicted) ++conflictCount;
    }
    localDetail_->setConflictCount(conflictCount);

    if (!isRepository) {
        setStatus(tr("Folder is not a Git repository — initialize to start."), 4000);
        return;
    }
    QString summary;
    if      (conflictCount > 0)   summary = tr("%1 conflicted file(s)").arg(conflictCount);
    else if (entries.empty())     summary = tr("Working tree clean");
    else                          summary = tr("%1 changed file(s)").arg(entries.size());
    setStatus(tr("On %1 — %2").arg(branch.isEmpty() ? QStringLiteral("(unborn)") : branch,
                                   summary), 4000);
}

void MainWindow::onWorkspaceStageOpFinished(bool ok, const QString& path,
                                            const QString& error)
{
    Q_UNUSED(path);
    if (!ok) QMessageBox::warning(this, tr("Stage failed"), error);
}

void MainWindow::onWorkspaceUnstageOpFinished(bool ok, const QString& path,
                                              const QString& error)
{
    Q_UNUSED(path);
    if (!ok) QMessageBox::warning(this, tr("Unstage failed"), error);
}

void MainWindow::onWorkspaceCommitFinished(bool ok, const QString& path,
                                           const QString& sha, const QString& error)
{
    Q_UNUSED(path);
    setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, tr("Commit failed"), error);
        return;
    }
    setStatus(tr("Committed %1").arg(sha.left(7)), 5000);
}

void MainWindow::onWorkspaceInitFinished(bool ok, const QString& path,
                                         const QString& error)
{
    setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, tr("Initialize failed"), error);
        return;
    }
    setStatus(tr("Initialized empty repository in %1").arg(path), 5000);
}

void MainWindow::onWorkspaceHistoryReady(
    const QString& path,
    const std::vector<ghm::git::CommitInfo>& commits,
    bool isAppend)
{
    if (path != activeLocalPath_) return;
    if (isAppend) localDetail_->appendHistory(commits);
    else          localDetail_->setHistory(commits);
}

void MainWindow::onWorkspaceFileDiffReady(
    const QString& path,
    const QString& repoRelPath,
    const ghm::git::FileDiff& diff,
    const QString& error)
{
    if (path != activeLocalPath_) return;
    localDetail_->setFileDiff(repoRelPath, diff, error);
}

void MainWindow::onWorkspaceCommitDiffReady(
    const QString& path,
    const QString& sha,
    const std::vector<ghm::git::FileDiff>& files,
    const QString& error)
{
    if (path != activeLocalPath_) return;
    localDetail_->setCommitDiff(sha, files, error);
}

void MainWindow::onWorkspaceBranchInfosReady(
    const QString& path,
    const std::vector<ghm::git::BranchInfo>& infos)
{
    if (path != activeLocalPath_) return;
    localDetail_->setBranches(infos);
}

void MainWindow::onWorkspaceBranchSwitched(bool ok, const QString& path,
                                           const QString& branch,
                                           const QString& error)
{
    Q_UNUSED(path);
    setBusy(false);
    if (!ok) QMessageBox::warning(this, tr("Branch switch failed"), error);
    else     setStatus(tr("Now on %1").arg(branch), 4000);
}

void MainWindow::onWorkspaceBranchCreated(bool ok, const QString& path,
                                          const QString& branch,
                                          const QString& error)
{
    Q_UNUSED(path);
    setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, tr("Create branch failed"), error);
    } else {
        setStatus(tr("Created branch %1.").arg(branch), 4000);
    }
}

void MainWindow::onWorkspaceBranchDeleted(bool ok, const QString& path,
                                          const QString& branch,
                                          const QString& error)
{
    Q_UNUSED(path);
    setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, tr("Delete branch failed"), error);
    } else {
        setStatus(tr("Deleted branch %1.").arg(branch), 4000);
    }
}

void MainWindow::onWorkspaceBranchRenamed(bool ok, const QString& path,
                                          const QString& oldName,
                                          const QString& newName,
                                          const QString& error)
{
    Q_UNUSED(path);
    setBusy(false);
    if (!ok) {
        QMessageBox::warning(this, tr("Rename branch failed"), error);
    } else {
        setStatus(tr("Renamed %1 → %2.").arg(oldName, newName), 4000);
    }
}

// ----- Dialog prompts requested by LocalWorkspaceController ---------------

void MainWindow::onWorkspaceIdentityRequiredForCommit(const QString& path,
                                                       const QString& pendingMessage)
{
    if (!ensureIdentity()) {
        setStatus(tr("Commit cancelled — author identity is required."), 5000);
        return;
    }
    // ensureIdentity persisted the new identity into Settings; tell
    // the controller to resume the commit it had pending.
    workspace_->commitWithKnownIdentity(path, pendingMessage);
}

void MainWindow::onWorkspaceBranchCreateDialogRequested(const QString& path,
                                                         const QStringList& existing)
{
    // We need the current branch name for the dialog's "branch off"
    // hint. Fetch it synchronously — this is a UI thread call right
    // before opening a modal, so latency doesn't matter.
    ghm::git::GitResult err;
    const QString currentName = worker_->handler().currentBranch(path, &err);

    CreateBranchDialog dlg(currentName, existing, this);
    if (dlg.exec() != QDialog::Accepted) return;

    // Controller emits busy() with the proper status message.
    workspace_->createBranchAccepted(path, dlg.name(), dlg.checkoutAfter());
}

void MainWindow::onWorkspaceBranchForceDeleteConfirmation(const QString& path,
                                                           const QString& branch,
                                                           const QString& reason)
{
    const auto reply = QMessageBox::warning(this,
        tr("Branch is not merged"),
        tr("<b>%1</b> contains commits that aren't reachable from "
           "your current branch.<br><br>%2<br><br>"
           "Force-delete anyway? <b>The unique commits will be lost</b> "
           "unless they're referenced from another branch.")
            .arg(branch.toHtmlEscaped(), reason.toHtmlEscaped()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) {
        // User backed out — clear the busy state we set for the
        // first-pass delete, and refresh so the widget is in sync.
        setBusy(false);
        localDetail_->setBusy(false);
        workspace_->refreshActiveFolder();
        return;
    }
    workspace_->forceDeleteConfirmed(path, branch);
}

void MainWindow::onWorkspaceBranchRenameDialogRequested(const QString& path,
                                                         const QString& oldName,
                                                         const QStringList& existing)
{
    bool accepted = false;
    QString newName = QInputDialog::getText(
        this, tr("Rename branch"),
        tr("Rename <b>%1</b> to:").arg(oldName.toHtmlEscaped()),
        QLineEdit::Normal, oldName, &accepted);
    if (!accepted) return;
    newName = newName.trimmed();
    if (newName == oldName) return;

    QString why;
    if (!ghm::ui::isValidBranchName(newName, &why)) {
        QMessageBox::warning(this, tr("Rename branch"),
            tr("Cannot rename branch:\n\n%1").arg(why));
        return;
    }
    if (existing.contains(newName)) {
        QMessageBox::warning(this, tr("Rename branch"),
            tr("A branch named '%1' already exists.").arg(newName));
        return;
    }

    workspace_->renameBranchAccepted(path, oldName, newName);
}

// ----- Branch delete (kept here — needs a confirmation dialog) ------------

void MainWindow::onLocalBranchDeleteRequested(const QString& path,
                                              const QString& branch)
{
    if (path.isEmpty() || branch.isEmpty()) return;

    // First-pass non-force delete. The controller handles the actual
    // worker call and re-prompts (via branchForceDeleteConfirmation)
    // if libgit2 reports "not merged".
    const auto reply = QMessageBox::question(this, tr("Delete branch"),
        tr("Delete the local branch <b>%1</b>?<br><br>"
           "This is a local-only operation; nothing on GitHub is affected.")
            .arg(branch.toHtmlEscaped()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    workspace_->onBranchDeleteRequested(path, branch);
}

// ----- Stash --------------------------------------------------------------

void MainWindow::onStashSaveRequested()
{
    if (activeLocalPath_.isEmpty()) return;

    // Stash needs an author identity (libgit2 requires a signature
    // for the stash commit). Surface a friendlier prompt than letting
    // the worker fail later.
    if (settings_.authorName().isEmpty() ||
        settings_.authorEmail().isEmpty()) {
        QMessageBox::information(this, tr("Stash changes"),
            tr("Stashing requires a configured author identity. "
               "Set your name and email first (the prompt appears the "
               "next time you commit, or click Edit next to your "
               "identity in the local-folder header)."));
        return;
    }

    StashSaveDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString path = activeLocalPath_;
    setBusy(true, tr("Stashing changes…"));
    localDetail_->setBusy(true);
    worker_->stashSave(path, dlg.message(),
                       dlg.includeUntracked(), dlg.keepIndex(),
                       settings_.authorName(), settings_.authorEmail());
}

void MainWindow::onStashListRequested()
{
    if (activeLocalPath_.isEmpty()) return;

    if (!stashListDialog_) {
        stashListDialog_ = new StashListDialog(this);
        connect(stashListDialog_, &StashListDialog::applyRequested,
                this, &MainWindow::onStashApplyRequested);
        connect(stashListDialog_, &StashListDialog::popRequested,
                this, &MainWindow::onStashPopRequested);
        connect(stashListDialog_, &StashListDialog::dropRequested,
                this, &MainWindow::onStashDropRequested);
    }
    worker_->stashList(activeLocalPath_);
    stashListDialog_->show();
    stashListDialog_->raise();
    stashListDialog_->activateWindow();
}

void MainWindow::onStashApplyRequested(int index)
{
    if (activeLocalPath_.isEmpty()) return;
    if (stashListDialog_) stashListDialog_->setBusy(true);
    setStatus(tr("Applying stash@{%1}…").arg(index));
    worker_->stashApply(activeLocalPath_, index);
}

void MainWindow::onStashPopRequested(int index)
{
    if (activeLocalPath_.isEmpty()) return;
    if (stashListDialog_) stashListDialog_->setBusy(true);
    setStatus(tr("Popping stash@{%1}…").arg(index));
    worker_->stashPop(activeLocalPath_, index);
}

void MainWindow::onStashDropRequested(int index)
{
    if (activeLocalPath_.isEmpty()) return;
    if (stashListDialog_) stashListDialog_->setBusy(true);
    setStatus(tr("Dropping stash@{%1}…").arg(index));
    worker_->stashDrop(activeLocalPath_, index);
}

void MainWindow::onStashOpFinished(bool ok, const QString& path,
                                   const QString& operation,
                                   const QString& error)
{
    if (path != activeLocalPath_) return;
    setBusy(false);
    localDetail_->setBusy(false);
    if (stashListDialog_) stashListDialog_->setBusy(false);

    if (!ok) {
        QMessageBox::warning(this, tr("Stash %1 failed").arg(operation), error);
    } else {
        QString msg;
        if      (operation == QLatin1String("save"))  msg = tr("Stashed.");
        else if (operation == QLatin1String("apply")) msg = tr("Stash applied.");
        else if (operation == QLatin1String("pop"))   msg = tr("Stash popped.");
        else if (operation == QLatin1String("drop")) msg = tr("Stash dropped.");
        if (!msg.isEmpty()) setStatus(msg, 4000);
    }

    worker_->refreshLocalState(path);
    if (stashListDialog_ && stashListDialog_->isVisible()) {
        worker_->stashList(path);
    }
}

void MainWindow::onStashListReady(const QString& path,
                                  const std::vector<ghm::git::StashEntry>& entries)
{
    if (path != activeLocalPath_) return;
    if (stashListDialog_) stashListDialog_->setEntries(entries);
}

// ----- Tags ---------------------------------------------------------------

void MainWindow::onTagsRequested()
{
    if (activeLocalPath_.isEmpty()) return;

    if (!tagsDialog_) {
        tagsDialog_ = new TagsDialog(this);
        connect(tagsDialog_, &TagsDialog::createRequested,
                this, &MainWindow::onTagCreateRequested);
        connect(tagsDialog_, &TagsDialog::deleteRequested,
                this, &MainWindow::onTagDeleteRequested);
    }
    if (settings_.authorName().isEmpty() ||
        settings_.authorEmail().isEmpty()) {
        tagsDialog_->setIdentityWarning(
            tr("⚠ No author identity configured. You can still create "
               "lightweight tags (empty message), but annotated tags "
               "require setting your name and email first."));
    } else {
        tagsDialog_->setIdentityWarning(QString());
    }

    worker_->listTags(activeLocalPath_);
    tagsDialog_->show();
    tagsDialog_->raise();
    tagsDialog_->activateWindow();
}

void MainWindow::onTagCreateRequested(const QString& name, const QString& message)
{
    if (activeLocalPath_.isEmpty()) return;
    if (tagsDialog_) tagsDialog_->setBusy(true);
    setStatus(tr("Creating tag %1…").arg(name));

    // Build SigningConfig from settings — same logic as commit flow.
    // Lightweight tags (empty message) silently drop the signing
    // config in the handler, so this is harmless for them too.
    ghm::git::SigningConfig sigCfg;
    const auto mode = settings_.signingMode();
    if (mode == ghm::core::Settings::SigningMode::Gpg) {
        sigCfg.mode = ghm::git::SigningConfig::Mode::Gpg;
        sigCfg.key  = settings_.signingKey();
    } else if (mode == ghm::core::Settings::SigningMode::Ssh) {
        sigCfg.mode = ghm::git::SigningConfig::Mode::Ssh;
        sigCfg.key  = settings_.signingKey();
    }

    worker_->createTag(activeLocalPath_, name, message,
                       settings_.authorName(), settings_.authorEmail(),
                       sigCfg);
}

void MainWindow::onTagDeleteRequested(const QString& name)
{
    if (activeLocalPath_.isEmpty()) return;
    if (tagsDialog_) tagsDialog_->setBusy(true);
    setStatus(tr("Deleting tag %1…").arg(name));
    worker_->deleteTag(activeLocalPath_, name);
}

void MainWindow::onTagOpFinished(bool ok, const QString& path,
                                 const QString& operation,
                                 const QString& name,
                                 const QString& error)
{
    if (path != activeLocalPath_) return;
    if (tagsDialog_) tagsDialog_->setBusy(false);

    if (!ok) {
        const QString title = (operation == QLatin1String("create"))
            ? tr("Create tag failed")
            : tr("Delete tag failed");
        QMessageBox::warning(this, title, error);
    } else {
        if (operation == QLatin1String("create")) {
            setStatus(tr("Created tag %1.").arg(name), 4000);
        } else {
            setStatus(tr("Deleted tag %1.").arg(name), 4000);
        }
    }
    if (tagsDialog_ && tagsDialog_->isVisible()) {
        worker_->listTags(path);
    }
}

void MainWindow::onTagsReady(const QString& path,
                             const std::vector<ghm::git::TagInfo>& tags)
{
    if (path != activeLocalPath_) return;
    if (tagsDialog_) tagsDialog_->setTags(tags);
}

// ----- Submodules ----------------------------------------------------------

void MainWindow::onSubmoduleOpFinished(bool ok, const QString& path,
                                        const QString& operation,
                                        const QString& name,
                                        const QString& error)
{
    if (path != activeLocalPath_) return;

    if (!ok) {
        // Show a dialog rather than just status, because submodule
        // errors (auth failed, network down, host key unknown) tend
        // to be multi-line and informative — they need to be read
        // carefully, not glanced at in a status bar.
        QString title;
        if (operation == QLatin1String("init+update")) {
            title = tr("Submodule init failed");
        } else if (operation == QLatin1String("update")) {
            title = tr("Submodule update failed");
        } else if (operation == QLatin1String("add")) {
            title = tr("Add submodule failed");
        } else if (operation == QLatin1String("remove")) {
            title = tr("Remove submodule failed");
        } else {
            title = tr("Submodule sync failed");
        }
        QMessageBox::warning(this, title,
            tr("Submodule '%1':\n\n%2").arg(name, error));
    } else {
        if (operation == QLatin1String("init+update")) {
            setStatus(tr("Submodule '%1' initialized.").arg(name), 4000);
        } else if (operation == QLatin1String("update")) {
            setStatus(tr("Submodule '%1' updated.").arg(name), 4000);
        } else if (operation == QLatin1String("add")) {
            // For "add" the name is actually the subPath. Worded as
            // such so the user sees what they recognise.
            setStatus(tr("Submodule added at '%1'. "
                          "Don't forget to commit the .gitmodules "
                          "change.").arg(name), 6000);
        } else if (operation == QLatin1String("remove")) {
            setStatus(tr("Submodule '%1' removed. "
                          "Don't forget to commit the deletion.").arg(name), 6000);
        } else {
            setStatus(tr("Submodule '%1' URL synced.").arg(name), 4000);
        }
    }
    // Re-list to refresh the status column — easier than trying to
    // surgically update just the affected row. Cheap (no network).
    worker_->listSubmodules(path);

    // For add/remove we also want to refresh the working-tree status —
    // those ops produce uncommitted changes (.gitmodules diff, staged
    // gitlink delete) that should appear in the Changes tab right away.
    if (operation == QLatin1String("add") ||
        operation == QLatin1String("remove")) {
        worker_->refreshStatus(path);
    }
}

void MainWindow::onSubmoduleAddRequested(const QString& path)
{
    if (path != activeLocalPath_) return;

    AddSubmoduleDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString url     = dlg.url();
    const QString subPath = dlg.subPath();

    // If the user wants an explicit SSH key, prompt for it now (on
    // the GUI thread, before the worker is dispatched) — same pattern
    // as the top-level CloneDialog flow from 0.20.0.
    ghm::git::SshCredentials sshCreds;
    if (dlg.useExplicitKey()) {
        SshKeyDialog keyDlg(this);
        if (keyDlg.exec() != QDialog::Accepted) {
            setStatus(tr("Add submodule cancelled."), 3000);
            return;
        }
        sshCreds.keyPath    = keyDlg.keyPath();
        sshCreds.passphrase = keyDlg.passphrase();
    }

    setStatus(tr("Adding submodule at '%1'…").arg(subPath));
    worker_->addSubmodule(path, url, subPath,
                          session_->token(), sshCreds);
}

void MainWindow::onSubmoduleRemoveRequested(const QString& path,
                                             const QString& name)
{
    if (path != activeLocalPath_) return;

    // Confirmation dialog — destructive op, no undo. We list what
    // exactly will happen so the user can decide informedly. Default
    // button is Cancel (the safer choice).
    QMessageBox confirm(this);
    confirm.setWindowTitle(tr("Remove submodule?"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(tr("Remove submodule <b>%1</b>?").arg(name));
    confirm.setInformativeText(tr(
        "This will:\n"
        "  • Delete the submodule's working directory\n"
        "  • Delete .git/modules/%1/\n"
        "  • Remove the entry from .gitmodules and .git/config\n"
        "  • Stage the deletion of the gitlink in the parent repo\n\n"
        "You'll need to commit the resulting changes afterwards. "
        "This cannot be undone via this app — recovery requires the "
        "submodule's URL and a fresh clone.").arg(name));
    confirm.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    confirm.setDefaultButton(QMessageBox::Cancel);
    // Be explicit about which button means "really remove" — Yes is
    // ambiguous on first glance for destructive ops.
    confirm.button(QMessageBox::Yes)->setText(tr("Remove"));
    if (confirm.exec() != QMessageBox::Yes) return;

    setStatus(tr("Removing submodule '%1'…").arg(name));
    worker_->removeSubmodule(path, name);
}

// ----- Fetch ----------------------------------------------------------------

void MainWindow::onFetchRequested()
{
    if (activeLocalPath_.isEmpty()) return;

    // Fetch needs a remote name. We default to "origin" because that's
    // what 99% of repos have. Could be made configurable later by
    // listing remotes and asking the user — but for now keeping it
    // one-click is more valuable than flexible.
    const QString remoteName = QStringLiteral("origin");

    // Pass the GitHub PAT for HTTPS auth. If the user isn't signed in
    // we still try (public repos work without auth); on failure the
    // error message will explain why.
    const QString pat = session_->isSignedIn() ? session_->token() : QString();

    setBusy(true, tr("Fetching from %1…").arg(remoteName));
    localDetail_->setBusy(true);
    worker_->fetchRemote(activeLocalPath_, remoteName, pat);
}

void MainWindow::onFetchFinished(bool ok, const QString& path,
                                 const QString& remoteName,
                                 const QString& error)
{
    if (path != activeLocalPath_) return;
    setBusy(false);
    localDetail_->setBusy(false);

    if (!ok) {
        QMessageBox::warning(this, tr("Fetch failed"), error);
        return;
    }
    setStatus(tr("Fetched from %1.").arg(remoteName), 4000);
    // Refresh local state so updated ahead/behind counts on branches
    // are reflected in the popup.
    workspace_->refreshActiveFolder();
}

// ----- Undo last commit -----------------------------------------------------

void MainWindow::onUndoLastCommitRequested()
{
    if (activeLocalPath_.isEmpty()) return;

    // Confirmation: undoing is recoverable (the changes come back as
    // staged) but it does rewrite the local branch's history, which
    // can be confusing if the commit was already pushed. Make the
    // user pause and read.
    const auto reply = QMessageBox::question(this, tr("Undo last commit"),
        tr("Undo your most recent commit?<br><br>"
           "<b>What happens:</b> the commit's changes are kept in the "
           "working tree and re-staged so you can edit and re-commit.<br><br>"
           "<b>If you've already pushed this commit</b> to a shared remote, "
           "your next push will be rejected unless you force-push — which "
           "rewrites history for everyone else. Make sure you know what "
           "you're doing first.<br><br>"
           "Equivalent to: <code>git reset --soft HEAD~1</code>"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    setBusy(true, tr("Undoing last commit…"));
    localDetail_->setBusy(true);
    worker_->undoLastCommit(activeLocalPath_);
}

void MainWindow::onUndoLastCommitFinished(bool ok, const QString& path,
                                          const QString& error)
{
    if (path != activeLocalPath_) return;
    setBusy(false);
    localDetail_->setBusy(false);

    if (!ok) {
        QMessageBox::warning(this, tr("Undo failed"), error);
        return;
    }
    setStatus(tr("Last commit undone — its changes are now staged."), 6000);
    // Reload status, history, and branch infos. The history view in
    // particular will show a different HEAD, and the staged-files
    // section gets repopulated.
    workspace_->refreshActiveFolder();
    worker_->loadHistory(path, /*maxCount*/ 200);
}

// ----- Reflog ----------------------------------------------------------

void MainWindow::onReflogRequested()
{
    if (activeLocalPath_.isEmpty()) return;

    if (!reflogDialog_) {
        reflogDialog_ = new ReflogDialog(this);
        connect(reflogDialog_, &ReflogDialog::restoreRequested,
                this, &MainWindow::onReflogRestoreRequested);
        connect(reflogDialog_, &ReflogDialog::refreshRequested,
                this, &MainWindow::onReflogRequested);
    }
    // Refresh contents every time it opens — reflog grows whenever
    // HEAD moves, so a cached snapshot would go stale fast. The
    // worker calls back into onReflogReady.
    setStatus(tr("Loading reflog…"));
    worker_->loadReflog(activeLocalPath_, /*maxCount*/ 200);
    reflogDialog_->show();
    reflogDialog_->raise();
    reflogDialog_->activateWindow();
}

void MainWindow::onReflogReady(const QString& path,
                               const std::vector<ghm::git::ReflogEntry>& entries)
{
    if (path != activeLocalPath_) return;
    if (!reflogDialog_) return;  // user closed before result arrived
    reflogDialog_->setEntries(entries);
    setStatus(tr("Reflog: %1 entries.").arg(entries.size()), 3000);
}

void MainWindow::onReflogRestoreRequested(const QString& sha)
{
    if (activeLocalPath_.isEmpty() || sha.isEmpty()) return;
    if (reflogDialog_) reflogDialog_->setBusy(true);
    setBusy(true, tr("Restoring HEAD to %1…").arg(sha.left(7)));
    worker_->softResetTo(activeLocalPath_, sha);
}

void MainWindow::onSoftResetFinished(bool ok, const QString& path,
                                     const QString& sha, const QString& error)
{
    if (path != activeLocalPath_) return;
    setBusy(false);
    if (reflogDialog_) reflogDialog_->setBusy(false);

    if (!ok) {
        QMessageBox::warning(this, tr("Restore failed"), error);
        return;
    }
    setStatus(tr("HEAD restored to %1 — abandoned commits' changes are staged.")
                  .arg(sha.left(7)), 6000);
    // Refresh everything visibly affected: status (staged files
    // reappear), history (HEAD moved), reflog itself (the restore is
    // ALSO a new reflog entry).
    workspace_->refreshActiveFolder();
    worker_->loadHistory(path, /*maxCount*/ 200);
    if (reflogDialog_ && reflogDialog_->isVisible()) {
        worker_->loadReflog(path, /*maxCount*/ 200);
    }
}

// ----- Conflict resolution -------------------------------------------------

// ----- Conflict resolution -------------------------------------------------

void MainWindow::onResolveConflictsRequested(const QString& path)
{
    if (path.isEmpty()) return;
    conflict_->start(path);
}

void MainWindow::onConflictStatusChanged(const QString& message)
{
    setStatus(message);
}

void MainWindow::onConflictSucceeded(const QString& message)
{
    setStatus(message, 4000);
}

void MainWindow::onConflictFailed(const QString& title, const QString& error)
{
    QMessageBox::warning(this, title, error);
}

void MainWindow::onConflictAllResolved()
{
    QMessageBox::information(this, tr("All conflicts resolved"),
        tr("Every conflicted file has been marked resolved.\n\n"
           "Finish the merge by committing — type a message in the "
           "Commit box and click Commit, or use the default merge "
           "message."));
}

void MainWindow::onConflictWorkingTreeChanged(const QString& path)
{
    // Working tree changed (resolve mark, abort, or completion).
    // Refresh the active folder's view so staged/unstaged counts
    // and the conflict notice update.
    if (activeLocalPath_ == path) worker_->refreshLocalState(path);
}

void MainWindow::onPublishToGitHubRequested(const QString& path)
{
    if (path.isEmpty()) return;
    if (!session_->isSignedIn()) {
        QMessageBox::information(this, tr("Sign in required"),
            tr("Publishing requires being signed in to GitHub. Please sign in first."));
        session_->signIn(this);
        if (!session_->isSignedIn()) return;
    }
    if (publish_->isActive()) {
        QMessageBox::information(this, tr("Already publishing"),
            tr("Another publish operation is in progress — please wait for it to finish."));
        return;
    }

    const QString folderName = QFileInfo(path).fileName();
    PublishToGitHubDialog dlg(folderName,
                              /*suggestedRepoName*/ QString(),
                              /*accountLogin*/      session_->username(),
                              /*knownRepos*/        reposCache_,
                              this);
    if (dlg.exec() != QDialog::Accepted) return;

    // Hand off everything else to the controller. It owns the state
    // machine and watchdog from here until it emits succeeded/failed.
    ghm::workspace::PublishController::StartParams params;
    params.localPath            = path;
    params.pushAfter            = dlg.pushAfterPublish();
    if (dlg.mode() == PublishToGitHubDialog::Mode::CreateNew) {
        params.mode                = ghm::workspace::PublishController::Mode::CreateNew;
        params.newRepoName         = dlg.name();
        params.newRepoDescription  = dlg.description();
        params.isPrivate           = dlg.isPrivate();
        params.licenseTemplate     = dlg.licenseTemplate();
        params.gitignoreTemplate   = dlg.gitignoreTemplate();
    } else {
        params.mode     = ghm::workspace::PublishController::Mode::ExistingRepo;
        params.existing = dlg.existingRepo();
    }
    localDetail_->setBusy(true);
    publish_->start(params);
}

// ----- Publish controller feedback ------------------------------------------

void MainWindow::onPublishProgress(const QString& message)
{
    setBusy(true, message);
}

void MainWindow::onPublishRepoCreated(const ghm::github::Repository& repo,
                                       const QString& localPath)
{
    // Optimistically reflect in our caches so the sidebar updates the
    // moment the controller confirms. The controller fires this for
    // both CreateNew (after GitHub created the repo) and ExistingRepo
    // (immediately at start) so we can keep one update path.
    rememberLocalPath(repo.fullName, localPath);

    // Splice into the cached list — for CreateNew this surfaces the
    // new repo without a manual refresh; for ExistingRepo this is
    // a no-op (the repo is already in the cache).
    bool alreadyCached = false;
    for (const auto& r : reposCache_) {
        if (r.fullName == repo.fullName) { alreadyCached = true; break; }
    }
    if (!alreadyCached) {
        auto annotated = repo;
        annotated.localPath = localPath;
        reposCache_.prepend(annotated);
        repoList_->setRepositories(reposCache_);
    }
}

void MainWindow::onPublishNeedNonEmptyBranch(const QString& localPath)
{
    Q_UNUSED(localPath);
    QMessageBox::information(this, tr("Nothing to push yet"),
        tr("The remote is connected, but this branch has no commits. "
           "Make a commit and then push from the Remotes tab."));
}

void MainWindow::onPublishSucceeded(const QString& localPath,
                                     const QString& cloneUrl,
                                     const QString& repoFullName,
                                     bool pushed)
{
    Q_UNUSED(repoFullName);
    setBusy(false);
    localDetail_->setBusy(false);
    if (pushed) {
        setStatus(tr("Published to GitHub."), 6000);
    } else {
        setStatus(tr("Connected to %1.").arg(cloneUrl), 6000);
    }
    // Refresh so the local-detail view sees the new remote.
    if (activeLocalPath_ == localPath) {
        workspace_->refreshActiveFolder();
    }
}

void MainWindow::onPublishFailed(const QString& localPath,
                                  const QString& title,
                                  const QString& detail)
{
    setBusy(false);
    localDetail_->setBusy(false);
    QMessageBox::warning(this, title, detail);
    if (activeLocalPath_ == localPath) {
        workspace_->refreshActiveFolder();
    }
}

void MainWindow::onEditIdentityRequested()
{
    IdentityDialog dlg(settings_.authorName(), settings_.authorEmail(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    settings_.setAuthorName (dlg.name());
    settings_.setAuthorEmail(dlg.email());
    pushIdentityToWidget();
}

// ----- Worker callbacks (local-folder flow) --------------------------------

void MainWindow::onRemoteOpFinished(bool ok, const QString& path, const QString& error)
{
    // PublishController has its own remoteOpFinished subscription and
    // filters by path; if this op is part of a publish flow, the
    // controller handles it and we should stay out of the way.
    // For plain manual remote add/remove from the Remotes tab we
    // still want to surface the result.
    if (publish_->isActive() && publish_->activePath() == path) {
        // Controller owns the UI feedback for this one.
        return;
    }

    localDetail_->setBusy(false);

    if (!ok) {
        QMessageBox::warning(this, tr("Remote operation failed"), error);
    }
    if (activeLocalPath_ == path) worker_->refreshLocalState(path);
}

// ----- App-level UI --------------------------------------------------------

void MainWindow::onLanguageChosen(const QString& code)
{
    if (code.isEmpty() || code == settings_.language()) return;

    settings_.setLanguage(code);

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
