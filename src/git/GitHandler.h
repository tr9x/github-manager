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

#include "git/SignatureVerifier.h"  // VerifyResult struct

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

    // Diff stats relative to first parent (or empty tree for root
    // commit). Populated by log() when computeStats=true, otherwise
    // left at -1 to indicate "not computed" — UI can then render a
    // placeholder like "…" instead of misleading zeros.
    //
    // Counts in libgit2 terms: insertions/deletions are line counts
    // matching what `git log --shortstat` produces; filesChanged is
    // the number of file deltas.
    int filesChanged{-1};
    int insertions{-1};
    int deletions{-1};

    // True when the commit has a "gpgsig" header (could be GPG, SSH,
    // or X.509). Set by log() always — it's a cheap check via
    // git_commit_extract_signature with no subprocess, so we just
    // populate it unconditionally. Lets the UI show a "signed but
    // not yet verified" prefix before the worker has time to run
    // gpg/ssh-keygen verifications.
    bool hasSignature{false};
};

// Detail about a single conflicted file — three sides of the merge:
// - "ancestor" (base): the common ancestor commit's version
// - "ours"   : what's at HEAD (the branch you were on when the merge
//              started)
// - "theirs" : the version being merged in
//
// `*Sha` strings are the blob OIDs; they're empty if that side
// represents "file did not exist" (e.g. add/add conflicts have no
// ancestor, delete/modify conflicts have no theirs blob, etc).
//
// `*Content` is the raw blob contents — populated on demand by
// loadConflictContents() because blobs can be large and we don't want
// to materialise them when the user only wants the file list.
struct ConflictEntry {
    QString path;            // repo-relative path of the conflicted file
    QString ancestorSha;     // base blob OID, empty if nonexistent
    QString oursSha;
    QString theirsSha;
    QByteArray ancestorContent;
    QByteArray oursContent;
    QByteArray theirsContent;
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

// One entry on the stash stack. Index 0 is the most-recently saved
// (matching the convention `git stash list` uses, e.g. stash@{0}).
struct StashEntry {
    int       index{0};       // stash@{N}
    QString   shortId;        // 7-char abbrev of the stash commit OID
    QString   message;        // "WIP on master: deadbeef commit summary"
    QDateTime when;           // stasher.when (committer time)
};

// One git tag. Lightweight tags have no annotation message (`message`
// stays empty); annotated tags carry the message the tagger wrote.
struct TagInfo {
    QString name;             // short name, e.g. "v1.2.3"
    QString targetSha;        // OID the tag points to
    QString message;          // empty for lightweight tags
    bool    isAnnotated{false};
};

// One entry in HEAD's reflog. Reflog is git's local journal of where
// HEAD moved — every commit, checkout, reset, merge, rebase appends
// one line. It's how `git reset --hard` is recoverable: the old SHA
// is still in the reflog (until `git gc` reaps it, default 90 days).
//
// Field semantics map to libgit2's git_reflog_entry:
//   oldSha:  HEAD's value BEFORE this operation (empty for the very
//            first entry — there was no prior state).
//   newSha:  HEAD's value AFTER this operation. This is what the user
//            would restore to via a soft/hard reset.
//   message: the human-readable reason ("commit: …", "reset: …",
//            "checkout: moving from main to feature", etc.)
//   when:    timestamp git wrote the entry, local time.
//   committerName/Email: who did the operation (typically the user).
struct ReflogEntry {
    QString   oldSha;
    QString   newSha;
    QString   message;
    QDateTime when;
    QString   committerName;
    QString   committerEmail;
};

// One entry from `git submodule status`. The repo's .gitmodules file
// lists the path/url pairs; the index records the SHA the parent
// commit pinned each submodule to; the working directory may or may
// not have the submodule cloned and checked out.
//
// `status` flags below are derived from libgit2's git_submodule_status_t
// bitmask, simplified to one human-readable state:
//
//   NotInitialized  — entry in .gitmodules but no .git in workdir
//                     (the most common state after a fresh clone)
//   UpToDate        — workdir HEAD matches recorded SHA
//   Modified        — workdir HEAD differs from recorded SHA, or
//                     uncommitted changes inside the submodule
//   UrlMismatch     — .gitmodules URL differs from .git/config URL
//                     (sync needed)
//   Missing         — recorded in index but not in .gitmodules
//                     (rare; usually means .gitmodules was hand-edited)
struct SubmoduleInfo {
    enum class Status {
        NotInitialized,
        UpToDate,
        Modified,
        UrlMismatch,
        Missing,
        Unknown,
    };

    QString  name;          // logical name, key in .gitmodules
    QString  path;          // workdir-relative path, e.g. "vendor/foo"
    QString  url;           // URL from .gitmodules (the "intended" one)
    QString  configUrl;     // URL from .git/config (the "active" one)
                            // — when they differ, sync is needed
    QString  recordedSha;   // SHA the parent commit pinned (from index)
    QString  workdirSha;    // current HEAD of the submodule (empty when not init'd)
    Status   status{Status::Unknown};
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

// SSH credentials for clone operations. When provided to clone(),
// these override the default ssh-agent path in the credential
// callback. Passphrase is optional — leave empty for unencrypted keys.
//
// We don't expose these for pull/push/fetch because those operations
// run against an already-cloned repo; if the repo was set up to use
// ssh-agent (which is the default), it'll keep working that way. If
// the user really needs explicit keys for those ops too, they can
// configure `core.sshCommand` in .git/config — out of our scope.
struct SshCredentials {
    QString keyPath;        // absolute path to private key
    QString publicKeyPath;  // optional; if empty, libssh2 derives from keyPath
    QString passphrase;     // empty for unencrypted keys

    bool isExplicit() const { return !keyPath.isEmpty(); }
};

// Commit signing configuration. When mode==None, commit() builds an
// unsigned commit the usual way (git_commit_create). When mode==Gpg
// or Ssh, commit() instead builds a commit buffer, runs gpg or
// ssh-keygen to sign it, then calls git_commit_create_with_signature
// to write the signed commit.
//
// We don't have a "fall back to unsigned on failure" mode — if signing
// is enabled and signing fails, the commit fails. Surprising silent
// fallback is worse than a clear error message.
struct SigningConfig {
    enum class Mode { None, Gpg, Ssh };

    Mode    mode{Mode::None};
    QString key;            // GPG key ID or SSH key path; depends on mode
};

// Thread-safety contract:
//
// All methods on GitHandler are reentrant. They share NO state between
// calls — every operation opens its own git_repository* on entry and
// frees it on return (RAII via the GitHandle wrappers in the .cpp).
// libgit2 itself is fine with multiple concurrent operations from
// different threads as long as they don't share the same repository
// object, which is exactly the contract this class enforces.
//
// In practice this means:
//   * Two threads can simultaneously call methods on the SAME GitHandler
//     instance — even targeting the SAME local path — without external
//     synchronization. libgit2 handles index/lock contention internally.
//   * GitWorker exploits this by running every async task as an
//     independent QtConcurrent job, while MainWindow occasionally calls
//     handler-side methods synchronously from the GUI thread (e.g. the
//     pre-dialog branch listing in onLocalBranchCreateRequested).
//
// IMPORTANT: this contract holds only as long as GitHandler stays
// stateless. If a future change adds a cache (e.g. memoising open
// repositories), the cache MUST either be thread-safe internally or
// the class needs to grow a mutex. Reviewers: enforce.
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
                    const ProgressFn& progress = {},
                    const SshCredentials& sshCreds = {});

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

    // Renames a local branch in place. Equivalent to `git branch -m
    // <oldName> <newName>`. Refuses to overwrite an existing branch
    // unless `force=true`. Renaming the currently-checked-out branch
    // is fine — HEAD's symbolic ref tracks the new name automatically.
    GitResult renameBranch(const QString& localPath,
                           const QString& oldName,
                           const QString& newName,
                           bool           force = false);

    // Fetches updates from a remote without touching the working
    // tree or the local branch's HEAD. Equivalent to `git fetch
    // <remote>`. After this returns, refs/remotes/<remote>/* point at
    // whatever the remote has, and ahead/behind counts on local
    // branches reflect the new state — but no merge/rebase happens.
    //
    // `pat` is the GitHub personal access token used as the HTTPS
    // password (same scheme as push/clone). Pass empty for public
    // repos that don't need auth.
    //
    // Optional `progress` callback reports transfer progress for
    // large refspecs — same shape as clone/pull's. Phase string is
    // "Receiving objects" for fetch.
    GitResult fetch(const QString& localPath,
                    const QString& remoteName,
                    const QString& pat = QString(),
                    const ProgressFn& progress = {});

    // Undo the last commit, leaving its changes staged in the index.
    // Equivalent to `git reset --soft HEAD~1`. Refuses to operate if
    // HEAD is already at the root commit (nothing to undo) or if
    // the repo is mid-merge/rebase (the operation would be ambiguous).
    GitResult undoLastCommit(const QString& localPath);

    // --- Reflog --------------------------------------------------------
    //
    // HEAD's reflog: git's local journal of where HEAD moved over time.
    // Acts as a recovery net for destructive operations (reset, hard
    // checkout, rebase) — the old SHA stays in the reflog and the
    // commit object stays in the object database until `git gc` reaps
    // it, defaults: 90 days for reachable, 30 for unreachable.
    //
    // Listing is read-only and safe. `maxCount` caps the result;
    // typical reflog is dozens of entries, but a long-running repo
    // can accumulate thousands.
    GitResult readHeadReflog(const QString& localPath,
                             int maxCount,
                             std::vector<ReflogEntry>& out);

    // Reset HEAD to the given SHA without touching the working tree
    // or index. Equivalent to `git reset --soft <sha>`. This is the
    // safest possible recovery: the previous HEAD's changes reappear
    // as staged, the user can inspect and decide what to do next.
    //
    // The SHA must reference an existing commit object — usually
    // pulled from a reflog entry. We don't validate that it was ever
    // an ancestor of any branch (which would defeat the recovery
    // use case where the commit is currently unreachable).
    GitResult softResetTo(const QString& localPath, const QString& sha);

    // --- Stash --------------------------------------------------------
    //
    // Mirrors the git CLI's stash subcommands. Stashes are stored on a
    // hidden ref refs/stash; the user-facing index 0 corresponds to
    // the top of the stack (most recent push).
    //
    // Save uses the configured author identity. Pass an empty message
    // to let libgit2 auto-generate the standard "WIP on <branch>: …"
    // message. include* flags follow `git stash push` defaults:
    // includeUntracked covers untracked files; keepIndex preserves the
    // staged-but-not-committed area in the working tree.

    GitResult stashSave(const QString& localPath,
                        const QString& message,
                        bool           includeUntracked,
                        bool           keepIndex,
                        const QString& authorName,
                        const QString& authorEmail);

    GitResult stashList(const QString& localPath,
                        std::vector<StashEntry>& out);

    GitResult stashApply(const QString& localPath, int index);
    GitResult stashPop  (const QString& localPath, int index);
    GitResult stashDrop (const QString& localPath, int index);

    // --- Tags ---------------------------------------------------------
    //
    // Tags can be lightweight (just a ref pointing at an OID) or
    // annotated (a real tag object with author + message). Annotated
    // tags require an author identity; lightweight tags don't.

    GitResult listTags(const QString& localPath, std::vector<TagInfo>& out);

    // Empty `message` creates a lightweight tag at HEAD; non-empty
    // creates an annotated tag using the supplied identity.
    //
    // When signingConfig.mode != None AND message is non-empty,
    // the tag is signed (GPG only — SSH tag signing not implemented;
    // libgit2 has no SSH-tag-aware API, would need full manual byte
    // construction with sshsig header). For SSH mode passed here,
    // the result is an error explaining the limitation.
    //
    // Lightweight tags (empty message) ignore signingConfig — tags
    // without an annotation object can't carry a signature.
    GitResult createTag(const QString& localPath,
                        const QString& name,
                        const QString& message,
                        const QString& authorName,
                        const QString& authorEmail,
                        const SigningConfig& signingConfig = {});

    GitResult deleteTag(const QString& localPath, const QString& name);

    // --- Submodules ---------------------------------------------------
    //
    // We expose three operations:
    //
    //   listSubmodules()       — read .gitmodules + libgit2's
    //                            git_submodule_foreach + per-submodule
    //                            status. Populates a SubmoduleInfo per
    //                            entry. Cheap; no network.
    //
    //   initAndUpdateSubmodule()  — clone the submodule into its
    //                               recorded path and check out the
    //                               recorded SHA. Network op; uses
    //                               ssh-agent for SSH URLs (no explicit
    //                               key path for submodules — power
    //                               users can add to agent first).
    //
    //   updateSubmodule()      — fetch + checkout the recorded SHA on
    //                            an already-initialized submodule.
    //                            Used when the parent commit bumped
    //                            the submodule to a new SHA.
    //
    //   syncSubmoduleUrl()     — copy URL from .gitmodules into
    //                            .git/config (equivalent to
    //                            `git submodule sync`). No network.
    //                            Run when the project moves a
    //                            submodule's upstream URL.
    //
    // We deliberately don't expose `add` or `rm` — those need a more
    // involved workflow (write .gitmodules, stage, commit) and are
    // less common than init/update/sync.

    GitResult listSubmodules(const QString& localPath,
                             std::vector<SubmoduleInfo>& out);

    // For `name`, pass SubmoduleInfo::name (the .gitmodules key, not
    // the path — same as `git submodule init <name>`). Token is used
    // when the URL is HTTPS-with-PAT.
    GitResult initAndUpdateSubmodule(const QString& localPath,
                                      const QString& name,
                                      const QString& token,
                                      const ProgressFn& progress = {});

    GitResult updateSubmodule(const QString& localPath,
                              const QString& name,
                              const QString& token,
                              const ProgressFn& progress = {});

    // Network ops (initAndUpdate/update) above now accept an optional
    // sshCreds parameter — symmetric to clone(). When set, the
    // credential callback uses the explicit key + passphrase instead
    // of falling through to ssh-agent. Default {} preserves existing
    // ssh-agent behaviour for callers that don't want to plumb creds.
    GitResult initAndUpdateSubmoduleWithCreds(const QString& localPath,
                                                const QString& name,
                                                const QString& token,
                                                const SshCredentials& sshCreds,
                                                const ProgressFn& progress = {});

    GitResult updateSubmoduleWithCreds(const QString& localPath,
                                        const QString& name,
                                        const QString& token,
                                        const SshCredentials& sshCreds,
                                        const ProgressFn& progress = {});

    GitResult syncSubmoduleUrl(const QString& localPath,
                                const QString& name);

    // Add a new submodule. Three-step internally:
    //   1. git_submodule_add_setup     — writes .gitmodules entry,
    //                                    creates subrepo dir
    //   2. git_clone (via submodule's repo) — actually pulls the
    //                                          submodule's commits
    //   3. git_submodule_add_finalize  — stages .gitmodules + gitlink
    //
    // The parent repo is left with uncommitted changes after this —
    // the user must commit the new .gitmodules + gitlink themselves.
    // We surface this as the documented behaviour (matches `git
    // submodule add`).
    //
    // Validation:
    //   * subPath must not already exist
    //   * url must be non-empty
    //
    // The token is used for HTTPS clones; sshCreds for SSH explicit
    // keys (same as clone()). Default empty sshCreds → ssh-agent.
    GitResult addSubmodule(const QString& localPath,
                           const QString& url,
                           const QString& subPath,
                           const QString& token,
                           const SshCredentials& sshCreds = {},
                           const ProgressFn& progress = {});

    // Remove a submodule. libgit2 doesn't expose `git submodule rm`
    // natively, so we do the five-step dance manually:
    //   1. Edit .gitmodules — remove the submodule.<name>.* section
    //   2. Edit .git/config  — remove the same section
    //   3. Remove the gitlink from the index (so git status doesn't
    //      keep showing it as deleted-but-tracked)
    //   4. Delete .git/modules/<name>/ (the embedded repo)
    //   5. Delete the submodule's workdir directory
    //
    // After this, the parent repo has staged deletions of the
    // gitlink + .gitmodules edits which the user must commit.
    //
    // We do steps in this order so that if any one fails partway,
    // the user can still see what's left and recover. We do NOT
    // try to roll back partial state — submodule removal is
    // inherently a multi-file edit and partial recovery would
    // require copying files around, which is worse than the
    // current "leave it where it is, user fixes manually" approach.
    GitResult removeSubmodule(const QString& localPath,
                              const QString& name);

    // --- Conflict resolution ------------------------------------------
    //
    // Conflicts arise after a merge/rebase/cherry-pick/stash-pop that
    // libgit2 couldn't resolve automatically. The repository is left
    // in a "conflict" state: the index has multiple stages for each
    // conflicted file, the working tree has files with merge markers
    // (<<<<<<<, =======, >>>>>>>), and HEAD points at the unfinished
    // merge.
    //
    // The flow we expose:
    //   listConflicts()   — what's currently conflicted, plus blob OIDs
    //   loadConflictBlobs() — fetch contents for one entry on demand
    //   markResolved()    — stage the working-tree version of a path
    //                       and remove conflict entries from the index
    //   abortMerge()      — `git merge --abort`: reset to pre-merge state

    GitResult listConflicts(const QString& localPath,
                            std::vector<ConflictEntry>& out);

    // Populates ancestorContent/oursContent/theirsContent on the
    // supplied entry using the OIDs already stored. Cheap to call
    // — passes blobs through libgit2's object cache.
    GitResult loadConflictBlobs(const QString& localPath,
                                ConflictEntry& entry);

    // Stages the current working-tree version of `path` and removes
    // its conflict stage entries. Use this after the user has edited
    // the file and removed merge markers. Does NOT commit — caller
    // commits separately via the existing commit flow once all
    // conflicts are resolved.
    GitResult markResolved(const QString& localPath, const QString& path);

    // Cancels an in-progress merge, restoring HEAD/index/working tree
    // to the pre-merge state. Equivalent to `git merge --abort`.
    GitResult abortMerge(const QString& localPath);

    // True if the repo is currently mid-merge (or rebase, cherry-pick,
    // etc — any state where MERGE_HEAD or equivalent is present). UI
    // uses this to decide whether to show the "resolve conflicts" CTA.
    bool isMerging(const QString& localPath);

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
    //
    // When `signingConfig.mode != None`, the commit is signed using
    // gpg or ssh-keygen (subprocess). Signing failures cause the
    // entire commit to fail with the signer's error message — we
    // don't silently fall back to unsigned, because the user opted
    // into signing and a quiet unsigned commit would surprise them
    // (and could leak unsigned commits into a "signed-only" branch).
    GitResult commit(const QString& localPath,
                     const QString& message,
                     const QString& authorName,
                     const QString& authorEmail,
                     QString* outSha = nullptr,
                     const SigningConfig& signingConfig = {});

    // Verify a commit's signature. Extracts the gpgsig header via
    // git_commit_extract_signature, detects format (GPG/SSH from the
    // armor header), spawns the appropriate verifier (gpg --verify
    // or ssh-keygen -Y verify), and returns the verdict.
    //
    // Cost: one subprocess per call. For History tab use, callers
    // MUST batch or cache — verifying a 200-commit history one at a
    // time would block the worker for seconds.
    //
    // `allowedSignersPath` is only consulted for SSH signatures.
    // Pass empty when verifying GPG-only or when no allowed_signers
    // file is configured (SSH falls back to check-novalidate).
    GitResult verifyCommitSignature(const QString& localPath,
                                     const QString& sha,
                                     const QString& allowedSignersPath,
                                     VerifyResult& out);

    // `git log` — most recent first, capped at `maxCount` (0 = no cap).
    // When `computeStats` is true, each CommitInfo gets its
    // filesChanged/insertions/deletions populated from a diff against
    // the first parent (or empty tree for root commit). That's
    // significantly slower for large repos — roughly +10-30ms per
    // commit — so callers that don't need stats should leave it false.
    //
    // When `afterSha` is non-empty, the walk starts from THAT commit
    // (inclusive of its parents but NOT itself). This is how
    // "load more" pagination works — pass the SHA of the oldest
    // commit currently shown and you get the next page of older
    // commits without re-walking the already-visible ones.
    GitResult log(const QString& localPath, int maxCount,
                  std::vector<CommitInfo>& out,
                  bool computeStats = false,
                  const QString& afterSha = QString());

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

    // Diff between two arbitrary commits — what `git diff <a> <b>`
    // would produce. Order matters: the result describes the changes
    // that turn `shaA`'s tree into `shaB`'s tree (additions in B
    // appear with status 'A', deletions with 'D', etc.).
    //
    // Either SHA can be empty to mean "the empty tree" — useful for
    // showing a single root commit's full contents (handled internally
    // by commitDiff() but exposed here in case a caller wants the
    // wider primitive).
    GitResult commitDiffBetween(const QString& localPath,
                                const QString& shaA,
                                const QString& shaB,
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
