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
    qRegisterMetaType<std::vector<ghm::git::StashEntry>> ("std::vector<ghm::git::StashEntry>");
    qRegisterMetaType<std::vector<ghm::git::TagInfo>>    ("std::vector<ghm::git::TagInfo>");
    qRegisterMetaType<std::vector<ghm::git::ReflogEntry>>(
        "std::vector<ghm::git::ReflogEntry>");
    qRegisterMetaType<ghm::git::ConflictEntry>           ("ghm::git::ConflictEntry");
    qRegisterMetaType<std::vector<ghm::git::ConflictEntry>>(
        "std::vector<ghm::git::ConflictEntry>");
    qRegisterMetaType<ghm::git::FileDiff>                ("ghm::git::FileDiff");
    qRegisterMetaType<std::vector<ghm::git::FileDiff>>   ("std::vector<ghm::git::FileDiff>");
    qRegisterMetaType<ghm::git::DiffScope>               ("ghm::git::DiffScope");
}

GitWorker::~GitWorker() = default;

// ----- GitHub-clone flow (existing) ----------------------------------------

void GitWorker::clone(const QString& url, const QString& localPath,
                       const QString& token,
                       const ghm::git::SshCredentials& sshCreds)
{
    auto progressFn = makeProgressFn(this);
    // Capture sshCreds by copy — the lambda runs on the worker thread
    // after this function returns, so we can't rely on the caller's
    // reference staying valid. The struct is small (three QStrings).
    auto task = [this, url, localPath, token, progressFn, sshCreds]() -> GitResult {
        return handler_.clone(url, localPath, token, progressFn, sshCreds);
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

void GitWorker::renameBranch(const QString& localPath,
                             const QString& oldName,
                             const QString& newName)
{
    auto task = [this, localPath, oldName, newName]() -> GitResult {
        return handler_.renameBranch(localPath, oldName, newName, /*force*/ false);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, oldName, newName](const GitResult& r) {
            Q_EMIT branchRenamed(r.ok, localPath, oldName, newName, r.error);
        });
}

void GitWorker::fetchRemote(const QString& localPath,
                            const QString& remoteName,
                            const QString& pat)
{
    auto progressFn = makeProgressFn(this);
    auto task = [this, localPath, remoteName, pat, progressFn]() -> GitResult {
        return handler_.fetch(localPath, remoteName, pat, progressFn);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, remoteName](const GitResult& r) {
            Q_EMIT fetchFinished(r.ok, localPath, remoteName, r.error);
        });
}

void GitWorker::undoLastCommit(const QString& localPath)
{
    auto task = [this, localPath]() -> GitResult {
        return handler_.undoLastCommit(localPath);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT undoLastCommitFinished(r.ok, localPath, r.error);
        });
}

void GitWorker::loadReflog(const QString& localPath, int maxCount)
{
    auto task = [this, localPath, maxCount]() -> std::vector<ReflogEntry> {
        std::vector<ReflogEntry> out;
        // No error path surfaced to the host — empty list on failure
        // is enough. The likely failures are "no reflog yet" (brand
        // new repo) which is correctly represented by an empty list,
        // or repo I/O errors which the user has already seen elsewhere.
        (void)handler_.readHeadReflog(localPath, maxCount, out);
        return out;
    };
    runAsync<std::vector<ReflogEntry>>(this, std::move(task),
        [this, localPath](const std::vector<ReflogEntry>& entries) {
            Q_EMIT reflogReady(localPath, entries);
        });
}

void GitWorker::softResetTo(const QString& localPath, const QString& sha)
{
    auto task = [this, localPath, sha]() -> GitResult {
        return handler_.softResetTo(localPath, sha);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, sha](const GitResult& r) {
            Q_EMIT softResetFinished(r.ok, localPath, sha, r.error);
        });
}

// ----- Stash --------------------------------------------------------------

void GitWorker::stashSave(const QString& localPath,
                          const QString& message,
                          bool           includeUntracked,
                          bool           keepIndex,
                          const QString& authorName,
                          const QString& authorEmail)
{
    auto task = [this, localPath, message, includeUntracked, keepIndex,
                 authorName, authorEmail]() -> GitResult {
        return handler_.stashSave(localPath, message,
                                  includeUntracked, keepIndex,
                                  authorName, authorEmail);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT stashOpFinished(r.ok, localPath,
                                   QStringLiteral("save"), r.error);
        });
}

void GitWorker::stashList(const QString& localPath)
{
    auto task = [this, localPath]() -> std::vector<StashEntry> {
        std::vector<StashEntry> out;
        (void)handler_.stashList(localPath, out);
        return out;
    };
    runAsync<std::vector<StashEntry>>(this, std::move(task),
        [this, localPath](const std::vector<StashEntry>& v) {
            Q_EMIT stashListReady(localPath, v);
        });
}

void GitWorker::stashApply(const QString& localPath, int index)
{
    auto task = [this, localPath, index]() -> GitResult {
        return handler_.stashApply(localPath, index);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT stashOpFinished(r.ok, localPath,
                                   QStringLiteral("apply"), r.error);
        });
}

void GitWorker::stashPop(const QString& localPath, int index)
{
    auto task = [this, localPath, index]() -> GitResult {
        return handler_.stashPop(localPath, index);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT stashOpFinished(r.ok, localPath,
                                   QStringLiteral("pop"), r.error);
        });
}

void GitWorker::stashDrop(const QString& localPath, int index)
{
    auto task = [this, localPath, index]() -> GitResult {
        return handler_.stashDrop(localPath, index);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT stashOpFinished(r.ok, localPath,
                                   QStringLiteral("drop"), r.error);
        });
}

// ----- Tags ---------------------------------------------------------------

void GitWorker::listTags(const QString& localPath)
{
    auto task = [this, localPath]() -> std::vector<TagInfo> {
        std::vector<TagInfo> out;
        (void)handler_.listTags(localPath, out);
        return out;
    };
    runAsync<std::vector<TagInfo>>(this, std::move(task),
        [this, localPath](const std::vector<TagInfo>& v) {
            Q_EMIT tagsReady(localPath, v);
        });
}

void GitWorker::createTag(const QString& localPath,
                          const QString& name,
                          const QString& message,
                          const QString& authorName,
                          const QString& authorEmail,
                          const ghm::git::SigningConfig& signingConfig)
{
    auto task = [this, localPath, name, message,
                 authorName, authorEmail, signingConfig]() -> GitResult {
        return handler_.createTag(localPath, name, message,
                                  authorName, authorEmail, signingConfig);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, name](const GitResult& r) {
            Q_EMIT tagOpFinished(r.ok, localPath,
                                 QStringLiteral("create"), name, r.error);
        });
}

void GitWorker::deleteTag(const QString& localPath, const QString& name)
{
    auto task = [this, localPath, name]() -> GitResult {
        return handler_.deleteTag(localPath, name);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, name](const GitResult& r) {
            Q_EMIT tagOpFinished(r.ok, localPath,
                                 QStringLiteral("delete"), name, r.error);
        });
}

// ----- Submodules ----------------------------------------------------------

void GitWorker::listSubmodules(const QString& localPath)
{
    auto task = [this, localPath]() -> std::vector<SubmoduleInfo> {
        std::vector<SubmoduleInfo> out;
        (void)handler_.listSubmodules(localPath, out);
        return out;
    };
    runAsync<std::vector<SubmoduleInfo>>(this, std::move(task),
        [this, localPath](const std::vector<SubmoduleInfo>& v) {
            Q_EMIT submodulesReady(localPath, v);
        });
}

void GitWorker::initAndUpdateSubmodule(const QString& localPath,
                                        const QString& name,
                                        const QString& token,
                                        const ghm::git::SshCredentials& sshCreds)
{
    auto progressFn = makeProgressFn(this);
    auto task = [this, localPath, name, token, sshCreds, progressFn]() -> GitResult {
        return handler_.initAndUpdateSubmoduleWithCreds(
            localPath, name, token, sshCreds, progressFn);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, name](const GitResult& r) {
            Q_EMIT submoduleOpFinished(r.ok, localPath,
                QStringLiteral("init+update"), name, r.error);
        });
}

void GitWorker::updateSubmodule(const QString& localPath,
                                 const QString& name,
                                 const QString& token,
                                 const ghm::git::SshCredentials& sshCreds)
{
    auto progressFn = makeProgressFn(this);
    auto task = [this, localPath, name, token, sshCreds, progressFn]() -> GitResult {
        return handler_.updateSubmoduleWithCreds(
            localPath, name, token, sshCreds, progressFn);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, name](const GitResult& r) {
            Q_EMIT submoduleOpFinished(r.ok, localPath,
                QStringLiteral("update"), name, r.error);
        });
}

void GitWorker::syncSubmoduleUrl(const QString& localPath, const QString& name)
{
    // Sync is fast — no network — but we still go through the worker
    // for consistency with the other two ops and to avoid blocking
    // the GUI on .gitmodules I/O.
    auto task = [this, localPath, name]() -> GitResult {
        return handler_.syncSubmoduleUrl(localPath, name);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, name](const GitResult& r) {
            Q_EMIT submoduleOpFinished(r.ok, localPath,
                QStringLiteral("sync"), name, r.error);
        });
}

void GitWorker::addSubmodule(const QString& localPath,
                              const QString& url,
                              const QString& subPath,
                              const QString& token,
                              const ghm::git::SshCredentials& sshCreds)
{
    auto progressFn = makeProgressFn(this);
    auto task = [this, localPath, url, subPath, token, sshCreds, progressFn]() -> GitResult {
        return handler_.addSubmodule(localPath, url, subPath, token,
                                      sshCreds, progressFn);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, subPath](const GitResult& r) {
            // Use subPath as the "name" in the signal since the user
            // didn't pick a logical name — they picked a path.
            Q_EMIT submoduleOpFinished(r.ok, localPath,
                QStringLiteral("add"), subPath, r.error);
        });
}

void GitWorker::removeSubmodule(const QString& localPath, const QString& name)
{
    auto task = [this, localPath, name]() -> GitResult {
        return handler_.removeSubmodule(localPath, name);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, name](const GitResult& r) {
            Q_EMIT submoduleOpFinished(r.ok, localPath,
                QStringLiteral("remove"), name, r.error);
        });
}

// ----- Conflict resolution -------------------------------------------------

void GitWorker::listConflicts(const QString& localPath)
{
    auto task = [this, localPath]() -> std::vector<ConflictEntry> {
        std::vector<ConflictEntry> out;
        (void)handler_.listConflicts(localPath, out);
        return out;
    };
    runAsync<std::vector<ConflictEntry>>(this, std::move(task),
        [this, localPath](const std::vector<ConflictEntry>& v) {
            Q_EMIT conflictsReady(localPath, v);
        });
}

void GitWorker::loadConflictBlobs(const QString& localPath,
                                  const ghm::git::ConflictEntry& entry)
{
    auto task = [this, localPath, entry]() -> ConflictEntry {
        ConflictEntry copy = entry;
        (void)handler_.loadConflictBlobs(localPath, copy);
        return copy;
    };
    runAsync<ConflictEntry>(this, std::move(task),
        [this, localPath](const ConflictEntry& filled) {
            Q_EMIT conflictBlobsReady(localPath, filled);
        });
}

void GitWorker::markResolved(const QString& localPath, const QString& path)
{
    auto task = [this, localPath, path]() -> GitResult {
        return handler_.markResolved(localPath, path);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath, path](const GitResult& r) {
            Q_EMIT conflictOpFinished(r.ok, localPath,
                                      QStringLiteral("resolve"), path, r.error);
        });
}

void GitWorker::abortMerge(const QString& localPath)
{
    auto task = [this, localPath]() -> GitResult {
        return handler_.abortMerge(localPath);
    };
    runAsync<GitResult>(this, std::move(task),
        [this, localPath](const GitResult& r) {
            Q_EMIT conflictOpFinished(r.ok, localPath,
                                      QStringLiteral("abort"), QString(), r.error);
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
                              const QString& authorEmail,
                              const ghm::git::SigningConfig& signingConfig)
{
    struct Out { bool ok; QString sha; QString error; };
    // Capture signingConfig by copy — it's a small struct and the
    // lambda may run after this function returns.
    auto task = [this, localPath, message, authorName, authorEmail,
                 signingConfig]() -> Out {
        Out o;
        QString sha;
        const auto r = handler_.commit(localPath, message,
                                        authorName, authorEmail,
                                        &sha, signingConfig);
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

void GitWorker::loadHistory(const QString& localPath, int maxCount,
                            const QString& afterSha)
{
    const bool isAppend = !afterSha.isEmpty();
    auto task = [this, localPath, maxCount, afterSha]() -> std::vector<CommitInfo> {
        std::vector<CommitInfo> out;
        // Compute diff stats per commit. Adds ~10-30ms per commit but
        // since this runs on the worker thread the GUI stays responsive,
        // and the history view becomes substantially more useful — users
        // can scan for big commits at a glance.
        (void)handler_.log(localPath, maxCount, out,
                           /*computeStats*/ true, afterSha);
        return out;
    };
    runAsync<std::vector<CommitInfo>>(this, std::move(task),
        [this, localPath, isAppend](const std::vector<CommitInfo>& commits) {
            Q_EMIT historyReady(localPath, commits, isAppend);
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

void GitWorker::verifyCommitSignature(const QString& localPath,
                                       const QString& sha,
                                       const QString& allowedSignersPath)
{
    auto task = [this, localPath, sha, allowedSignersPath]() -> VerifyResult {
        VerifyResult vr;
        // The handler's GitResult flags "couldn't even attempt verify"
        // (e.g. bad SHA, bad path). If that happens, we leave vr as
        // its default Unsigned state and let the UI decide whether to
        // show an error icon vs nothing. The verifier's own errors
        // (gpg failed, sig invalid) propagate through vr.status.
        (void)handler_.verifyCommitSignature(
            localPath, sha, allowedSignersPath, vr);
        return vr;
    };
    runAsync<VerifyResult>(this, std::move(task),
        [this, localPath, sha](const VerifyResult& vr) {
            Q_EMIT signatureVerified(localPath, sha, vr);
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

void GitWorker::loadCommitDiffBetween(const QString& localPath,
                                      const QString& shaA,
                                      const QString& shaB)
{
    struct Out { std::vector<FileDiff> files; QString error; };
    auto task = [this, localPath, shaA, shaB]() -> Out {
        Out o;
        const auto r = handler_.commitDiffBetween(localPath, shaA, shaB, o.files);
        if (!r.ok) o.error = r.error;
        return o;
    };
    // Synthetic identifier matches `git diff a..b` syntax — lets the
    // host tell single-commit results apart from compare results
    // without us inventing a new signal.
    const QString synthId = shaA + QStringLiteral("..") + shaB;
    runAsync<Out>(this, std::move(task),
        [this, localPath, synthId](const Out& r) {
            Q_EMIT commitDiffReady(localPath, synthId, r.files, r.error);
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
