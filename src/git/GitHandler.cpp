#include "git/GitHandler.h"

#include <git2.h>

#include <QByteArray>
#include <QFileInfo>
#include <QDir>
#include <atomic>
#include <memory>
#include <vector>

namespace ghm::git {

namespace {

std::atomic<int> g_initCount{0};

GitResult lastError(const QString& context, int code)
{
    const git_error* e = git_error_last();
    QString msg;
    if (e && e->message) {
        msg = QStringLiteral("%1: %2").arg(context, QString::fromUtf8(e->message));
    } else {
        msg = QStringLiteral("%1 (code %2)").arg(context).arg(code);
    }
    return GitResult::failure(std::move(msg), code);
}

// ----- RAII handle wrappers ------------------------------------------------

template <typename T, void (*Free)(T*)>
struct GitHandle {
    T* p{nullptr};
    GitHandle() = default;
    explicit GitHandle(T* raw) : p(raw) {}
    ~GitHandle() { if (p) Free(p); }
    GitHandle(const GitHandle&) = delete;
    GitHandle& operator=(const GitHandle&) = delete;
    GitHandle(GitHandle&& o) noexcept : p(o.p) { o.p = nullptr; }
    GitHandle& operator=(GitHandle&& o) noexcept {
        if (this != &o) { if (p) Free(p); p = o.p; o.p = nullptr; }
        return *this;
    }
    T*  get() const { return p; }
    T** out()       { return &p; }
};

using RepoHandle      = GitHandle<git_repository,    git_repository_free>;
using RemoteHandle    = GitHandle<git_remote,        git_remote_free>;
using ReferenceHandle = GitHandle<git_reference,     git_reference_free>;
using ObjectHandle    = GitHandle<git_object,        git_object_free>;
using StatusListH     = GitHandle<git_status_list,   git_status_list_free>;
using AnnotatedH      = GitHandle<git_annotated_commit, git_annotated_commit_free>;
using BranchIterH     = GitHandle<git_branch_iterator,git_branch_iterator_free>;
using IndexHandle     = GitHandle<git_index,         git_index_free>;
using TreeHandle      = GitHandle<git_tree,          git_tree_free>;
using CommitHandle    = GitHandle<git_commit,        git_commit_free>;
using SignatureHandle = GitHandle<git_signature,     git_signature_free>;
using RevwalkHandle   = GitHandle<git_revwalk,       git_revwalk_free>;
using ConfigHandle    = GitHandle<git_config,        git_config_free>;

// git_strarray needs its own free and is ALSO a value type, so we use a
// scoped helper rather than the template.
struct StrArrayScope {
    git_strarray a{};
    ~StrArrayScope() { git_strarray_dispose(&a); }
};

// ----- Credential & progress callbacks -------------------------------------

struct CallbackCtx {
    QByteArray  token;          // ASCII PAT, kept alive for callback duration
    ProgressFn  progress;
};

extern "C" int credCb(git_credential** out, const char* /*url*/,
                       const char* username_from_url, unsigned int allowed_types,
                       void* payload)
{
    auto* ctx = static_cast<CallbackCtx*>(payload);
    if (!ctx || ctx->token.isEmpty()) return GIT_EUSER;

    if (allowed_types & GIT_CREDENTIAL_USERNAME) {
        return git_credential_username_new(out,
            username_from_url ? username_from_url : "x-access-token");
    }
    if (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) {
        // GitHub accepts the PAT as the password with any non-empty
        // username for HTTPS basic auth.
        return git_credential_userpass_plaintext_new(out,
            "x-access-token", ctx->token.constData());
    }
    return GIT_EUSER;
}

extern "C" int transferCb(const git_indexer_progress* stats, void* payload)
{
    auto* ctx = static_cast<CallbackCtx*>(payload);
    if (ctx && ctx->progress) {
        ctx->progress(QStringLiteral("Receiving objects"),
                      stats->received_objects,
                      stats->total_objects);
    }
    return 0;
}

extern "C" int pushTransferCb(unsigned int current, unsigned int total,
                              size_t /*bytes*/, void* payload)
{
    auto* ctx = static_cast<CallbackCtx*>(payload);
    if (ctx && ctx->progress) {
        ctx->progress(QStringLiteral("Pushing objects"), current, total);
    }
    return 0;
}

// ----- Helpers -------------------------------------------------------------

GitResult openRepo(const QString& path, RepoHandle& out)
{
    const QByteArray p = path.toUtf8();
    const int rc = git_repository_open(out.out(), p.constData());
    if (rc != 0) return lastError(QStringLiteral("Open repository"), rc);
    return GitResult::success();
}

GitResult fastForwardToOriginHead(git_repository* repo, const QString& branchShort)
{
    git_oid fetchOid;
    QByteArray refStr = QStringLiteral("refs/remotes/origin/%1").arg(branchShort).toUtf8();
    int rc = git_reference_name_to_id(&fetchOid, repo, refStr.constData());
    if (rc != 0) return lastError(QStringLiteral("Resolve origin/%1").arg(branchShort), rc);

    AnnotatedH theirs;
    rc = git_annotated_commit_lookup(theirs.out(), repo, &fetchOid);
    if (rc != 0) return lastError(QStringLiteral("Lookup remote commit"), rc);

    git_merge_analysis_t analysis;
    git_merge_preference_t pref;
    const git_annotated_commit* heads[] = { theirs.get() };
    rc = git_merge_analysis(&analysis, &pref, repo, heads, 1);
    if (rc != 0) return lastError(QStringLiteral("Analyse merge"), rc);

    if (analysis & GIT_MERGE_ANALYSIS_UP_TO_DATE) {
        return GitResult::success();
    }
    if (!(analysis & GIT_MERGE_ANALYSIS_FASTFORWARD)) {
        return GitResult::failure(
            QStringLiteral("Cannot fast-forward: branches have diverged. "
                           "Resolve manually with git CLI."));
    }

    QByteArray localRef = QStringLiteral("refs/heads/%1").arg(branchShort).toUtf8();
    ReferenceHandle ref;
    rc = git_reference_lookup(ref.out(), repo, localRef.constData());
    if (rc != 0) return lastError(QStringLiteral("Lookup local ref"), rc);

    ReferenceHandle newRef;
    rc = git_reference_set_target(newRef.out(), ref.get(), &fetchOid, "pull: Fast-forward");
    if (rc != 0) return lastError(QStringLiteral("Update local ref"), rc);

    git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
    copts.checkout_strategy = GIT_CHECKOUT_SAFE;
    rc = git_checkout_head(repo, &copts);
    if (rc != 0) return lastError(QStringLiteral("Checkout HEAD"), rc);

    return GitResult::success();
}

// Build a libgit2 strarray from a list of UTF-8 byte strings. The byte
// arrays are kept alive by the caller via the returned vector.
struct StrArrayBuf {
    std::vector<QByteArray> bytes;
    std::vector<char*>      ptrs;
    git_strarray            arr{};

    void rebuildFromBytes() {
        ptrs.clear();
        ptrs.reserve(bytes.size());
        for (auto& b : bytes) ptrs.push_back(b.data());
        arr.strings = ptrs.data();
        arr.count   = ptrs.size();
    }
};

StrArrayBuf makeStrArray(const QStringList& paths)
{
    StrArrayBuf out;
    out.bytes.reserve(paths.size());
    for (const auto& p : paths) out.bytes.push_back(p.toUtf8());
    out.rebuildFromBytes();
    return out;
}

StrArrayBuf makeStrArrayAll()
{
    StrArrayBuf out;
    out.bytes.push_back(QByteArrayLiteral("*"));
    out.rebuildFromBytes();
    return out;
}

} // namespace

// ----- Class impl ----------------------------------------------------------

GitHandler::GitHandler()
{
    if (g_initCount.fetch_add(1) == 0) {
        git_libgit2_init();
    }
}

GitHandler::~GitHandler()
{
    if (g_initCount.fetch_sub(1) == 1) {
        git_libgit2_shutdown();
    }
}

bool GitHandler::isRepository(const QString& localPath) const
{
    if (localPath.isEmpty() || !QFileInfo::exists(localPath)) return false;
    const QByteArray p = localPath.toUtf8();
    git_repository* raw = nullptr;
    const int rc = git_repository_open(&raw, p.constData());
    if (rc != 0) return false;
    git_repository_free(raw);
    return true;
}

GitResult GitHandler::init(const QString& localPath, const QString& initialBranch)
{
    QDir dir(localPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return GitResult::failure(
            QStringLiteral("Cannot create directory: %1").arg(localPath));
    }

    git_repository_init_options opts = GIT_REPOSITORY_INIT_OPTIONS_INIT;
    opts.flags = GIT_REPOSITORY_INIT_MKPATH | GIT_REPOSITORY_INIT_NO_REINIT;

    const QByteArray branchBytes = initialBranch.isEmpty()
        ? QByteArrayLiteral("master")
        : initialBranch.toUtf8();
    opts.initial_head = branchBytes.constData();

    const QByteArray p = localPath.toUtf8();
    RepoHandle repo;
    const int rc = git_repository_init_ext(repo.out(), p.constData(), &opts);
    if (rc != 0) return lastError(QStringLiteral("Initialize repository"), rc);
    return GitResult::success();
}

GitResult GitHandler::clone(const QString& url, const QString& localPath,
                            const QString& token, const ProgressFn& progress)
{
    if (QFileInfo::exists(localPath) && !QDir(localPath).isEmpty()) {
        return GitResult::failure(
            QStringLiteral("Target directory '%1' is not empty.").arg(localPath));
    }

    CallbackCtx ctx;
    ctx.token    = token.toUtf8();
    ctx.progress = progress;

    git_clone_options opts = GIT_CLONE_OPTIONS_INIT;
    opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    opts.fetch_opts.callbacks.credentials       = &credCb;
    opts.fetch_opts.callbacks.transfer_progress = &transferCb;
    opts.fetch_opts.callbacks.payload           = &ctx;

    RepoHandle repo;
    const QByteArray u = url.toUtf8();
    const QByteArray p = localPath.toUtf8();

    const int rc = git_clone(repo.out(), u.constData(), p.constData(), &opts);
    if (rc != 0) return lastError(QStringLiteral("Clone"), rc);
    return GitResult::success();
}

GitResult GitHandler::open(const QString& localPath)
{
    RepoHandle r;
    return openRepo(localPath, r);
}

QString GitHandler::currentBranch(const QString& localPath, GitResult* outErr)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) {
        if (outErr) *outErr = rc;
        return {};
    }
    ReferenceHandle head;
    int rc = git_repository_head(head.out(), repo.get());
    if (rc == GIT_EUNBORNBRANCH) {
        // .git/HEAD points at refs/heads/<initialBranch> but the branch
        // doesn't exist yet (no commits). Read the symbolic target so we
        // can show "master" (or whatever was configured) instead of a
        // generic placeholder.
        ReferenceHandle headRef;
        if (git_reference_lookup(headRef.out(), repo.get(), "HEAD") == 0 &&
            git_reference_type(headRef.get()) == GIT_REFERENCE_SYMBOLIC)
        {
            const char* target = git_reference_symbolic_target(headRef.get());
            constexpr const char* kPrefix = "refs/heads/";
            constexpr int         kPrefixLen = 11;
            if (target && QByteArray(target).startsWith(kPrefix)) {
                return QString::fromUtf8(target + kPrefixLen);
            }
        }
        return QStringLiteral("(unborn)");
    }
    if (rc == GIT_ENOTFOUND) {
        return QStringLiteral("(unborn)");
    }
    if (rc != 0) {
        if (outErr) *outErr = lastError(QStringLiteral("Repository HEAD"), rc);
        return {};
    }
    if (git_repository_head_detached(repo.get()) == 1) {
        return QStringLiteral("(detached)");
    }
    const char* shorthand = git_reference_shorthand(head.get());
    return QString::fromUtf8(shorthand ? shorthand : "");
}

QStringList GitHandler::localBranches(const QString& localPath, GitResult* outErr)
{
    QStringList out;
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) {
        if (outErr) *outErr = rc;
        return out;
    }
    BranchIterH it;
    int rc = git_branch_iterator_new(it.out(), repo.get(), GIT_BRANCH_LOCAL);
    if (rc != 0) {
        if (outErr) *outErr = lastError(QStringLiteral("Branch iterator"), rc);
        return out;
    }
    git_reference* refRaw = nullptr;
    git_branch_t   type;
    while (git_branch_next(&refRaw, &type, it.get()) == 0) {
        ReferenceHandle ref(refRaw);
        const char* name = nullptr;
        if (git_branch_name(&name, ref.get()) == 0 && name) {
            out << QString::fromUtf8(name);
        }
    }
    return out;
}

GitResult GitHandler::checkoutBranch(const QString& localPath, const QString& branch)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    QByteArray refName = QStringLiteral("refs/heads/%1").arg(branch).toUtf8();
    ObjectHandle target;
    int rc = git_revparse_single(target.out(), repo.get(), refName.constData());
    if (rc != 0) return lastError(QStringLiteral("Resolve %1").arg(branch), rc);

    git_checkout_options copts = GIT_CHECKOUT_OPTIONS_INIT;
    copts.checkout_strategy = GIT_CHECKOUT_SAFE;

    rc = git_checkout_tree(repo.get(), target.get(), &copts);
    if (rc != 0) return lastError(QStringLiteral("Checkout tree"), rc);

    rc = git_repository_set_head(repo.get(), refName.constData());
    if (rc != 0) return lastError(QStringLiteral("Set HEAD"), rc);

    return GitResult::success();
}

StatusSummary GitHandler::status(const QString& localPath, GitResult* outErr)
{
    StatusSummary s;
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) {
        if (outErr) *outErr = rc;
        return s;
    }

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show  = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED |
                 GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;

    StatusListH list;
    int rc = git_status_list_new(list.out(), repo.get(), &opts);
    if (rc != 0) {
        if (outErr) *outErr = lastError(QStringLiteral("Status list"), rc);
        return s;
    }
    const size_t count = git_status_list_entrycount(list.get());
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry* e = git_status_byindex(list.get(), i);
        const auto st = e->status;
        if (st & GIT_STATUS_CONFLICTED)            ++s.conflicted;
        else if (st & GIT_STATUS_WT_NEW)           ++s.untracked;
        else if (st & (GIT_STATUS_INDEX_NEW))      ++s.added;
        else if (st & (GIT_STATUS_WT_DELETED |
                       GIT_STATUS_INDEX_DELETED))  ++s.deleted;
        else if (st & (GIT_STATUS_WT_MODIFIED |
                       GIT_STATUS_INDEX_MODIFIED |
                       GIT_STATUS_WT_TYPECHANGE |
                       GIT_STATUS_INDEX_TYPECHANGE)) ++s.modified;
    }

    // Ahead/behind vs upstream.
    ReferenceHandle head;
    if (git_repository_head(head.out(), repo.get()) == 0 &&
        git_reference_is_branch(head.get()) == 1)
    {
        ReferenceHandle upstream;
        if (git_branch_upstream(upstream.out(), head.get()) == 0) {
            const git_oid* localOid    = git_reference_target(head.get());
            const git_oid* upstreamOid = git_reference_target(upstream.get());
            size_t ahead = 0, behind = 0;
            if (localOid && upstreamOid &&
                git_graph_ahead_behind(&ahead, &behind, repo.get(),
                                       localOid, upstreamOid) == 0) {
                s.ahead  = static_cast<int>(ahead);
                s.behind = static_cast<int>(behind);
            }
        }
    }
    return s;
}

GitResult GitHandler::statusEntries(const QString& localPath,
                                    std::vector<StatusEntry>& out)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show  = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED
               | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
               | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX
               | GIT_STATUS_OPT_RENAMES_INDEX_TO_WORKDIR;

    StatusListH list;
    int rc = git_status_list_new(list.out(), repo.get(), &opts);
    if (rc != 0) return lastError(QStringLiteral("Status list"), rc);

    const size_t count = git_status_list_entrycount(list.get());
    out.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry* e = git_status_byindex(list.get(), i);
        StatusEntry se;

        // Choose a path: head_to_index has new_file (post-rename),
        // index_to_workdir has both old and new for renames in the
        // working tree. Fall back through whichever is populated.
        const git_diff_delta* hi = e->head_to_index;
        const git_diff_delta* iw = e->index_to_workdir;

        if (hi && hi->new_file.path) {
            se.path = QString::fromUtf8(hi->new_file.path);
            if (hi->old_file.path && hi->status == GIT_DELTA_RENAMED) {
                se.oldPath = QString::fromUtf8(hi->old_file.path);
            }
        } else if (iw && iw->new_file.path) {
            se.path = QString::fromUtf8(iw->new_file.path);
            if (iw->old_file.path && iw->status == GIT_DELTA_RENAMED) {
                se.oldPath = QString::fromUtf8(iw->old_file.path);
            }
        }
        if (se.path.isEmpty()) continue;

        const auto st = e->status;
        // Index column (HEAD vs index)
        if      (st & GIT_STATUS_INDEX_NEW)        se.indexFlag = 'A';
        else if (st & GIT_STATUS_INDEX_MODIFIED)   se.indexFlag = 'M';
        else if (st & GIT_STATUS_INDEX_DELETED)    se.indexFlag = 'D';
        else if (st & GIT_STATUS_INDEX_RENAMED)    se.indexFlag = 'R';
        else if (st & GIT_STATUS_INDEX_TYPECHANGE) se.indexFlag = 'T';
        // Worktree column (index vs workdir)
        if      (st & GIT_STATUS_WT_NEW)           se.worktreeFlag = '?';
        else if (st & GIT_STATUS_WT_MODIFIED)      se.worktreeFlag = 'M';
        else if (st & GIT_STATUS_WT_DELETED)       se.worktreeFlag = 'D';
        else if (st & GIT_STATUS_WT_RENAMED)       se.worktreeFlag = 'R';
        else if (st & GIT_STATUS_WT_TYPECHANGE)    se.worktreeFlag = 'T';
        if (st & GIT_STATUS_IGNORED)               se.worktreeFlag = '!';

        se.isStaged      = se.indexFlag != ' ';
        se.isUntracked   = (st & GIT_STATUS_WT_NEW) != 0 && se.indexFlag == ' ';
        se.isUnstaged    = se.worktreeFlag != ' ' && !se.isUntracked;
        se.isConflicted  = (st & GIT_STATUS_CONFLICTED) != 0;

        out.push_back(std::move(se));
    }
    return GitResult::success();
}

GitResult GitHandler::stageAll(const QString& localPath)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    IndexHandle idx;
    int rc = git_repository_index(idx.out(), repo.get());
    if (rc != 0) return lastError(QStringLiteral("Open index"), rc);

    auto pathspec = makeStrArrayAll();

    // Stage new + modified files (mirrors `git add .`).
    rc = git_index_add_all(idx.get(), &pathspec.arr, GIT_INDEX_ADD_DEFAULT,
                           nullptr, nullptr);
    if (rc != 0) return lastError(QStringLiteral("Stage all (add)"), rc);

    // Pick up deletions (`git add .` removes deleted files from index).
    rc = git_index_update_all(idx.get(), &pathspec.arr, nullptr, nullptr);
    if (rc != 0) return lastError(QStringLiteral("Stage all (update)"), rc);

    rc = git_index_write(idx.get());
    if (rc != 0) return lastError(QStringLiteral("Write index"), rc);
    return GitResult::success();
}

GitResult GitHandler::stagePaths(const QString& localPath, const QStringList& paths)
{
    if (paths.isEmpty()) return GitResult::success();

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    IndexHandle idx;
    int rc = git_repository_index(idx.out(), repo.get());
    if (rc != 0) return lastError(QStringLiteral("Open index"), rc);

    auto pathspec = makeStrArray(paths);

    rc = git_index_add_all(idx.get(), &pathspec.arr, GIT_INDEX_ADD_DEFAULT,
                           nullptr, nullptr);
    if (rc != 0) return lastError(QStringLiteral("Stage paths (add)"), rc);

    rc = git_index_update_all(idx.get(), &pathspec.arr, nullptr, nullptr);
    if (rc != 0) return lastError(QStringLiteral("Stage paths (update)"), rc);

    rc = git_index_write(idx.get());
    if (rc != 0) return lastError(QStringLiteral("Write index"), rc);
    return GitResult::success();
}

GitResult GitHandler::unstagePaths(const QString& localPath, const QStringList& paths)
{
    if (paths.isEmpty()) return GitResult::success();

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Try to resolve HEAD's commit. If HEAD is unborn, "unstage" means
    // "remove from index" since there's no committed version to revert to.
    ReferenceHandle head;
    int rc = git_repository_head(head.out(), repo.get());

    auto pathspec = makeStrArray(paths);

    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND) {
        IndexHandle idx;
        int irc = git_repository_index(idx.out(), repo.get());
        if (irc != 0) return lastError(QStringLiteral("Open index"), irc);
        for (const auto& p : paths) {
            const QByteArray pb = p.toUtf8();
            // Ignore not-in-index errors; user unstaging an already-clean file is fine.
            git_index_remove_bypath(idx.get(), pb.constData());
        }
        irc = git_index_write(idx.get());
        if (irc != 0) return lastError(QStringLiteral("Write index"), irc);
        return GitResult::success();
    }
    if (rc != 0) return lastError(QStringLiteral("Resolve HEAD"), rc);

    ObjectHandle headCommit;
    rc = git_reference_peel(headCommit.out(), head.get(), GIT_OBJECT_COMMIT);
    if (rc != 0) return lastError(QStringLiteral("Peel HEAD"), rc);

    rc = git_reset_default(repo.get(), headCommit.get(), &pathspec.arr);
    if (rc != 0) return lastError(QStringLiteral("Unstage"), rc);
    return GitResult::success();
}

GitResult GitHandler::commit(const QString& localPath,
                             const QString& message,
                             const QString& authorName,
                             const QString& authorEmail,
                             QString* outSha)
{
    if (message.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Commit message is empty."));
    }
    if (authorName.isEmpty() || authorEmail.isEmpty()) {
        return GitResult::failure(
            QStringLiteral("Author name and email must be set before committing."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Build tree from current index.
    IndexHandle idx;
    int rc = git_repository_index(idx.out(), repo.get());
    if (rc != 0) return lastError(QStringLiteral("Open index"), rc);

    git_oid treeOid;
    rc = git_index_write_tree(&treeOid, idx.get());
    if (rc != 0) return lastError(QStringLiteral("Write tree"), rc);

    TreeHandle tree;
    rc = git_tree_lookup(tree.out(), repo.get(), &treeOid);
    if (rc != 0) return lastError(QStringLiteral("Lookup tree"), rc);

    // Signature.
    SignatureHandle sig;
    {
        const QByteArray name  = authorName.toUtf8();
        const QByteArray email = authorEmail.toUtf8();
        rc = git_signature_now(sig.out(), name.constData(), email.constData());
        if (rc != 0) return lastError(QStringLiteral("Create signature"), rc);
    }

    // Resolve parent (none on first commit).
    CommitHandle parent;
    bool hasParent = false;
    {
        ReferenceHandle head;
        int hrc = git_repository_head(head.out(), repo.get());
        if (hrc == 0) {
            ObjectHandle headObj;
            if (git_reference_peel(headObj.out(), head.get(), GIT_OBJECT_COMMIT) == 0) {
                // Move ownership: peel returns a new git_object*; we want
                // a git_commit*. lookup by oid is simplest.
                const git_oid* poid = git_object_id(headObj.get());
                if (git_commit_lookup(parent.out(), repo.get(), poid) == 0) {
                    hasParent = true;
                }
            }
        } else if (hrc != GIT_EUNBORNBRANCH && hrc != GIT_ENOTFOUND) {
            return lastError(QStringLiteral("Resolve HEAD"), hrc);
        }
    }

    git_oid commitOid;
    const QByteArray msg = message.toUtf8();

    const git_commit* parents[1];
    size_t parentCount = 0;
    if (hasParent) {
        parents[0] = parent.get();
        parentCount = 1;
    }

    rc = git_commit_create(&commitOid, repo.get(), "HEAD",
                           sig.get(), sig.get(),
                           nullptr /* utf-8 default */,
                           msg.constData(),
                           tree.get(),
                           parentCount, parentCount ? parents : nullptr);
    if (rc != 0) return lastError(QStringLiteral("Create commit"), rc);

    if (outSha) {
        char buf[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr(buf, sizeof(buf), &commitOid);
        *outSha = QString::fromUtf8(buf);
    }
    return GitResult::success();
}

GitResult GitHandler::log(const QString& localPath, int maxCount,
                          std::vector<CommitInfo>& out)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    RevwalkHandle walk;
    int rc = git_revwalk_new(walk.out(), repo.get());
    if (rc != 0) return lastError(QStringLiteral("Create revwalk"), rc);

    git_revwalk_sorting(walk.get(), GIT_SORT_TIME | GIT_SORT_TOPOLOGICAL);

    rc = git_revwalk_push_head(walk.get());
    if (rc == GIT_ENOTFOUND || rc == GIT_EUNBORNBRANCH) {
        // No commits yet; empty log is fine.
        return GitResult::success();
    }
    if (rc != 0) return lastError(QStringLiteral("Push HEAD onto revwalk"), rc);

    git_oid oid;
    int count = 0;
    while (git_revwalk_next(&oid, walk.get()) == 0) {
        if (maxCount > 0 && count >= maxCount) break;

        CommitHandle commit;
        if (git_commit_lookup(commit.out(), repo.get(), &oid) != 0) continue;

        CommitInfo info;
        char buf[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr(buf, sizeof(buf), &oid);
        info.id      = QString::fromUtf8(buf);
        info.shortId = info.id.left(7);

        const char* summary = git_commit_summary(commit.get());
        const char* message = git_commit_message(commit.get());
        info.summary = QString::fromUtf8(summary ? summary : "");
        info.message = QString::fromUtf8(message ? message : "");

        if (const git_signature* author = git_commit_author(commit.get())) {
            info.authorName  = QString::fromUtf8(author->name  ? author->name  : "");
            info.authorEmail = QString::fromUtf8(author->email ? author->email : "");
            const qint64 secs = static_cast<qint64>(author->when.time);
            info.when = QDateTime::fromSecsSinceEpoch(secs).toLocalTime();
        }

        const unsigned int n = git_commit_parentcount(commit.get());
        for (unsigned int i = 0; i < n; ++i) {
            const git_oid* poid = git_commit_parent_id(commit.get(), i);
            if (!poid) continue;
            char pbuf[GIT_OID_SHA1_HEXSIZE + 1] = {};
            git_oid_tostr(pbuf, sizeof(pbuf), poid);
            info.parents << QString::fromUtf8(pbuf);
        }

        out.push_back(std::move(info));
        ++count;
    }
    return GitResult::success();
}

GitResult GitHandler::listRemotes(const QString& localPath,
                                  std::vector<RemoteInfo>& out)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    StrArrayScope names;
    int rc = git_remote_list(&names.a, repo.get());
    if (rc != 0) return lastError(QStringLiteral("List remotes"), rc);

    out.reserve(names.a.count);
    for (size_t i = 0; i < names.a.count; ++i) {
        RemoteInfo info;
        info.name = QString::fromUtf8(names.a.strings[i]);

        RemoteHandle remote;
        if (git_remote_lookup(remote.out(), repo.get(), names.a.strings[i]) == 0) {
            const char* url     = git_remote_url(remote.get());
            const char* pushUrl = git_remote_pushurl(remote.get());
            info.url     = QString::fromUtf8(url     ? url     : "");
            if (pushUrl) info.pushUrl = QString::fromUtf8(pushUrl);
        }
        out.push_back(std::move(info));
    }
    return GitResult::success();
}

GitResult GitHandler::addRemote(const QString& localPath,
                                const QString& name,
                                const QString& url)
{
    if (name.isEmpty() || url.isEmpty()) {
        return GitResult::failure(QStringLiteral("Remote name and URL are required."));
    }
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    const QByteArray nb = name.toUtf8();
    const QByteArray ub = url.toUtf8();

    RemoteHandle remote;
    int rc = git_remote_create(remote.out(), repo.get(), nb.constData(), ub.constData());
    if (rc != 0) return lastError(QStringLiteral("Add remote"), rc);
    return GitResult::success();
}

GitResult GitHandler::removeRemote(const QString& localPath, const QString& name)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    const QByteArray nb = name.toUtf8();
    int rc = git_remote_delete(repo.get(), nb.constData());
    if (rc != 0) return lastError(QStringLiteral("Remove remote"), rc);
    return GitResult::success();
}

GitResult GitHandler::setUpstream(const QString& localPath,
                                  const QString& branch,
                                  const QString& remoteName)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    ConfigHandle cfg;
    int rc = git_repository_config(cfg.out(), repo.get());
    if (rc != 0) return lastError(QStringLiteral("Open config"), rc);

    const QByteArray remoteKey = QStringLiteral("branch.%1.remote").arg(branch).toUtf8();
    const QByteArray mergeKey  = QStringLiteral("branch.%1.merge").arg(branch).toUtf8();
    const QByteArray mergeVal  = QStringLiteral("refs/heads/%1").arg(branch).toUtf8();
    const QByteArray remoteVal = remoteName.toUtf8();

    rc = git_config_set_string(cfg.get(), remoteKey.constData(), remoteVal.constData());
    if (rc != 0) return lastError(QStringLiteral("Set branch remote"), rc);

    rc = git_config_set_string(cfg.get(), mergeKey.constData(), mergeVal.constData());
    if (rc != 0) return lastError(QStringLiteral("Set branch merge"), rc);
    return GitResult::success();
}

GitResult GitHandler::pull(const QString& localPath, const QString& token,
                           const ProgressFn& progress)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    GitResult err;
    const QString branch = currentBranch(localPath, &err);
    if (!err.ok) return err;
    if (branch.startsWith(QLatin1Char('('))) {
        return GitResult::failure(
            QStringLiteral("Cannot pull while in %1 state.").arg(branch));
    }

    RemoteHandle remote;
    int rc = git_remote_lookup(remote.out(), repo.get(), "origin");
    if (rc != 0) return lastError(QStringLiteral("Lookup remote 'origin'"), rc);

    CallbackCtx ctx;
    ctx.token    = token.toUtf8();
    ctx.progress = progress;

    git_fetch_options fopts = GIT_FETCH_OPTIONS_INIT;
    fopts.callbacks.credentials       = &credCb;
    fopts.callbacks.transfer_progress = &transferCb;
    fopts.callbacks.payload           = &ctx;

    rc = git_remote_fetch(remote.get(), nullptr, &fopts, "pull");
    if (rc != 0) return lastError(QStringLiteral("Fetch"), rc);

    return fastForwardToOriginHead(repo.get(), branch);
}

GitResult GitHandler::push(const QString& localPath, const QString& token,
                           const ProgressFn& progress)
{
    GitResult err;
    const QString branch = currentBranch(localPath, &err);
    if (!err.ok) return err;
    if (branch.startsWith(QLatin1Char('('))) {
        return GitResult::failure(
            QStringLiteral("Cannot push while in %1 state.").arg(branch));
    }
    return pushBranch(localPath, QStringLiteral("origin"), branch,
                      /*setUpstreamAfter*/ false, token, progress);
}

GitResult GitHandler::pushBranch(const QString& localPath,
                                 const QString& remoteName,
                                 const QString& branch,
                                 bool setUpstreamAfter,
                                 const QString& token,
                                 const ProgressFn& progress)
{
    if (branch.isEmpty() || branch.startsWith(QLatin1Char('('))) {
        return GitResult::failure(
            QStringLiteral("Cannot push: no branch checked out."));
    }
    if (remoteName.isEmpty()) {
        return GitResult::failure(
            QStringLiteral("Cannot push: remote name is empty."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    RemoteHandle remote;
    const QByteArray nb = remoteName.toUtf8();
    int rc = git_remote_lookup(remote.out(), repo.get(), nb.constData());
    if (rc != 0) {
        return lastError(QStringLiteral("Lookup remote '%1'").arg(remoteName), rc);
    }

    CallbackCtx ctx;
    ctx.token    = token.toUtf8();
    ctx.progress = progress;

    git_push_options popts = GIT_PUSH_OPTIONS_INIT;
    popts.callbacks.credentials            = &credCb;
    popts.callbacks.push_transfer_progress = &pushTransferCb;
    popts.callbacks.payload                = &ctx;

    QByteArray refspec = QStringLiteral("refs/heads/%1:refs/heads/%1").arg(branch).toUtf8();
    char* refspecArr[]  = { refspec.data() };
    git_strarray refs;
    refs.strings = refspecArr;
    refs.count   = 1;

    rc = git_remote_push(remote.get(), &refs, &popts);
    if (rc != 0) return lastError(QStringLiteral("Push"), rc);

    if (setUpstreamAfter) {
        if (auto upRes = setUpstream(localPath, branch, remoteName); !upRes.ok) {
            // Push succeeded; surface the upstream-config error but don't
            // make the whole operation look failed.
            return GitResult::failure(
                QStringLiteral("Pushed, but failed to set upstream: %1")
                    .arg(upRes.error), upRes.code);
        }
    }
    return GitResult::success();
}

} // namespace ghm::git
