#include "workspace/GitHubCloneController.h"

#include <QFileInfo>
#include <QDir>

#include "git/GitWorker.h"

namespace ghm::workspace {

GitHubCloneController::GitHubCloneController(ghm::git::GitWorker& worker,
                                             QObject* parent)
    : QObject(parent)
    , worker_(worker)
{
    // We subscribe to cloneFinished unconditionally and filter inside
    // the handler by inFlightLocalPath_. If we're not running a
    // clone, the signal still arrives (worker is shared) but the
    // handler no-ops.
    connect(&worker_, &ghm::git::GitWorker::cloneFinished,
            this, &GitHubCloneController::onCloneFinished);
}

bool GitHubCloneController::startClone(const ghm::github::Repository& repo,
                                        const QString& targetPath,
                                        const QString& token,
                                        const ghm::git::SshCredentials& sshCreds)
{
    if (!repo.isValid())   return false;
    if (targetPath.isEmpty()) return false;
    if (isBusy()) return false;  // host should refuse the new clone

    if (QFileInfo::exists(targetPath)) {
        // Don't enter busy state — fail immediately so the host can
        // re-show its dialog with a different target. The repo
        // payload echoes back so the host can reopen the dialog
        // pre-filled if it wants.
        Q_EMIT failed(repo, targetPath,
            tr("Cannot clone"),
            tr("'%1' already exists. Choose a different folder.").arg(targetPath));
        return false;
    }

    // Optimistically remember the parent dir as the new default —
    // host persists this to settings so the next CloneDialog opens
    // there. We do it BEFORE the worker call so even if the clone
    // fails, the directory the user picked stays the default.
    const QString parentDir = QFileInfo(targetPath).absolutePath();
    if (!parentDir.isEmpty()) {
        Q_EMIT defaultCloneDirectoryChanged(parentDir);
    }

    inFlightFullName_  = repo.fullName;
    inFlightLocalPath_ = targetPath;
    inFlightRepo_      = repo;

    Q_EMIT cloneStarted(repo, targetPath);
    worker_.clone(repo.cloneUrl, targetPath, token, sshCreds);
    return true;
}

void GitHubCloneController::openExisting(const ghm::github::Repository& repo,
                                          const QString& localPath)
{
    if (!repo.isValid() || localPath.isEmpty()) return;

    const QString gitDir = QDir(localPath).filePath(QStringLiteral(".git"));
    if (!QFileInfo(gitDir).exists()) {
        Q_EMIT failed(repo, localPath,
            tr("Not a git repository"),
            tr("'%1' does not contain a .git directory.").arg(localPath));
        return;
    }

    Q_EMIT opened(repo, localPath);
}

void GitHubCloneController::onCloneFinished(bool ok,
                                             const QString& localPath,
                                             const QString& error)
{
    // Filter: only react to the clone we initiated. If a clone was
    // kicked off from elsewhere (currently nothing does, but defending
    // against future code), the host handles that separately.
    if (localPath != inFlightLocalPath_) return;

    // Snapshot state before clearing so the signal payload is intact.
    const auto repo  = inFlightRepo_;
    const QString lp = inFlightLocalPath_;

    inFlightFullName_.clear();
    inFlightLocalPath_.clear();
    inFlightRepo_ = {};

    if (!ok) {
        Q_EMIT failed(repo, lp, tr("Clone failed"), error);
        return;
    }
    Q_EMIT cloned(repo, lp);
}

} // namespace ghm::workspace
