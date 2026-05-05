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

// Per-branch metadata. Used by the branch picker so it can display
// the upstream relationship and ahead/behind counts inline.
struct BranchInfo {
    QString name;             // short name, e.g. "master"
    bool    isCurrent{false}; // checked out HEAD points here
    QString upstreamName;     // "origin/master" or empty
    int     ahead{0};         // commits this branch has that upstream doesn't
    int     behind{0};        // commits upstream has that this branch doesn't
    bool    hasUpstream{false};
};

// One line inside a diff hunk. `origin` mirrors libgit2's classification:
//   ' '  context (unchanged)
//   '+'  added line (present in new, absent in old)
//   '-'  removed line (present in old, absent in new)
//   '\n' newline-at-EOF marker (rare, can be ignored by UI)
// `oldLineNo` and `newLineNo` are 1-based; -1 when the line doesn't
// exist on that side.
struct DiffLine {
    char    origin{' '};
    int     oldLineNo{-1};
    int     newLineNo{-1};
    QString content;        // text without origin char or trailing \n
};

struct DiffHunk {
    QString header;         // raw "@@ -1,3 +1,5 @@ funcName" line
    int     oldStart{0};
    int     oldLines{0};
    int     newStart{0};
    int     newLines{0};
    std::vector<DiffLine> lines;
};

// Full diff for a single file. Empty hunks + binary == binary file.
struct FileDiff {
    QString path;           // current path
    QString oldPath;        // for renames; otherwise empty
    char    status{' '};    // 'A','M','D','R','T','U','?'
    bool    isBinary{false};
    bool    isUntracked{false};
    int     additions{0};
    int     deletions{0};
    std::vector<DiffHunk> hunks;
};

// What we're diffing against. For the typical Changes-tab use-case
// pass HeadToWorkdir — that combines staged + unstaged changes the
// way `git diff HEAD` does, and with the right flags also shows
// untracked file contents.
enum class DiffScope {
    HeadToWorkdir,    // git diff HEAD (combined view)
    HeadToIndex,      // git diff --cached  (staged only)
    IndexToWorkdir,   // git diff           (unstaged only, plus untracked content)
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

    // Like localBranches() but returns full metadata: upstream
    // relationship and ahead/behind counts. Used by the branch picker.
    GitResult listLocalBranches(const QString& localPath,
                                std::vector<BranchInfo>& out);

    GitResult checkoutBranch(const QString& localPath, const QString& branch);

    // Creates a local branch named `name` pointing at the current HEAD
    // (or any ref-like spec if needed later — but for the UI we only
    // ever branch from HEAD). Pass `checkoutAfter=true` to immediately
    // switch to the new branch.
    GitResult createBranch(const QString& localPath,
                           const QString& name,
                           bool           checkoutAfter);

    // Deletes a local branch. Refuses to delete the currently-checked-
    // out branch. With `force=false`, also refuses if the branch isn't
    // merged into HEAD (mirrors `git branch -d` vs `-D`).
    GitResult deleteBranch(const QString& localPath,
                           const QString& name,
                           bool           force);

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

    // Computes the diff for a single file, in the chosen scope. The
    // path is repository-relative (e.g. "src/foo.cpp"). When the scope
    // is HeadToWorkdir, untracked files are included with their full
    // content rendered as additions.
    GitResult fileDiff(const QString& localPath,
                       const QString& repoRelPath,
                       DiffScope      scope,
                       FileDiff&      out);

    // Computes the diff a commit introduces — equivalent to `git show
    // <sha>`. For a commit with one parent (the common case), this is
    // diff(parent.tree, this.tree). For a root commit (no parent), it
    // becomes diff(emptyTree, this.tree), so every file shows up as an
    // addition.
    //
    // Each file changed by the commit becomes one FileDiff in `out`.
    // Merge commits are diffed against their first parent, mirroring
    // `git show` defaults — combined diffs (`-c`) aren't covered.
    GitResult commitDiff(const QString& localPath,
                         const QString& sha,
                         std::vector<FileDiff>& out);

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
