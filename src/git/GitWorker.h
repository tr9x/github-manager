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
    //
    // Optional `sshCreds` overrides the default ssh-agent credential
    // path for SSH URLs. When the URL is HTTPS or sshCreds is empty,
    // it's ignored. Passed by value because we capture it into the
    // worker lambda by copy.
    void clone(const QString& url, const QString& localPath,
               const QString& token,
               const ghm::git::SshCredentials& sshCreds = {});
    void pull (const QString& localPath, const QString& token);
    void push (const QString& localPath, const QString& token);
    void refreshStatus(const QString& localPath);
    void switchBranch(const QString& localPath, const QString& branch);
    void listBranches(const QString& localPath);

    // Bogata wersja listy gałęzi: pełne BranchInfo z upstream + ahead/behind.
    void listBranchInfos(const QString& localPath);

    // git branch <name> [+ checkout <name>].
    void createBranch(const QString& localPath,
                      const QString& name,
                      bool           checkoutAfter);

    // git branch -d / -D <name>.
    void deleteBranch(const QString& localPath,
                      const QString& name,
                      bool           force);

    // git branch -m <oldName> <newName>. Renames a local branch in
    // place, including the currently-checked-out one (HEAD's symbolic
    // ref tracks the new name automatically).
    void renameBranch(const QString& localPath,
                      const QString& oldName,
                      const QString& newName);

    // Fetches updates from a named remote without modifying the
    // working tree. Result delivered via fetchFinished. The PAT
    // (when needed for HTTPS auth) is supplied at call time so
    // the worker doesn't have to know about the GitHub session.
    void fetchRemote(const QString& localPath,
                     const QString& remoteName,
                     const QString& pat);

    // Undo the last commit (`git reset --soft HEAD~1`). Result
    // delivered via undoLastCommitFinished.
    void undoLastCommit(const QString& localPath);

    // Read HEAD's reflog asynchronously. maxCount caps the size of
    // the returned list. Result via reflogReady.
    void loadReflog(const QString& localPath, int maxCount);

    // Soft-reset HEAD to the given SHA (typically picked from the
    // reflog). Result via softResetFinished. Working tree is left
    // untouched; the changes from the abandoned commits reappear
    // as staged so the user can review before re-committing.
    void softResetTo(const QString& localPath, const QString& sha);

    // -- Stash ---------------------------------------------------------
    void stashSave (const QString& localPath,
                    const QString& message,
                    bool           includeUntracked,
                    bool           keepIndex,
                    const QString& authorName,
                    const QString& authorEmail);
    void stashList (const QString& localPath);
    void stashApply(const QString& localPath, int index);
    void stashPop  (const QString& localPath, int index);
    void stashDrop (const QString& localPath, int index);

    // -- Tags ----------------------------------------------------------
    void listTags  (const QString& localPath);
    void createTag (const QString& localPath,
                    const QString& name,
                    const QString& message,
                    const QString& authorName,
                    const QString& authorEmail,
                    const ghm::git::SigningConfig& signingConfig = {});
    void deleteTag (const QString& localPath, const QString& name);

    // -- Submodules -----------------------------------------------------
    //
    // listSubmodules is cheap (no network) — emits submodulesReady
    // with the populated vector. The three mutating ops run on the
    // worker thread because initAndUpdate/update do network I/O;
    // syncSubmoduleUrl is fast but lives here for symmetry.
    //
    // All three emit submoduleOpFinished — the host re-runs
    // listSubmodules afterwards to refresh the UI. We don't try to
    // surgically update the existing list because submodule status
    // depends on multiple git objects, and "fetch + re-render" is
    // simpler and fast enough.
    void listSubmodules        (const QString& localPath);
    void initAndUpdateSubmodule(const QString& localPath,
                                 const QString& name,
                                 const QString& token,
                                 const ghm::git::SshCredentials& sshCreds = {});
    void updateSubmodule       (const QString& localPath,
                                 const QString& name,
                                 const QString& token,
                                 const ghm::git::SshCredentials& sshCreds = {});
    void syncSubmoduleUrl      (const QString& localPath,
                                 const QString& name);

    // Add a new submodule. Network op (clones the submodule's
    // contents). Emits submoduleOpFinished with operation="add" on
    // completion. Host should re-run listSubmodules afterwards.
    void addSubmodule          (const QString& localPath,
                                 const QString& url,
                                 const QString& subPath,
                                 const QString& token,
                                 const ghm::git::SshCredentials& sshCreds = {});

    // Remove a submodule. Destructive — host MUST confirm with the
    // user before calling. Emits submoduleOpFinished with
    // operation="remove" on completion.
    void removeSubmodule       (const QString& localPath,
                                 const QString& name);

    // -- Conflict resolution -------------------------------------------
    // listConflicts returns immediately via conflictsReady. Use
    // loadConflictBlobs to fetch the actual file contents for one
    // entry — kept separate so listing 50 conflicted files doesn't
    // pull 50 blobs we'll never display.
    void listConflicts     (const QString& localPath);
    void loadConflictBlobs (const QString& localPath,
                            const ghm::git::ConflictEntry& entry);
    void markResolved      (const QString& localPath, const QString& path);
    void abortMerge        (const QString& localPath);

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
                       const QString& authorEmail,
                       const ghm::git::SigningConfig& signingConfig = {});

    // Load commit history for the repo at `localPath`.
    //
    // Two paging modes:
    //   afterSha empty → initial fetch; emits historyReady with
    //                    isAppend=false. The receiver should REPLACE
    //                    its current list with the result.
    //   afterSha set   → next-page fetch; emits historyReady with
    //                    isAppend=true and starting AFTER the given
    //                    commit. The receiver should APPEND the
    //                    result to its current list.
    //
    // When the returned vector has fewer than maxCount entries, the
    // caller has reached the end of history (no more older commits).
    void loadHistory(const QString& localPath, int maxCount,
                     const QString& afterSha = QString());

    // Verify a single commit's signature. The expected use is lazy
    // verification — the History tab calls this only for commits
    // currently visible, and the result is cached so a re-scroll
    // doesn't trigger a re-verify.
    //
    // allowedSignersPath: path to an OpenSSH allowed_signers file,
    // or empty when none is configured. Only used for SSH-signed
    // commits; GPG-signed ones ignore it.
    void verifyCommitSignature(const QString& localPath,
                                const QString& sha,
                                const QString& allowedSignersPath);

    // Compute a unified diff for a single file, in the requested scope.
    // Result lands in fileDiffReady(); on failure the FileDiff in the
    // signal is empty and `error` is populated.
    void loadFileDiff(const QString& localPath,
                      const QString& repoRelPath,
                      ghm::git::DiffScope scope);

    // Compute the diffs that a single commit introduces — equivalent
    // to `git show <sha>`. Result is delivered as a vector of FileDiffs
    // (one per changed file) via commitDiffReady().
    void loadCommitDiff(const QString& localPath, const QString& sha);

    // Compute the diff between two arbitrary commits (`git diff a b`).
    // Reuses the commitDiffReady signal, with `sha` set to a synthetic
    // "shaA..shaB" identifier so the host can distinguish the result
    // from a single-commit diff.
    void loadCommitDiffBetween(const QString& localPath,
                               const QString& shaA,
                               const QString& shaB);

    void addRemote(const QString& localPath,
                   const QString& name, const QString& url);
    void removeRemote(const QString& localPath, const QString& name);

    void pushTo(const QString& localPath,
                const QString& remoteName,
                const QString& branch,
                bool setUpstreamAfter,
                const QString& token);

    // Direct (synchronous) access to the underlying GitHandler — useful
    // when a dialog needs branch metadata *before* opening, where the
    // round-trip of an async call would feel laggy. Callers must NOT
    // invoke long-running operations through this from the GUI thread.
    GitHandler&       handler()       { return handler_; }
    const GitHandler& handler() const { return handler_; }

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

    // Rich version with upstream/ahead/behind info.
    void branchInfosReady(const QString& localPath,
                          const std::vector<ghm::git::BranchInfo>& branches);

    // Both create and delete share the same finished signal: caller
    // identifies the operation by which slot it called. `name` is the
    // affected branch.
    void branchCreated(bool ok, const QString& localPath,
                       const QString& name, const QString& error);
    void branchDeleted(bool ok, const QString& localPath,
                       const QString& name, const QString& error);
    void branchRenamed(bool ok, const QString& localPath,
                       const QString& oldName, const QString& newName,
                       const QString& error);

    // Fetch finished. `remoteName` is what we asked for; on success
    // the host should re-read local state to surface updated
    // ahead/behind counts.
    void fetchFinished(bool ok, const QString& localPath,
                       const QString& remoteName, const QString& error);

    // Undo last commit finished. Working tree is unchanged; the
    // committed changes are now in the index ready to be edited
    // and re-committed.
    void undoLastCommitFinished(bool ok, const QString& localPath,
                                const QString& error);

    // Reflog list ready for display.
    void reflogReady(const QString& localPath,
                     const std::vector<ghm::git::ReflogEntry>& entries);

    // Soft-reset to a reflog entry completed. Caller should refresh
    // local state on success — staged changes will be populated.
    void softResetFinished(bool ok, const QString& localPath,
                           const QString& sha, const QString& error);

    // Stash
    void stashOpFinished(bool ok, const QString& localPath,
                         const QString& operation,  // "save" | "apply" | "pop" | "drop"
                         const QString& error);
    void stashListReady (const QString& localPath,
                         const std::vector<ghm::git::StashEntry>& entries);

    // Tags
    void tagOpFinished  (bool ok, const QString& localPath,
                         const QString& operation,  // "create" | "delete"
                         const QString& name,
                         const QString& error);
    void tagsReady      (const QString& localPath,
                         const std::vector<ghm::git::TagInfo>& tags);

    // Submodules
    void submodulesReady     (const QString& localPath,
                              const std::vector<ghm::git::SubmoduleInfo>& subs);
    void submoduleOpFinished (bool ok, const QString& localPath,
                              const QString& operation,  // "init+update" | "update" | "sync"
                              const QString& name,
                              const QString& error);

    // Conflict resolution
    void conflictsReady       (const QString& localPath,
                               const std::vector<ghm::git::ConflictEntry>& entries);
    void conflictBlobsReady   (const QString& localPath,
                               const ghm::git::ConflictEntry& entryWithBlobs);
    void conflictOpFinished   (bool ok, const QString& localPath,
                               const QString& operation,  // "resolve" | "abort"
                               const QString& path,       // populated for "resolve"
                               const QString& error);

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
                         const std::vector<ghm::git::CommitInfo>& commits,
                         bool isAppend);
    void signatureVerified(const QString& localPath,
                            const QString& sha,
                            const ghm::git::VerifyResult& result);
    void fileDiffReady  (const QString& localPath,
                         const QString& repoRelPath,
                         const ghm::git::FileDiff& diff,
                         const QString& error);
    void commitDiffReady(const QString& localPath,
                         const QString& sha,
                         const std::vector<ghm::git::FileDiff>& files,
                         const QString& error);
    void remoteOpFinished(bool ok, const QString& localPath, const QString& error);

    // Generic progress for clone / push / pull / pushTo.
    void progress(const QString& phase, qint64 current, qint64 total);

private:
    GitHandler handler_;
};

} // namespace ghm::git
