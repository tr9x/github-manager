#pragma once

// GitHandler - synchronous libgit2 operations.
//
// Methods block, so they're always called from a worker thread (see
// GitWorker). Errors come back as a structured result rather than via
// exceptions — Qt's signal/slot machinery doesn't play nicely with
// exceptions across threads.

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <functional>
#include <memory>
#include <vector>

namespace ghm::git {

struct GitResult {
    bool    ok{false};
    QString error;          // empty when ok
    int     code{0};        // raw libgit2 error code, for logging

    static GitResult success() { return {true, {}, 0}; }
    static GitResult failure(QString msg, int code = -1) {
        return {false, std::move(msg), code};
    }
};

// Aggregate counts for the existing summary view (shown next to the
// branch label in the GitHub-clone detail panel).
struct StatusSummary {
    int  modified{0};
    int  added{0};
    int  deleted{0};
    int  untracked{0};
    int  conflicted{0};
    int  ahead{0};          // commits ahead of upstream
    int  behind{0};         // commits behind upstream

    bool isClean() const {
        return modified + added + deleted + untracked + conflicted == 0;
    }
};

// Per-file status entry, used by the local-folder Changes view.
//
// `indexFlag` describes the diff between HEAD and the index (i.e. what
// will be in the next commit if you commit now). `worktreeFlag` describes
// the diff between the index and the working tree (i.e. unstaged edits).
//
// Letters mirror `git status --short`:
//   'M' modified, 'A' added, 'D' deleted, 'R' renamed, 'T' typechange,
//   '?' untracked, '!' ignored, 'U' conflicted, ' ' unchanged.
struct StatusEntry {
    QString path;
    QString oldPath;        // populated on rename
    char    indexFlag{' '};
    char    worktreeFlag{' '};
    bool    isStaged{false};      // anything in indexFlag != ' '
    bool    isUnstaged{false};    // anything in worktreeFlag != ' ' && != '?'
    bool    isUntracked{false};
    bool    isConflicted{false};
};

struct CommitInfo {
    QString id;             // full SHA
    QString shortId;        // first 7 chars
    QString summary;        // first line of message
    QString message;        // full message
    QString authorName;
    QString authorEmail;
    QDateTime when;         // local time
    QStringList parents;    // parent SHAs
};

struct RemoteInfo {
    QString name;
    QString url;
    QString pushUrl;        // empty if same as url
};

// Optional progress callback for long-running operations.
// `phase` is a short label ("Receiving objects", "Resolving deltas").
// `current`/`total` describe units appropriate to the phase.
using ProgressFn = std::function<void(const QString& phase,
                                       qint64 current,
                                       qint64 total)>;

class GitHandler {
public:
    // Initialises libgit2. Construct one of these per process; safe to
    // construct multiple times (libgit2 ref-counts internally).
    GitHandler();
    ~GitHandler();

    GitHandler(const GitHandler&) = delete;
    GitHandler& operator=(const GitHandler&) = delete;

    // --- Discovery ----------------------------------------------------

    // Returns true if `localPath` contains a `.git` directory recognised
    // by libgit2. Cheap; useful for deciding whether to show "init" UI.
    bool isRepository(const QString& localPath) const;

    // --- Local repository creation ------------------------------------

    // `git init`. Creates a fresh repo at `localPath` whose HEAD points
    // at refs/heads/<initialBranch> (still unborn — first commit will
    // create the branch). Default initial branch is "master".
    GitResult init(const QString& localPath,
                   const QString& initialBranch = QStringLiteral("master"));

    // --- Repository-level (existing GitHub-clone flow) ----------------

    GitResult clone(const QString& url,
                    const QString& localPath,
                    const QString& token,
                    const ProgressFn& progress = {});

    GitResult open(const QString& localPath);

    QString currentBranch(const QString& localPath, GitResult* outErr = nullptr);
    QStringList localBranches(const QString& localPath, GitResult* outErr = nullptr);

    GitResult checkoutBranch(const QString& localPath, const QString& branch);

    StatusSummary status(const QString& localPath, GitResult* outErr = nullptr);

    GitResult pull(const QString& localPath, const QString& token,
                   const ProgressFn& progress = {});

    // Push the current branch to "origin" (used by the GitHub-clone view).
    GitResult push(const QString& localPath, const QString& token,
                   const ProgressFn& progress = {});

    // --- Local-folder workflow ----------------------------------------

    // Per-file status, the basis of the Changes view.
    GitResult statusEntries(const QString& localPath,
                            std::vector<StatusEntry>& out);

    // `git add .` — stages every change including new files.
    GitResult stageAll(const QString& localPath);

    // `git add <path>...` for the supplied paths (relative to repo root).
    GitResult stagePaths(const QString& localPath, const QStringList& paths);

    // `git reset HEAD <path>...` — moves entries out of the index back
    // into the working tree. Handles unborn HEAD (first-commit case).
    GitResult unstagePaths(const QString& localPath, const QStringList& paths);

    // `git commit -m`. Creates a commit from whatever's currently in the
    // index. Both `authorName` and `authorEmail` must be non-empty.
    // Returns the new commit's full SHA in `outSha` on success.
    GitResult commit(const QString& localPath,
                     const QString& message,
                     const QString& authorName,
                     const QString& authorEmail,
                     QString* outSha = nullptr);

    // `git log` — most recent first, capped at `maxCount` (0 = no cap).
    GitResult log(const QString& localPath, int maxCount,
                  std::vector<CommitInfo>& out);

    // --- Remotes ------------------------------------------------------

    GitResult listRemotes(const QString& localPath,
                          std::vector<RemoteInfo>& out);

    // `git remote add <name> <url>`. Fails if a remote with that name
    // already exists.
    GitResult addRemote(const QString& localPath,
                        const QString& name,
                        const QString& url);

    GitResult removeRemote(const QString& localPath, const QString& name);

    // Configures `branch.<branch>.remote` and `branch.<branch>.merge`,
    // i.e. what `git push -u origin <branch>` does in addition to the
    // push itself.
    GitResult setUpstream(const QString& localPath,
                          const QString& branch,
                          const QString& remoteName);

    // Push `branch` to `remoteName`. If `setUpstreamAfter` is true, the
    // upstream is wired up on success.
    GitResult pushBranch(const QString& localPath,
                         const QString& remoteName,
                         const QString& branch,
                         bool setUpstreamAfter,
                         const QString& token,
                         const ProgressFn& progress = {});
};

} // namespace ghm::git
