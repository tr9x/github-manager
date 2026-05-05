#include "git/GitWorker.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>
#include <QPointer>
#include <QMetaType>

namespace ghm::git {

namespace {

// Launches `task` on the global thread pool, hands the result to `onDone`
// on the GUI thread, and self-cleans the watcher.
template <typename R, typename Fn, typename DoneFn>
void runAsync(QObject* owner, Fn&& task, DoneFn&& onDone)
{
    auto* watcher = new QFutureWatcher<R>(owner);
    QObject::connect(watcher, &QFutureWatcher<R>::finished, owner,
                     [watcher, onDone = std::forward<DoneFn>(onDone)]() mutable {
        onDone(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(std::forward<Fn>(task)));
}

// Convenience: build a thread-safe progress hook that re-enters the
// GitWorker via QueuedConnection so the actual signal emission happens
// on the GUI thread.
auto makeProgressFn(GitWorker* self) {
    QPointer<GitWorker> guarded(self);
    return [guarded](const QString& phase, qint64 cur, qint64 tot) {
        if (guarded) {
            QMetaObject::invokeMethod(guarded.data(), "progress",
                Qt::QueuedConnection,
                Q_ARG(QString, phase), Q_ARG(qint64, cur), Q_ARG(qint64, tot));
        }
    };
}

} // namespace

GitWorker::GitWorker(QObject* parent) : QObject(parent)
{
    // Register STL containers used in cross-thread signals so Qt can
    // marshal them in case of a queued connection. (Direct connections
    // work without this; we register defensively.)
    qRegisterMetaType<std::vector<ghm::git::StatusEntry>>("std::vector<ghm::git::StatusEntry>");
    qRegisterMetaType<std::vector<ghm::git::RemoteInfo>> ("std::vector<ghm::git::RemoteInfo>");
    qRegisterMetaType<std::vector<ghm::git::CommitInfo>> ("std::vector<ghm::git::CommitInfo>");
    qRegisterMetaType<std::vector<ghm::git::BranchInfo>> ("std::vector<ghm::git::BranchInfo>");
    qRegisterMetaType<ghm::git::FileDiff>                ("ghm::git::FileDiff");
    qRegisterMetaType<std::vector<ghm::git::FileDiff>>   ("std::vector<ghm::git::FileDiff>");
    qRegisterMetaType<ghm::git::DiffScope>               ("ghm::git::DiffScope");
}

GitWorker::~GitWorker() = default;

// ----- GitHub-clone flow (existing) ----------------------------------------

void GitWorker::clone(const QString& url, const QString& localPath, const QString& token)
{
    auto progressFn = makeProgressFn(this);
    auto task = [this, url, localPath, token, progressFn]() -> GitResult {
        return handler_.clone(url, localPath, token, progressFn);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT cloneFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::pull(const QString& localPath, const QString& token)
{
    auto progressFn = makeProgressFn(this);
    auto task = [this, localPath, token, progressFn]() -> GitResult {
        return handler_.pull(localPath, token, progressFn);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT pullFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::push(const QString& localPath, const QString& token)
{
    auto progressFn = makeProgressFn(this);
    auto task = [this, localPath, token, progressFn]() -> GitResult {
        return handler_.push(localPath, token, progressFn);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT pushFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::refreshStatus(const QString& localPath)
{
    struct Out { QString branch; StatusSummary status; QString error; };
    auto task = [this, localPath]() -> Out {
        Out out;
        GitResult err;
        out.branch = handler_.currentBranch(localPath, &err);
        if (!err.ok) { out.error = err.error; return out; }
        out.status = handler_.status(localPath, &err);
        if (!err.ok) { out.error = err.error; }
        return out;
    };
    runAsync<Out>(this, std::move(task),
        [this, localPath](const Out& r) {
            Q_UNUSED(r.error);
            Q_EMIT statusReady(localPath, r.branch, r.status);
        });
}

void GitWorker::switchBranch(const QString& localPath, const QString& branch)
{
    auto task = [this, localPath, branch]() -> GitResult {
        return handler_.checkoutBranch(localPath, branch);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, branch](const GitResult& r) {
            Q_EMIT branchSwitched(r.ok, localPath, branch, r.error);
        });
}

void GitWorker::listBranches(const QString& localPath)
{
    auto task = [this, localPath]() -> QStringList {
        return handler_.localBranches(localPath);
    };
    runAsync<QStringList>(this, std::move(task),
        [this, localPath](const QStringList& bs) {
            Q_EMIT branchesReady(localPath, bs);
        });
}

void GitWorker::listBranchInfos(const QString& localPath)
{
    auto task = [this, localPath]() -> std::vector<BranchInfo> {
        std::vector<BranchInfo> out;
        (void)handler_.listLocalBranches(localPath, out);
        return out;
    };
    runAsync<std::vector<BranchInfo>>(this, std::move(task),
        [this, localPath](const std::vector<BranchInfo>& bs) {
            Q_EMIT branchInfosReady(localPath, bs);
        });
}

void GitWorker::createBranch(const QString& localPath,
                             const QString& name,
                             bool           checkoutAfter)
{
    auto task = [this, localPath, name, checkoutAfter]() -> GitResult {
        return handler_.createBranch(localPath, name, checkoutAfter);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, name](const GitResult& r) {
            Q_EMIT branchCreated(r.ok, localPath, name, r.error);
        });
}

void GitWorker::deleteBranch(const QString& localPath,
                             const QString& name,
                             bool           force)
{
    auto task = [this, localPath, name, force]() -> GitResult {
        return handler_.deleteBranch(localPath, name, force);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, name](const GitResult& r) {
            Q_EMIT branchDeleted(r.ok, localPath, name, r.error);
        });
}

// ----- Local-folder workflow -----------------------------------------------

void GitWorker::initRepository(const QString& localPath, const QString& initialBranch)
{
    auto task = [this, localPath, initialBranch]() -> GitResult {
        return handler_.init(localPath, initialBranch);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT initFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::refreshLocalState(const QString& localPath)
{
    struct Out {
        bool                     isRepo{false};
        QString                  branch;
        std::vector<StatusEntry> entries;
        std::vector<RemoteInfo>  remotes;
    };

    auto task = [this, localPath]() -> Out {
        Out out;
        out.isRepo = handler_.isRepository(localPath);
        if (!out.isRepo) return out;

        GitResult err;
        out.branch = handler_.currentBranch(localPath, &err);
        // Even if branch resolution failed we try the rest; a freshly-init
        // repo has unborn HEAD which we surface as "(unborn)".
        (void)handler_.statusEntries(localPath, out.entries);
        (void)handler_.listRemotes  (localPath, out.remotes);
        return out;
    };
    runAsync<Out>(this, std::move(task),
        [this, localPath](const Out& r) {
            Q_EMIT localStateReady(localPath, r.isRepo, r.branch,
                                   r.entries, r.remotes);
        });
}

void GitWorker::stageAll(const QString& localPath)
{
    auto task = [this, localPath]() -> GitResult {
        return handler_.stageAll(localPath);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT stageFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::stagePaths(const QString& localPath, const QStringList& paths)
{
    auto task = [this, localPath, paths]() -> GitResult {
        return handler_.stagePaths(localPath, paths);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT stageFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::unstagePaths(const QString& localPath, const QStringList& paths)
{
    auto task = [this, localPath, paths]() -> GitResult {
        return handler_.unstagePaths(localPath, paths);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT unstageFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::commitChanges(const QString& localPath,
                              const QString& message,
                              const QString& authorName,
                              const QString& authorEmail)
{
    struct Out { bool ok; QString sha; QString error; };
    auto task = [this, localPath, message, authorName, authorEmail]() -> Out {
        Out o;
        QString sha;
        const auto r = handler_.commit(localPath, message, authorName, authorEmail, &sha);
        o.ok    = r.ok;
        o.error = r.error;
        o.sha   = sha;
        return o;
    };
    runAsync<Out>(this, std::move(task),
        [this, localPath](const Out& r) {
            Q_EMIT commitFinished(r.ok, localPath, r.sha, r.error);
        });
}

void GitWorker::loadHistory(const QString& localPath, int maxCount)
{
    auto task = [this, localPath, maxCount]() -> std::vector<CommitInfo> {
        std::vector<CommitInfo> out;
        (void)handler_.log(localPath, maxCount, out);
        return out;
    };
    runAsync<std::vector<CommitInfo>>(this, std::move(task),
        [this, localPath](const std::vector<CommitInfo>& commits) {
            Q_EMIT historyReady(localPath, commits);
        });
}

void GitWorker::loadFileDiff(const QString& localPath,
                             const QString& repoRelPath,
                             ghm::git::DiffScope scope)
{
    struct Out { FileDiff diff; QString error; };
    auto task = [this, localPath, repoRelPath, scope]() -> Out {
        Out o;
        const auto r = handler_.fileDiff(localPath, repoRelPath, scope, o.diff);
        if (!r.ok) o.error = r.error;
        return o;
    };
    runAsync<Out>(this, std::move(task),
        [this, localPath, repoRelPath](const Out& r) {
            Q_EMIT fileDiffReady(localPath, repoRelPath, r.diff, r.error);
        });
}

void GitWorker::loadCommitDiff(const QString& localPath, const QString& sha)
{
    struct Out { std::vector<FileDiff> files; QString error; };
    auto task = [this, localPath, sha]() -> Out {
        Out o;
        const auto r = handler_.commitDiff(localPath, sha, o.files);
        if (!r.ok) o.error = r.error;
        return o;
    };
    runAsync<Out>(this, std::move(task),
        [this, localPath, sha](const Out& r) {
            Q_EMIT commitDiffReady(localPath, sha, r.files, r.error);
        });
}

void GitWorker::addRemote(const QString& localPath,
                          const QString& name, const QString& url)
{
    auto task = [this, localPath, name, url]() -> GitResult {
        return handler_.addRemote(localPath, name, url);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT remoteOpFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::removeRemote(const QString& localPath, const QString& name)
{
    auto task = [this, localPath, name]() -> GitResult {
        return handler_.removeRemote(localPath, name);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT remoteOpFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::pushTo(const QString& localPath,
                       const QString& remoteName,
                       const QString& branch,
                       bool setUpstreamAfter,
                       const QString& token)
{
    auto progressFn = makeProgressFn(this);
    auto task = [this, localPath, remoteName, branch, setUpstreamAfter,
                 token, progressFn]() -> GitResult {
        return handler_.pushBranch(localPath, remoteName, branch,
                                   setUpstreamAfter, token, progressFn);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            // Reuse the existing pushFinished signal so MainWindow only
            // needs to listen in one place.
            Q_EMIT pushFinished(r.ok, localPath, r.error);
        });
}

} // namespace ghm::git
