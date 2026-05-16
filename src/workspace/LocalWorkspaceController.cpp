#include "workspace/LocalWorkspaceController.h"

#include "core/Settings.h"
#include "git/GitWorker.h"
#include "ui/LocalRepositoryWidget.h"

#include <QString>

namespace ghm::workspace {

LocalWorkspaceController::LocalWorkspaceController(
        ghm::git::GitWorker&            worker,
        ghm::core::Settings&            settings,
        ghm::ui::LocalRepositoryWidget& widget,
        QObject*                        parent)
    : QObject(parent)
    , worker_  (worker)
    , settings_(settings)
    , widget_  (widget)
{
    // Subscribe to every worker signal we care about. Each handler
    // filters on activePath_ before re-emitting, so a callback for
    // a previously-active folder lands quietly.
    connect(&worker_, &ghm::git::GitWorker::localStateReady,
            this, &LocalWorkspaceController::onWorkerLocalStateReady);
    connect(&worker_, &ghm::git::GitWorker::stageFinished,
            this, &LocalWorkspaceController::onWorkerStageFinished);
    connect(&worker_, &ghm::git::GitWorker::unstageFinished,
            this, &LocalWorkspaceController::onWorkerUnstageFinished);
    connect(&worker_, &ghm::git::GitWorker::commitFinished,
            this, &LocalWorkspaceController::onWorkerCommitFinished);
    connect(&worker_, &ghm::git::GitWorker::initFinished,
            this, &LocalWorkspaceController::onWorkerInitFinished);
    connect(&worker_, &ghm::git::GitWorker::historyReady,
            this, &LocalWorkspaceController::onWorkerHistoryReady);
    connect(&worker_, &ghm::git::GitWorker::fileDiffReady,
            this, &LocalWorkspaceController::onWorkerFileDiffReady);
    connect(&worker_, &ghm::git::GitWorker::commitDiffReady,
            this, &LocalWorkspaceController::onWorkerCommitDiffReady);
    connect(&worker_, &ghm::git::GitWorker::branchInfosReady,
            this, &LocalWorkspaceController::onWorkerBranchInfosReady);
    connect(&worker_, &ghm::git::GitWorker::branchSwitched,
            this, &LocalWorkspaceController::onWorkerBranchSwitched);
    connect(&worker_, &ghm::git::GitWorker::branchCreated,
            this, &LocalWorkspaceController::onWorkerBranchCreated);
    connect(&worker_, &ghm::git::GitWorker::branchDeleted,
            this, &LocalWorkspaceController::onWorkerBranchDeleted);
    connect(&worker_, &ghm::git::GitWorker::branchRenamed,
            this, &LocalWorkspaceController::onWorkerBranchRenamed);
}

void LocalWorkspaceController::setActivePath(const QString& path)
{
    activePath_ = path;
}

void LocalWorkspaceController::refreshActiveFolder()
{
    if (activePath_.isEmpty()) return;
    worker_.refreshLocalState(activePath_);
    worker_.listBranchInfos(activePath_);
}

// ----- Slots wired to LocalRepositoryWidget signals ------------------------

void LocalWorkspaceController::onInitRequested(const QString& path,
                                               const QString& branch)
{
    if (path.isEmpty()) return;
    const QString useBranch = branch.isEmpty()
        ? QStringLiteral("master") : branch;
    // Persist the user's pick — next "Add Local Folder" should
    // default to the same name.
    settings_.setDefaultInitBranch(useBranch);
    widget_.setBusy(true);
    Q_EMIT busy(tr("Initializing repository…"));
    worker_.initRepository(path, useBranch);
}

void LocalWorkspaceController::onStageAllRequested(const QString& path)
{
    if (path.isEmpty()) return;
    widget_.setBusy(true);
    worker_.stageAll(path);
}

void LocalWorkspaceController::onStagePathsRequested(
    const QString& path, const QStringList& paths)
{
    if (path.isEmpty() || paths.isEmpty()) return;
    widget_.setBusy(true);
    worker_.stagePaths(path, paths);
}

void LocalWorkspaceController::onUnstagePathsRequested(
    const QString& path, const QStringList& paths)
{
    if (path.isEmpty() || paths.isEmpty()) return;
    widget_.setBusy(true);
    worker_.unstagePaths(path, paths);
}

void LocalWorkspaceController::onCommitRequested(const QString& path,
                                                 const QString& message)
{
    if (path.isEmpty() || message.trimmed().isEmpty()) return;

    // Identity check is the first thing — git refuses to commit
    // without a configured author, and we'd rather prompt cleanly
    // than let the worker fail with a libgit2 error. The host owns
    // the IdentityDialog, so we punt the prompt back via signal and
    // it calls commitWithKnownIdentity() once identity is filled in.
    if (settings_.authorName().isEmpty() ||
        settings_.authorEmail().isEmpty()) {
        Q_EMIT identityRequiredForCommit(path, message);
        return;
    }
    commitWithKnownIdentity(path, message);
}

void LocalWorkspaceController::commitWithKnownIdentity(const QString& path,
                                                        const QString& message)
{
    if (path.isEmpty() || message.trimmed().isEmpty()) return;
    widget_.setBusy(true);
    Q_EMIT busy(tr("Committing…"));

    // Build SigningConfig from settings. When mode == None this is
    // a default-constructed struct, which the handler treats as
    // "skip signing" — identical to the old unsigned-only behaviour.
    ghm::git::SigningConfig sigCfg;
    const auto mode = settings_.signingMode();
    if (mode == ghm::core::Settings::SigningMode::Gpg) {
        sigCfg.mode = ghm::git::SigningConfig::Mode::Gpg;
        sigCfg.key  = settings_.signingKey();
    } else if (mode == ghm::core::Settings::SigningMode::Ssh) {
        sigCfg.mode = ghm::git::SigningConfig::Mode::Ssh;
        sigCfg.key  = settings_.signingKey();
    }
    // mode == None → sigCfg stays default-constructed (mode=None).

    worker_.commitChanges(path, message,
                          settings_.authorName(), settings_.authorEmail(),
                          sigCfg);
}

void LocalWorkspaceController::onRefreshRequested(const QString& path)
{
    if (path.isEmpty()) return;
    worker_.refreshLocalState(path);
    worker_.listBranchInfos(path);
}

void LocalWorkspaceController::onHistoryRequested(const QString& path)
{
    if (path.isEmpty()) return;
    worker_.loadHistory(path, /*maxCount*/ 200);
}

void LocalWorkspaceController::onLoadMoreHistoryRequested(
    const QString& path, const QString& afterSha)
{
    if (path.isEmpty() || afterSha.isEmpty()) return;
    if (path != activePath_) return;
    worker_.loadHistory(path, /*maxCount*/ 200, afterSha);
}

void LocalWorkspaceController::onDiffRequested(const QString& path,
                                               const QString& repoRelPath,
                                               ghm::git::DiffScope scope)
{
    if (path.isEmpty() || repoRelPath.isEmpty()) return;
    worker_.loadFileDiff(path, repoRelPath, scope);
}

void LocalWorkspaceController::onCommitDiffRequested(const QString& path,
                                                      const QString& sha)
{
    if (path.isEmpty() || sha.isEmpty()) return;
    worker_.loadCommitDiff(path, sha);
}

void LocalWorkspaceController::onCommitCompareRequested(const QString& path,
                                                         const QString& shaA,
                                                         const QString& shaB)
{
    if (path.isEmpty() || shaA.isEmpty() || shaB.isEmpty()) return;
    worker_.loadCommitDiffBetween(path, shaA, shaB);
}

// ----- Branch ops ----------------------------------------------------------

void LocalWorkspaceController::onBranchSwitchRequested(const QString& path,
                                                        const QString& branch)
{
    if (path.isEmpty() || branch.isEmpty()) return;
    widget_.setBusy(true);
    Q_EMIT busy(tr("Switching to %1…").arg(branch));
    worker_.switchBranch(path, branch);
}

void LocalWorkspaceController::onBranchCreateRequested(const QString& path)
{
    if (path.isEmpty()) return;

    // We need the existing branch list to validate name collisions
    // in the dialog. listLocalBranches is reentrant on the handler,
    // so a synchronous call from the GUI thread is fine; this is what
    // MainWindow used to do directly.
    std::vector<ghm::git::BranchInfo> infos;
    (void)worker_.handler().listLocalBranches(path, infos);
    QStringList existing;
    for (const auto& b : infos) existing << b.name;

    Q_EMIT branchCreateDialogRequested(path, existing);
}

void LocalWorkspaceController::createBranchAccepted(const QString& path,
                                                     const QString& name,
                                                     bool checkout)
{
    if (path.isEmpty() || name.isEmpty()) return;
    widget_.setBusy(true);
    Q_EMIT busy(tr("Creating branch %1…").arg(name));
    worker_.createBranch(path, name, checkout);
}

void LocalWorkspaceController::onBranchDeleteRequested(const QString& path,
                                                        const QString& branch)
{
    if (path.isEmpty() || branch.isEmpty()) return;
    widget_.setBusy(true);
    Q_EMIT busy(tr("Deleting branch %1…").arg(branch));
    worker_.deleteBranch(path, branch, /*force*/ false);
}

void LocalWorkspaceController::forceDeleteConfirmed(const QString& path,
                                                     const QString& branch)
{
    if (path.isEmpty() || branch.isEmpty()) return;
    widget_.setBusy(true);
    Q_EMIT busy(tr("Force-deleting branch %1…").arg(branch));
    worker_.deleteBranch(path, branch, /*force*/ true);
}

void LocalWorkspaceController::onBranchRenameRequested(const QString& path,
                                                        const QString& oldName)
{
    if (path.isEmpty() || oldName.isEmpty()) return;

    std::vector<ghm::git::BranchInfo> infos;
    (void)worker_.handler().listLocalBranches(path, infos);
    QStringList existing;
    for (const auto& b : infos) existing << b.name;

    Q_EMIT branchRenameDialogRequested(path, oldName, existing);
}

void LocalWorkspaceController::renameBranchAccepted(const QString& path,
                                                     const QString& oldName,
                                                     const QString& newName)
{
    if (path.isEmpty() || oldName.isEmpty() || newName.isEmpty()) return;
    if (oldName == newName) return;
    widget_.setBusy(true);
    Q_EMIT busy(tr("Renaming branch %1 → %2…").arg(oldName, newName));
    worker_.renameBranch(path, oldName, newName);
}

// ----- Worker callback handlers --------------------------------------------

void LocalWorkspaceController::onWorkerLocalStateReady(
    const QString& path,
    bool                                       isRepository,
    const QString&                             branch,
    const std::vector<ghm::git::StatusEntry>&  entries,
    const std::vector<ghm::git::RemoteInfo>&   remotes)
{
    if (path != activePath_) return;
    widget_.setBusy(false);
    Q_EMIT idle();
    Q_EMIT stateRefreshed(path, isRepository, branch, entries, remotes);
}

void LocalWorkspaceController::onWorkerStageFinished(
    bool ok, const QString& path, const QString& error)
{
    if (path != activePath_) return;
    Q_EMIT stageOpFinished(ok, path, error);
    if (ok) worker_.refreshLocalState(path);
}

void LocalWorkspaceController::onWorkerUnstageFinished(
    bool ok, const QString& path, const QString& error)
{
    if (path != activePath_) return;
    Q_EMIT unstageOpFinished(ok, path, error);
    if (ok) worker_.refreshLocalState(path);
}

void LocalWorkspaceController::onWorkerCommitFinished(
    bool ok, const QString& path, const QString& sha, const QString& error)
{
    if (path != activePath_) return;
    widget_.setBusy(false);
    Q_EMIT idle();
    Q_EMIT commitFinished(ok, path, sha, error);
    if (ok) {
        worker_.refreshLocalState(path);
        worker_.loadHistory(path, /*maxCount*/ 200);
    }
}

void LocalWorkspaceController::onWorkerInitFinished(
    bool ok, const QString& path, const QString& error)
{
    if (path != activePath_) return;
    widget_.setBusy(false);
    Q_EMIT idle();
    Q_EMIT initFinished(ok, path, error);
    if (ok) {
        worker_.refreshLocalState(path);
        worker_.listBranchInfos(path);
    }
}

void LocalWorkspaceController::onWorkerHistoryReady(
    const QString& path, const std::vector<ghm::git::CommitInfo>& commits,
    bool isAppend)
{
    if (path != activePath_) return;
    Q_EMIT historyReady(path, commits, isAppend);
}

void LocalWorkspaceController::onWorkerFileDiffReady(
    const QString& path, const QString& repoRelPath,
    const ghm::git::FileDiff& diff, const QString& error)
{
    if (path != activePath_) return;
    Q_EMIT fileDiffReady(path, repoRelPath, diff, error);
}

void LocalWorkspaceController::onWorkerCommitDiffReady(
    const QString& path, const QString& sha,
    const std::vector<ghm::git::FileDiff>& files, const QString& error)
{
    if (path != activePath_) return;
    Q_EMIT commitDiffReady(path, sha, files, error);
}

void LocalWorkspaceController::onWorkerBranchInfosReady(
    const QString& path, const std::vector<ghm::git::BranchInfo>& infos)
{
    if (path != activePath_) return;
    Q_EMIT branchInfosReady(path, infos);
}

void LocalWorkspaceController::onWorkerBranchSwitched(
    bool ok, const QString& path, const QString& branch, const QString& error)
{
    if (path != activePath_) return;
    widget_.setBusy(false);
    Q_EMIT idle();
    Q_EMIT branchSwitched(ok, path, branch, error);
    if (ok) {
        worker_.refreshLocalState(path);
        worker_.listBranchInfos(path);
    }
}

void LocalWorkspaceController::onWorkerBranchCreated(
    bool ok, const QString& path, const QString& branch, const QString& error)
{
    if (path != activePath_) return;
    widget_.setBusy(false);
    Q_EMIT idle();
    Q_EMIT branchCreated(ok, path, branch, error);
    if (ok) {
        worker_.refreshLocalState(path);
        worker_.listBranchInfos(path);
    }
}

void LocalWorkspaceController::onWorkerBranchDeleted(
    bool ok, const QString& path, const QString& branch, const QString& error)
{
    if (path != activePath_) return;
    widget_.setBusy(false);

    // If the failure looks like "branch is not fully merged", escalate
    // to a force-delete confirmation. Don't emit idle yet — host will
    // re-fire force-delete which goes back to busy.
    if (!ok && error.contains(QStringLiteral("not fully merged"),
                              Qt::CaseInsensitive)) {
        Q_EMIT branchForceDeleteConfirmation(path, branch, error);
        return;
    }
    Q_EMIT idle();
    Q_EMIT branchDeleted(ok, path, branch, error);
    if (ok) {
        worker_.refreshLocalState(path);
        worker_.listBranchInfos(path);
    }
}

void LocalWorkspaceController::onWorkerBranchRenamed(
    bool ok, const QString& path,
    const QString& oldName, const QString& newName,
    const QString& error)
{
    if (path != activePath_) return;
    widget_.setBusy(false);
    Q_EMIT idle();
    Q_EMIT branchRenamed(ok, path, oldName, newName, error);
    if (ok) {
        worker_.refreshLocalState(path);
        worker_.listBranchInfos(path);
    }
}

} // namespace ghm::workspace
