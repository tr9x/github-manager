#pragma once

// GitWorker - turns blocking GitHandler calls into async, signal-based
// operations. Each public slot enqueues work onto QtConcurrent's global
// thread pool and emits a corresponding finished signal on completion.
//
// All result signals are emitted on the GUI thread (via QFutureWatcher),
// so consumers can update UI without extra marshalling.

#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

#include "git/GitHandler.h"

namespace ghm::git {

class GitWorker : public QObject {
    Q_OBJECT
public:
    explicit GitWorker(QObject* parent = nullptr);
    ~GitWorker() override;

public Q_SLOTS:
    // -- GitHub-clone flow (existing) ------------------------------------
    void clone(const QString& url, const QString& localPath, const QString& token);
    void pull (const QString& localPath, const QString& token);
    void push (const QString& localPath, const QString& token);
    void refreshStatus(const QString& localPath);
    void switchBranch(const QString& localPath, const QString& branch);
    void listBranches(const QString& localPath);

    // -- Local-folder workflow -------------------------------------------
    void initRepository(const QString& localPath, const QString& initialBranch);

    // Combined refresh: branch + per-file status + remotes, in one task.
    // Cheaper than three separate round-trips and keeps the UI consistent.
    void refreshLocalState(const QString& localPath);

    void stageAll  (const QString& localPath);
    void stagePaths(const QString& localPath, const QStringList& paths);
    void unstagePaths(const QString& localPath, const QStringList& paths);

    void commitChanges(const QString& localPath,
                       const QString& message,
                       const QString& authorName,
                       const QString& authorEmail);

    void loadHistory(const QString& localPath, int maxCount);

    void addRemote(const QString& localPath,
                   const QString& name, const QString& url);
    void removeRemote(const QString& localPath, const QString& name);

    void pushTo(const QString& localPath,
                const QString& remoteName,
                const QString& branch,
                bool setUpstreamAfter,
                const QString& token);

Q_SIGNALS:
    // -- GitHub-clone flow (existing) ------------------------------------
    void cloneFinished(bool ok, const QString& localPath, const QString& error);
    void pullFinished (bool ok, const QString& localPath, const QString& error);
    void pushFinished (bool ok, const QString& localPath, const QString& error);
    void statusReady(const QString& localPath,
                     const QString& currentBranch,
                     const ghm::git::StatusSummary& status);
    void branchSwitched(bool ok, const QString& localPath,
                        const QString& branch, const QString& error);
    void branchesReady(const QString& localPath, const QStringList& branches);

    // -- Local-folder workflow -------------------------------------------
    void initFinished(bool ok, const QString& localPath, const QString& error);

    // Whole-state snapshot for a local folder. `isRepository` is false
    // when the folder isn't yet a git repo (so the UI can show "init").
    void localStateReady(const QString& localPath,
                         bool                                  isRepository,
                         const QString&                        currentBranch,
                         const std::vector<ghm::git::StatusEntry>& entries,
                         const std::vector<ghm::git::RemoteInfo>&  remotes);

    void stageFinished  (bool ok, const QString& localPath, const QString& error);
    void unstageFinished(bool ok, const QString& localPath, const QString& error);
    void commitFinished (bool ok, const QString& localPath,
                         const QString& sha, const QString& error);
    void historyReady   (const QString& localPath,
                         const std::vector<ghm::git::CommitInfo>& commits);
    void remoteOpFinished(bool ok, const QString& localPath, const QString& error);

    // Generic progress for clone / push / pull / pushTo.
    void progress(const QString& phase, qint64 current, qint64 total);

private:
    GitHandler handler_;
};

} // namespace ghm::git
