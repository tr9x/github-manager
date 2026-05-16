#include "git/GitHandler.h"

#include <git2.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <atomic>
#include <memory>
#include <vector>

#include "git/CommitSigner.h"  // signed-commit subprocess wrapper
#include "ui/HostKeyApprover.h"  // certificate_check callback routes
#include "ui/TlsCertApprover.h"  // TLS cert approval (same pattern)
                                  // through this for SSH host approval.

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
using DiffHandle      = GitHandle<git_diff,          git_diff_free>;
using PatchHandle     = GitHandle<git_patch,         git_patch_free>;
using BlobHandle      = GitHandle<git_blob,          git_blob_free>;
using ConflictIterH   = GitHandle<git_index_conflict_iterator,
                                  git_index_conflict_iterator_free>;

// git_strarray needs its own free and is ALSO a value type, so we use a
// scoped helper rather than the template.
struct StrArrayScope {
    git_strarray a{};
    ~StrArrayScope() { git_strarray_dispose(&a); }
};

// ----- Credential & progress callbacks -------------------------------------

struct CallbackCtx {
    QByteArray  token;          // ASCII PAT, kept alive for callback duration
                                // (empty for SSH-only operations)
    ProgressFn  progress;

    // Optional explicit SSH key — when set, credCb uses these instead
    // of ssh-agent. We keep the public-key path as a derived value
    // (private path + ".pub") so the caller only fills sshKeyPath +
    // sshPassphrase. Both byte arrays are kept alive for the whole
    // op so libssh2 can read them lazily.
    //
    // Threading: the caller (MainWindow → controller) populates this
    // on the GUI thread BEFORE the worker is invoked. credCb on the
    // worker thread only reads, never writes back. No locks needed.
    QByteArray  sshKeyPath;
    QByteArray  sshPubKeyPath;
    QByteArray  sshPassphrase;

    // SSH-specific diagnostics. Set by credCb when an SSH credential
    // lookup fails, so the caller can surface a specific error
    // ("ssh-agent has no keys loaded") rather than the libgit2
    // generic GIT_EAUTH. We can't tell from the libgit2 return code
    // alone whether the agent had zero keys vs the wrong keys vs the
    // agent socket was unreachable — but they all result in
    // git_credential_ssh_key_from_agent returning non-zero, so we
    // lump them together as "agent didn't satisfy the auth request".
    bool        sshAgentFailed{false};

    // Did we attempt SSH at all? Used by the caller to decide
    // whether sshAgentFailed is even relevant.
    bool        sshAttempted{false};

    // True when the caller wants us to use sshKeyPath instead of
    // the agent. Separate from "non-empty path" so an empty-passphrase
    // unencrypted key works (passphrase empty != "no explicit key").
    bool        useExplicitKey{false};
};

extern "C" int credCb(git_credential** out, const char* /*url*/,
                       const char* username_from_url, unsigned int allowed_types,
                       void* payload)
{
    auto* ctx = static_cast<CallbackCtx*>(payload);
    if (!ctx) return GIT_EUSER;

    // SSH key auth — libgit2 reports GIT_CREDENTIAL_SSH_KEY when the
    // URL scheme is ssh:// or git@host:. We delegate to ssh-agent
    // via git_credential_ssh_key_from_agent(), which on Linux talks
    // to the SSH_AUTH_SOCK socket — same agent the user's terminal
    // sees. No key path or passphrase plumbing needed (yet).
    //
    // The agent must have at least one matching key loaded; if it
    // doesn't, libgit2 will call this callback again with the same
    // allowed_types and we'd loop. We return GIT_PASSTHROUGH (which
    // becomes GIT_EAUTH) after the second attempt to break the loop,
    // surfacing a meaningful error to the caller.
    if (allowed_types & GIT_CREDENTIAL_SSH_KEY) {
        const char* user = username_from_url ? username_from_url : "git";
        ctx->sshAttempted = true;

        // Two paths: explicit key file (with optional passphrase) or
        // fall back to ssh-agent. We prefer explicit when the caller
        // supplied one — it's how the user told us "use this exact
        // key, possibly with this passphrase". ssh-agent has no way
        // to take a passphrase, so falling through wouldn't help.
        if (ctx->useExplicitKey && !ctx->sshKeyPath.isEmpty()) {
            // git_credential_ssh_key_new takes both the public-key
            // path (optional — pass NULL to let libssh2 derive it
            // from the private key by appending .pub) and the private
            // key path. We pass an empty pub path as NULL because
            // libssh2 can read the public key out of the private
            // file directly for OpenSSH-format keys.
            const char* pubPath = ctx->sshPubKeyPath.isEmpty()
                ? nullptr : ctx->sshPubKeyPath.constData();
            const int rc = git_credential_ssh_key_new(
                out, user, pubPath,
                ctx->sshKeyPath.constData(),
                ctx->sshPassphrase.isEmpty()
                    ? nullptr : ctx->sshPassphrase.constData());
            if (rc != 0) {
                // Same diagnostic flag as agent failure — the caller
                // gets a clearer message either way. We don't try to
                // distinguish "wrong passphrase" from "key not on
                // server" because libgit2 doesn't surface that.
                ctx->sshAgentFailed = true;
            }
            return rc;
        }

        // Fallback: ssh-agent. On Linux this talks to SSH_AUTH_SOCK.
        const int rc = git_credential_ssh_key_from_agent(out, user);
        if (rc != 0) {
            ctx->sshAgentFailed = true;
        }
        return rc;
    }

    if (allowed_types & GIT_CREDENTIAL_USERNAME) {
        // Some servers ask for username separately before credentials.
        // For HTTPS-with-PAT we use the standard pseudo-username.
        return git_credential_username_new(out,
            username_from_url ? username_from_url : "x-access-token");
    }

    if (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) {
        // HTTPS basic auth — needs the PAT in ctx->token. If we're
        // here without a token, the caller did something wrong
        // (kicked off HTTPS clone without sign-in); return EAUTH so
        // the user gets a clear "authentication failed" error.
        if (ctx->token.isEmpty()) return GIT_EUSER;
        // GitHub accepts the PAT as the password with any non-empty
        // username for HTTPS basic auth.
        return git_credential_userpass_plaintext_new(out,
            "x-access-token", ctx->token.constData());
    }

    // No supported credential type for this transport. libgit2 will
    // surface GIT_EAUTH up to the caller.
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

// libgit2 calls this for every TLS / SSH certificate during a remote
// operation. When valid=1, libgit2 already validated the cert (TLS
// against the system CA store, SSH against known_hosts). When valid=0,
// it's our job to decide whether to proceed.
//
// For SSH, we route the decision to the GUI thread via the
// HostKeyApprover singleton. The user sees a modal showing the key
// fingerprint and chooses whether to trust it. On accept, the approver
// also writes to ~/.ssh/known_hosts so subsequent connections to the
// same host skip the prompt.
//
// For TLS X.509, we route to TlsCertApprover (same pattern), which
// pops a dialog showing fingerprint + subject + issuer + validity
// dates. On accept-always, the fingerprint is persisted in QSettings
// and re-trusted on future connections to the same host.
//
// Return values:
//   0                  → trust this cert, proceed
//   GIT_PASSTHROUGH    → let libgit2 use its default behaviour
//   GIT_ECERTIFICATE   → reject, abort the op with an auth error
//   any other negative → generic error
extern "C" int certificateCheckCb(git_cert* cert, int valid,
                                   const char* host, void* /*payload*/)
{
    if (valid) {
        // libgit2 / libssh2 already approved this cert against the
        // system trust store or known_hosts. Nothing for us to do.
        return 0;
    }

    if (cert && cert->cert_type == GIT_CERT_HOSTKEY_LIBSSH2) {
        auto* hk = reinterpret_cast<git_cert_hostkey*>(cert);

        // Prefer SHA256 fingerprint (modern ssh-keygen default).
        // libgit2 exposes the bytes; we encode as base64 minus the
        // padding "=", matching `ssh-keygen -lf`'s output.
        QByteArray fp;
        QString keyType = QStringLiteral("ssh");
        if (hk->type & GIT_CERT_SSH_SHA256) {
            fp = QByteArray(reinterpret_cast<const char*>(hk->hash_sha256),
                            sizeof(hk->hash_sha256));
        } else if (hk->type & GIT_CERT_SSH_SHA1) {
            fp = QByteArray(reinterpret_cast<const char*>(hk->hash_sha1),
                            sizeof(hk->hash_sha1));
        }
        QString fpStr = QString::fromLatin1(fp.toBase64());
        // Strip trailing '=' padding to match ssh-keygen format.
        while (fpStr.endsWith(QLatin1Char('='))) fpStr.chop(1);

        // libgit2 also exposes the raw key bytes (hk->hostkey,
        // hk->hostkey_len) only on newer versions. When available,
        // we pass the base64 of those bytes to the approver so it
        // can write a proper known_hosts line. When not, the entry
        // can't be written and the prompt will reappear next time.
        QString rawKeyBase64;
#ifdef GIT_CERT_SSH_RAW
        if (hk->type & GIT_CERT_SSH_RAW &&
            hk->hostkey && hk->hostkey_len > 0) {
            rawKeyBase64 = QString::fromLatin1(
                QByteArray(hk->hostkey, static_cast<int>(hk->hostkey_len))
                    .toBase64());
            // hk->raw_type_name carries "ssh-ed25519" / "ssh-rsa" etc.
            if (hk->raw_type_name) {
                keyType = QString::fromUtf8(hk->raw_type_name);
            }
        }
#endif

        // Route to the GUI thread via the approver. If no approver
        // is registered (e.g. headless test mode) we reject — safer
        // than silently trusting unknown hosts.
        auto* approver = ghm::ui::HostKeyApprover::instance();
        if (!approver) return GIT_ECERTIFICATE;

        const QString hostStr = QString::fromUtf8(host ? host : "");
        bool approved = false;
        // BlockingQueuedConnection: worker thread blocks until the
        // GUI thread runs the slot. Return value comes back through
        // Q_RETURN_ARG. The slot itself pops a modal dialog and
        // (on accept) writes to ~/.ssh/known_hosts.
        QMetaObject::invokeMethod(approver, "requestApproval",
            Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(bool, approved),
            Q_ARG(QString, hostStr),
            Q_ARG(QString, fpStr),
            Q_ARG(QString, keyType),
            Q_ARG(QString, rawKeyBase64));

        return approved ? 0 : GIT_ECERTIFICATE;
    }

    // Non-SSH cert (TLS X.509). Route to TlsCertApprover so the user
    // can inspect fingerprint / subject / issuer and accept once or
    // permanently. Without an approver registered (headless test
    // mode) we reject — same safe-default policy as the SSH path.
    if (cert && cert->cert_type == GIT_CERT_X509) {
        auto* x509 = reinterpret_cast<git_cert_x509*>(cert);
        if (!x509->data || x509->len == 0) {
            // libgit2 gave us a cert struct but no bytes — can't
            // compute fingerprint, can't show details. Reject.
            return GIT_ECERTIFICATE;
        }

        const QByteArray der(static_cast<const char*>(x509->data),
                              static_cast<int>(x509->len));
        const QString hostStr = QString::fromUtf8(host ? host : "");

        // Quick path: compute fingerprint on the worker thread and
        // check the trusted list FIRST. If trusted, we never bother
        // the GUI thread — avoids spurious UI flashes for repeat
        // operations on an already-approved server.
        auto* approver = ghm::ui::TlsCertApprover::instance();
        if (!approver) return GIT_ECERTIFICATE;

        const QString sha256Hex = QString::fromLatin1(
            QCryptographicHash::hash(der, QCryptographicHash::Sha256)
                .toHex().toLower());
        if (approver->isFingerprintTrusted(hostStr, sha256Hex)) {
            return 0;
        }

        // Not trusted — pop the dialog via BlockingQueuedConnection.
        bool approved = false;
        QMetaObject::invokeMethod(approver, "requestApproval",
            Qt::BlockingQueuedConnection,
            Q_RETURN_ARG(bool, approved),
            Q_ARG(QString, hostStr),
            Q_ARG(QByteArray, der));
        return approved ? 0 : GIT_ECERTIFICATE;
    }

    // Unknown cert type — let it fail with the original error.
    return GIT_ECERTIFICATE;
}

// ----- Helpers -------------------------------------------------------------

// If a failed network op had an SSH-agent credential attempt, surface
// a clearer error than libgit2's generic GIT_EAUTH. The caller passes
// the original GitResult through and we either return it unchanged
// (when SSH wasn't involved or the op succeeded) or replace it with
// a targeted message.
//
// We detect "agent has no usable keys" by the combination of:
//   * sshAttempted (we DID try GIT_CREDENTIAL_SSH_KEY at least once)
//   * sshAgentFailed (the agent lookup itself returned non-zero)
//
// Cases we can't distinguish (and don't try): "agent has the wrong
// key for this account" vs "agent socket unreachable" vs "no keys
// loaded". All three deserve roughly the same fix: load the right
// key into the agent. The message lists the diagnostic commands.
//
// We ALSO inspect the libgit2 error message for known unknown-host
// patterns. libssh2 puts strings like "Host key verification failed"
// or "unknown host" in the error when ~/.ssh/known_hosts doesn't
// have an entry for the target. We can't fix this in-process without
// a full host-key approval dialog (deferred to a future sprint),
// but the error message can at least tell the user what to do.
GitResult rewriteSshAgentError(GitResult original, const CallbackCtx& ctx)
{
    if (original.ok) return original;

    // Host-key check first, since it can apply even when sshAttempted
    // is false (libssh2 fails the connection before we get to creds).
    // Match common libssh2 phrasings — we don't try to be exhaustive,
    // and the original message is always kept in the output so the
    // user (or a future bug report) can see what libgit2 actually said.
    const QString lowered = original.error.toLower();
    const bool looksLikeHostKey =
        lowered.contains(QLatin1String("host key verification"))   ||
        lowered.contains(QLatin1String("unknown host"))            ||
        lowered.contains(QLatin1String("the remote host's key"))   ||
        lowered.contains(QLatin1String("server's host key"));
    if (looksLikeHostKey) {
        return GitResult::failure(
            QStringLiteral(
                "SSH host key verification failed — this host isn't in "
                "your ~/.ssh/known_hosts file yet.\n\n"
                "To register it, run this in a terminal once:\n"
                "  ssh -T git@github.com\n"
                "(replace 'github.com' with your remote's host if different).\n"
                "Type 'yes' when prompted. Then retry the clone.\n\n"
                "Original libgit2 error: %1").arg(original.error),
            original.code);
    }

    if (!ctx.sshAttempted || !ctx.sshAgentFailed) return original;
    return GitResult::failure(
        QStringLiteral(
            "SSH authentication failed. ssh-agent didn't provide a usable "
            "key — either it has no keys loaded, or none of the loaded "
            "keys are accepted by the remote.\n\n"
            "Common fix:\n"
            "  1. In a terminal, run:  ssh-add -l\n"
            "  2. If empty, run:  ssh-add ~/.ssh/id_ed25519  "
            "(or your key path)\n"
            "  3. Verify the key is on your GitHub account:  "
            "ssh -T git@github.com\n\n"
            "Original libgit2 error: %1").arg(original.error),
        original.code);
}

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

// Pulls a single git_diff_delta out of a populated git_diff and turns
// it into our FileDiff struct. Shared by fileDiff() (single-file
// pathspec, picks first delta) and commitDiff() (no pathspec, called
// once per delta in the loop).
//
// Returns false only on patch-creation failure for non-binary entries;
// the caller can decide whether to skip or treat that as fatal. Empty
// hunks for binary files are normal and return true.
bool extractDelta(git_diff* diff, size_t index, FileDiff& out)
{
    const git_diff_delta* delta = git_diff_get_delta(diff, index);
    if (!delta) return false;

    switch (delta->status) {
        case GIT_DELTA_ADDED:      out.status = 'A'; break;
        case GIT_DELTA_DELETED:    out.status = 'D'; break;
        case GIT_DELTA_MODIFIED:   out.status = 'M'; break;
        case GIT_DELTA_RENAMED:    out.status = 'R'; break;
        case GIT_DELTA_COPIED:     out.status = 'C'; break;
        case GIT_DELTA_TYPECHANGE: out.status = 'T'; break;
        case GIT_DELTA_UNTRACKED:  out.status = '?'; out.isUntracked = true; break;
        case GIT_DELTA_CONFLICTED: out.status = 'U'; break;
        default:                   out.status = '?'; break;
    }
    if (delta->old_file.path) out.oldPath = QString::fromUtf8(delta->old_file.path);
    if (delta->new_file.path) out.path    = QString::fromUtf8(delta->new_file.path);

    out.isBinary = (delta->flags & GIT_DIFF_FLAG_BINARY) != 0;
    if (out.isBinary) {
        // No hunks for binary files; libgit2 won't enumerate them.
        return true;
    }

    PatchHandle patch;
    if (git_patch_from_diff(patch.out(), diff, index) != 0) {
        return false;
    }

    const size_t nHunks = git_patch_num_hunks(patch.get());
    out.hunks.reserve(out.hunks.size() + nHunks);
    for (size_t h = 0; h < nHunks; ++h) {
        const git_diff_hunk* hRaw = nullptr;
        size_t nLinesInHunk = 0;
        if (git_patch_get_hunk(&hRaw, &nLinesInHunk, patch.get(), h) != 0) continue;

        DiffHunk hunk;
        hunk.oldStart = hRaw->old_start;
        hunk.oldLines = hRaw->old_lines;
        hunk.newStart = hRaw->new_start;
        hunk.newLines = hRaw->new_lines;
        hunk.header = QString::fromUtf8(hRaw->header,
                                        static_cast<int>(hRaw->header_len)).trimmed();
        hunk.lines.reserve(nLinesInHunk);

        for (size_t l = 0; l < nLinesInHunk; ++l) {
            const git_diff_line* lineRaw = nullptr;
            if (git_patch_get_line_in_hunk(&lineRaw, patch.get(), h, l) != 0) continue;
            if (!lineRaw) continue;

            DiffLine line;
            line.origin    = lineRaw->origin;
            line.oldLineNo = lineRaw->old_lineno;
            line.newLineNo = lineRaw->new_lineno;

            int len = static_cast<int>(lineRaw->content_len);
            if (len > 0 && lineRaw->content[len - 1] == '\n') --len;
            if (len > 0 && lineRaw->content[len - 1] == '\r') --len;
            line.content = QString::fromUtf8(lineRaw->content, len);

            if      (line.origin == '+') ++out.additions;
            else if (line.origin == '-') ++out.deletions;

            hunk.lines.push_back(std::move(line));
        }
        out.hunks.push_back(std::move(hunk));
    }
    return true;
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
                            const QString& token, const ProgressFn& progress,
                            const SshCredentials& sshCreds)
{
    if (QFileInfo::exists(localPath) && !QDir(localPath).isEmpty()) {
        return GitResult::failure(
            QStringLiteral("Target directory '%1' is not empty.").arg(localPath));
    }

    CallbackCtx ctx;
    ctx.token    = token.toUtf8();
    ctx.progress = progress;

    // When explicit creds were supplied, copy them into the ctx as
    // QByteArrays so they outlive the callback invocations. libgit2
    // may call credCb multiple times before clone finishes (e.g.
    // for both fetch and post-clone refresh), and we don't want the
    // QString temporaries to dangle.
    if (sshCreds.isExplicit()) {
        ctx.useExplicitKey  = true;
        ctx.sshKeyPath      = sshCreds.keyPath.toUtf8();
        ctx.sshPubKeyPath   = sshCreds.publicKeyPath.toUtf8();
        ctx.sshPassphrase   = sshCreds.passphrase.toUtf8();
    }

    git_clone_options opts = GIT_CLONE_OPTIONS_INIT;
    opts.checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    opts.fetch_opts.callbacks.credentials       = &credCb;
    opts.fetch_opts.callbacks.transfer_progress = &transferCb;
    opts.fetch_opts.callbacks.certificate_check = &certificateCheckCb;
    opts.fetch_opts.callbacks.payload           = &ctx;

    RepoHandle repo;
    const QByteArray u = url.toUtf8();
    const QByteArray p = localPath.toUtf8();

    const int rc = git_clone(repo.out(), u.constData(), p.constData(), &opts);
    if (rc != 0) {
        // Map libgit2's generic auth error into the more informative
        // ssh-agent message when applicable. Pass-through otherwise.
        return rewriteSshAgentError(
            lastError(QStringLiteral("Clone"), rc), ctx);
    }
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

GitResult GitHandler::listLocalBranches(const QString& localPath,
                                        std::vector<BranchInfo>& out)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Pre-resolve current branch so we can flag isCurrent in the loop.
    QString currentName;
    bool    headDetached = false;
    {
        ReferenceHandle head;
        const int hrc = git_repository_head(head.out(), repo.get());
        if (hrc == 0 && git_reference_is_branch(head.get()) == 1) {
            const char* sh = git_reference_shorthand(head.get());
            if (sh) currentName = QString::fromUtf8(sh);
        } else if (hrc == 0 && git_repository_head_detached(repo.get()) == 1) {
            headDetached = true;
        }
        // Unborn HEAD: currentName stays empty; branches still listed.
    }

    BranchIterH it;
    int rc = git_branch_iterator_new(it.out(), repo.get(), GIT_BRANCH_LOCAL);
    if (rc != 0) return lastError(QStringLiteral("Branch iterator"), rc);

    git_reference* refRaw = nullptr;
    git_branch_t   type;
    while (git_branch_next(&refRaw, &type, it.get()) == 0) {
        ReferenceHandle ref(refRaw);
        const char* nameC = nullptr;
        if (git_branch_name(&nameC, ref.get()) != 0 || !nameC) continue;

        BranchInfo info;
        info.name = QString::fromUtf8(nameC);
        info.isCurrent = !headDetached && (info.name == currentName);

        // Resolve upstream — most branches won't have one (e.g. local-only),
        // and that's fine. git_branch_upstream returns ENOTFOUND in that case.
        ReferenceHandle upstream;
        if (git_branch_upstream(upstream.out(), ref.get()) == 0) {
            info.hasUpstream = true;
            const char* upName = nullptr;
            if (git_branch_name(&upName, upstream.get()) == 0 && upName) {
                info.upstreamName = QString::fromUtf8(upName);
            }
            const git_oid* localOid    = git_reference_target(ref.get());
            const git_oid* upstreamOid = git_reference_target(upstream.get());
            size_t ahead = 0, behind = 0;
            if (localOid && upstreamOid &&
                git_graph_ahead_behind(&ahead, &behind, repo.get(),
                                       localOid, upstreamOid) == 0) {
                info.ahead  = static_cast<int>(ahead);
                info.behind = static_cast<int>(behind);
            }
        }
        out.push_back(std::move(info));
    }

    // Sort: current first, then alphabetically. Predictable ordering
    // matters for the picker — users learn where their branch lives.
    std::sort(out.begin(), out.end(),
              [](const BranchInfo& a, const BranchInfo& b) {
        if (a.isCurrent != b.isCurrent) return a.isCurrent;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    return GitResult::success();
}

GitResult GitHandler::createBranch(const QString& localPath,
                                   const QString& name,
                                   bool           checkoutAfter)
{
    if (name.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Branch name is empty."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Need a commit to branch from. Unborn HEAD (no commits yet) means
    // there's nothing to create a branch off of — surface a helpful
    // message instead of a cryptic libgit2 error code.
    ReferenceHandle head;
    int rc = git_repository_head(head.out(), repo.get());
    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND) {
        return GitResult::failure(
            QStringLiteral("Cannot create a branch yet — make at least one commit first."));
    }
    if (rc != 0) return lastError(QStringLiteral("Resolve HEAD"), rc);

    CommitHandle headCommit;
    {
        ObjectHandle peeled;
        if (git_reference_peel(peeled.out(), head.get(), GIT_OBJECT_COMMIT) != 0) {
            return lastError(QStringLiteral("Peel HEAD to commit"), -1);
        }
        const git_oid* oid = git_object_id(peeled.get());
        if (git_commit_lookup(headCommit.out(), repo.get(), oid) != 0) {
            return lastError(QStringLiteral("Lookup HEAD commit"), -1);
        }
    }

    const QByteArray nameBytes = name.toUtf8();
    ReferenceHandle newRef;
    rc = git_branch_create(newRef.out(), repo.get(),
                           nameBytes.constData(), headCommit.get(),
                           /*force*/ 0);
    if (rc != 0) return lastError(QStringLiteral("Create branch '%1'").arg(name), rc);

    if (checkoutAfter) {
        return checkoutBranch(localPath, name);
    }
    return GitResult::success();
}

GitResult GitHandler::deleteBranch(const QString& localPath,
                                   const QString& name,
                                   bool           force)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Refuse to delete the current branch — there's no good UX for
    // "where should HEAD go now". User must check out somewhere else
    // first. Mirrors `git branch -d <currentBranch>`.
    {
        ReferenceHandle head;
        if (git_repository_head(head.out(), repo.get()) == 0 &&
            git_reference_is_branch(head.get()) == 1) {
            const char* shorthand = git_reference_shorthand(head.get());
            if (shorthand && QString::fromUtf8(shorthand) == name) {
                return GitResult::failure(QStringLiteral(
                    "Cannot delete the currently-checked-out branch. "
                    "Switch to another branch first."));
            }
        }
    }

    ReferenceHandle ref;
    const QByteArray nameBytes = name.toUtf8();
    int rc = git_branch_lookup(ref.out(), repo.get(),
                               nameBytes.constData(), GIT_BRANCH_LOCAL);
    if (rc != 0) return lastError(QStringLiteral("Lookup branch '%1'").arg(name), rc);

    if (!force) {
        // Safety check: ensure the branch is fully reachable from HEAD.
        // This is what `git branch -d` does — refuses to drop unique
        // commits unless you opt in with -D.
        ReferenceHandle head;
        if (git_repository_head(head.out(), repo.get()) == 0) {
            const git_oid* branchOid = git_reference_target(ref.get());
            const git_oid* headOid   = git_reference_target(head.get());
            if (branchOid && headOid) {
                size_t ahead = 0, behind = 0;
                if (git_graph_ahead_behind(&ahead, &behind, repo.get(),
                                           branchOid, headOid) == 0) {
                    if (ahead > 0) {
                        return GitResult::failure(QStringLiteral(
                            "Branch '%1' has %2 commit(s) not merged into HEAD. "
                            "Use force-delete if you really want to drop them.")
                            .arg(name).arg(ahead));
                    }
                }
            }
        }
    }

    rc = git_branch_delete(ref.get());
    if (rc != 0) return lastError(QStringLiteral("Delete branch '%1'").arg(name), rc);
    return GitResult::success();
}

GitResult GitHandler::renameBranch(const QString& localPath,
                                   const QString& oldName,
                                   const QString& newName,
                                   bool           force)
{
    if (oldName.trimmed().isEmpty() || newName.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Branch name is empty."));
    }
    if (oldName == newName) {
        // No-op rather than going through libgit2 which would otherwise
        // succeed silently — surface this so the UI knows nothing changed.
        return GitResult::success();
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    ReferenceHandle oldRef;
    {
        const QByteArray nb = oldName.toUtf8();
        const int rc = git_branch_lookup(oldRef.out(), repo.get(),
                                         nb.constData(), GIT_BRANCH_LOCAL);
        if (rc != 0) {
            return lastError(QStringLiteral("Lookup branch '%1'").arg(oldName), rc);
        }
    }

    // git_branch_move doesn't validate the new name itself — libgit2's
    // git_reference_create called underneath does, but with a generic
    // "invalid reference name" error. The dialog already runs our
    // own validator before submitting, so by the time we get here
    // we trust the input. Force is the only knob we expose.
    ReferenceHandle newRef;
    const QByteArray nb = newName.toUtf8();
    const int rc = git_branch_move(newRef.out(), oldRef.get(),
                                   nb.constData(),
                                   force ? 1 : 0);
    if (rc != 0) {
        return lastError(QStringLiteral("Rename branch '%1' → '%2'")
                             .arg(oldName, newName), rc);
    }
    return GitResult::success();
}

GitResult GitHandler::fetch(const QString& localPath,
                            const QString& remoteName,
                            const QString& pat,
                            const ProgressFn& progress)
{
    if (remoteName.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Remote name is empty."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    RemoteHandle remote;
    const QByteArray nameBytes = remoteName.toUtf8();
    int rc = git_remote_lookup(remote.out(), repo.get(), nameBytes.constData());
    if (rc != 0) {
        return lastError(QStringLiteral("Lookup remote '%1'").arg(remoteName), rc);
    }

    // Same callback context used by pull/push — credCb pulls the PAT
    // out, transferCb forwards progress to the supplied callback.
    CallbackCtx ctx;
    ctx.token    = pat.toUtf8();
    ctx.progress = progress;

    git_fetch_options fopts = GIT_FETCH_OPTIONS_INIT;
    fopts.callbacks.credentials       = &credCb;
    fopts.callbacks.transfer_progress = &transferCb;
    fopts.callbacks.payload           = &ctx;

    // Empty refspec list (`nullptr`) tells libgit2 to use whatever's
    // configured in remote.<name>.fetch — same default as `git fetch`.
    rc = git_remote_fetch(remote.get(), nullptr, &fopts, "fetch");
    if (rc != 0) {
        return rewriteSshAgentError(
            lastError(QStringLiteral("Fetch '%1'").arg(remoteName), rc), ctx);
    }
    return GitResult::success();
}

GitResult GitHandler::undoLastCommit(const QString& localPath)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Refuse if mid-merge/rebase/cherry-pick — undoing the last
    // commit there would leave the repo in a state git itself
    // would refuse to interpret.
    if (git_repository_state(repo.get()) != GIT_REPOSITORY_STATE_NONE) {
        return GitResult::failure(QStringLiteral(
            "Cannot undo while a merge, rebase, or cherry-pick is in "
            "progress. Finish or abort it first."));
    }

    ReferenceHandle head;
    int rc = git_repository_head(head.out(), repo.get());
    if (rc != 0) {
        return lastError(QStringLiteral("Resolve HEAD"), rc);
    }

    // Walk to HEAD's commit, then to its first parent. If there's no
    // parent, HEAD is the root commit and we can't undo it (the
    // soft-reset has nowhere to point).
    ObjectHandle headObj;
    rc = git_reference_peel(headObj.out(), head.get(), GIT_OBJECT_COMMIT);
    if (rc != 0) return lastError(QStringLiteral("Peel HEAD to commit"), rc);

    CommitHandle headCommit;
    const git_oid* headOid = git_object_id(headObj.get());
    rc = git_commit_lookup(headCommit.out(), repo.get(), headOid);
    if (rc != 0) return lastError(QStringLiteral("Lookup HEAD commit"), rc);

    if (git_commit_parentcount(headCommit.get()) == 0) {
        return GitResult::failure(QStringLiteral(
            "HEAD is the initial commit — there's nothing to undo. "
            "If you want to start over, delete the .git directory."));
    }

    CommitHandle parent;
    rc = git_commit_parent(parent.out(), headCommit.get(), 0);
    if (rc != 0) return lastError(QStringLiteral("Read parent commit"), rc);

    // GIT_RESET_SOFT moves HEAD without touching index or working
    // tree — the changes from the undone commit reappear as staged.
    // That matches `git reset --soft HEAD~1` and is the user-friendly
    // default: nothing is lost, the user can edit the message and
    // re-commit.
    rc = git_reset(repo.get(),
                   reinterpret_cast<git_object*>(parent.get()),
                   GIT_RESET_SOFT, nullptr);
    if (rc != 0) return lastError(QStringLiteral("Soft-reset to HEAD~1"), rc);
    return GitResult::success();
}

// ----- Reflog --------------------------------------------------------------

// RAII for git_reflog — local to this section since reflog is the
// only consumer.
using ReflogHandle = GitHandle<git_reflog, git_reflog_free>;

GitResult GitHandler::readHeadReflog(const QString& localPath,
                                     int maxCount,
                                     std::vector<ReflogEntry>& out)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Reflog for HEAD specifically. libgit2 stores reflogs in
    // .git/logs/<refname>; for HEAD it's .git/logs/HEAD. Listing
    // returns entries in append order (oldest first), but the user
    // wants most-recent first to match `git reflog`, so we reverse
    // at the end.
    ReflogHandle reflog;
    int rc = git_reflog_read(reflog.out(), repo.get(), "HEAD");
    if (rc == GIT_ENOTFOUND) {
        // Brand-new repo with no operations yet — empty reflog.
        return GitResult::success();
    }
    if (rc != 0) return lastError(QStringLiteral("Read HEAD reflog"), rc);

    const size_t total = git_reflog_entrycount(reflog.get());
    out.reserve(std::min<size_t>(total, maxCount > 0 ? maxCount : total));

    // Walk from newest (index 0 in libgit2's API is actually OLDEST —
    // documented inversely from `git reflog` numbering). We iterate
    // from the back of the count so output is newest-first.
    for (size_t i = total; i-- > 0;) {
        if (maxCount > 0 && static_cast<int>(out.size()) >= maxCount) break;

        const git_reflog_entry* entry = git_reflog_entry_byindex(reflog.get(), i);
        if (!entry) continue;

        ReflogEntry e;
        char buf[GIT_OID_SHA1_HEXSIZE + 1] = {};
        if (const git_oid* old = git_reflog_entry_id_old(entry)) {
            git_oid_tostr(buf, sizeof(buf), old);
            // The first-ever entry has a zero OID for "old" — show it
            // as empty rather than the literal "0000...".
            if (!git_oid_is_zero(old)) {
                e.oldSha = QString::fromUtf8(buf);
            }
        }
        if (const git_oid* nu = git_reflog_entry_id_new(entry)) {
            git_oid_tostr(buf, sizeof(buf), nu);
            e.newSha = QString::fromUtf8(buf);
        }

        if (const char* msg = git_reflog_entry_message(entry)) {
            e.message = QString::fromUtf8(msg).trimmed();
        }

        if (const git_signature* sig = git_reflog_entry_committer(entry)) {
            e.committerName  = QString::fromUtf8(sig->name  ? sig->name  : "");
            e.committerEmail = QString::fromUtf8(sig->email ? sig->email : "");
            const qint64 secs = static_cast<qint64>(sig->when.time);
            e.when = QDateTime::fromSecsSinceEpoch(secs).toLocalTime();
        }

        out.push_back(std::move(e));
    }
    return GitResult::success();
}

GitResult GitHandler::softResetTo(const QString& localPath, const QString& sha)
{
    if (sha.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Target SHA is empty."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Don't reset while a merge/rebase/cherry-pick is in flight —
    // same guard as undoLastCommit. The user wouldn't get the
    // expected result and recovery becomes harder.
    if (git_repository_state(repo.get()) != GIT_REPOSITORY_STATE_NONE) {
        return GitResult::failure(QStringLiteral(
            "Cannot reset while a merge, rebase, or cherry-pick is in "
            "progress. Finish or abort it first."));
    }

    git_oid oid;
    const QByteArray shaBytes = sha.toLatin1();
    int rc = git_oid_fromstr(&oid, shaBytes.constData());
    if (rc != 0) {
        return GitResult::failure(QStringLiteral("Invalid SHA: %1").arg(sha));
    }

    ObjectHandle target;
    rc = git_object_lookup(target.out(), repo.get(), &oid, GIT_OBJECT_COMMIT);
    if (rc != 0) {
        // Most common cause: the commit was already garbage-collected
        // (e.g. ran `git gc` after the destructive op the user is
        // trying to undo). Surface this clearly rather than letting
        // libgit2's generic "object not found" through.
        return GitResult::failure(QStringLiteral(
            "Commit %1 is no longer in the object database. "
            "It may have been garbage-collected — `git fsck --lost-found` "
            "is the last-resort recovery option.").arg(sha.left(7)));
    }

    rc = git_reset(repo.get(), target.get(), GIT_RESET_SOFT, nullptr);
    if (rc != 0) return lastError(QStringLiteral("Soft-reset to %1").arg(sha.left(7)), rc);
    return GitResult::success();
}

// ----- Stash ---------------------------------------------------------------

GitResult GitHandler::stashSave(const QString& localPath,
                                const QString& message,
                                bool           includeUntracked,
                                bool           keepIndex,
                                const QString& authorName,
                                const QString& authorEmail)
{
    if (authorName.trimmed().isEmpty() || authorEmail.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral(
            "Stash needs a configured author identity. "
            "Set your name and email first."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    SignatureHandle sig;
    {
        const QByteArray name  = authorName.toUtf8();
        const QByteArray email = authorEmail.toUtf8();
        const int rc = git_signature_now(sig.out(),
                                         name.constData(), email.constData());
        if (rc != 0) return lastError(QStringLiteral("Create signature"), rc);
    }

    // Empty message ⇒ pass nullptr so libgit2 generates the default
    // "WIP on <branch>: <sha> <summary>" form. This matches what
    // `git stash` does when invoked without -m.
    QByteArray msgBytes = message.toUtf8();
    const char* msgPtr  = msgBytes.isEmpty() ? nullptr : msgBytes.constData();

    unsigned int flags = GIT_STASH_DEFAULT;
    if (includeUntracked) flags |= GIT_STASH_INCLUDE_UNTRACKED;
    if (keepIndex)        flags |= GIT_STASH_KEEP_INDEX;

    git_oid stashOid;
    const int rc = git_stash_save(&stashOid, repo.get(), sig.get(),
                                  msgPtr,
                                  static_cast<git_stash_flags>(flags));
    if (rc == GIT_ENOTFOUND) {
        // libgit2 returns ENOTFOUND when there's nothing to stash —
        // surface a clearer message than "object not found".
        return GitResult::failure(QStringLiteral("Nothing to stash."));
    }
    if (rc != 0) return lastError(QStringLiteral("Stash save"), rc);
    return GitResult::success();
}

namespace {

// Callback context for git_stash_foreach. We collect entries into an
// std::vector via the void* user-data slot. The callback signature is
// fixed by libgit2, so we have to round-trip through a struct.
struct StashListCtx {
    std::vector<StashEntry>* out;
    git_repository*          repo;
};

int stashListCb(size_t index, const char* message, const git_oid* stashId,
                void* payload)
{
    auto* ctx = static_cast<StashListCtx*>(payload);
    if (!ctx || !ctx->out || !stashId) return 0;

    StashEntry e;
    e.index   = static_cast<int>(index);
    e.message = QString::fromUtf8(message ? message : "");

    char shortBuf[8] = {};
    git_oid_tostr(shortBuf, sizeof(shortBuf), stashId);
    e.shortId = QString::fromLatin1(shortBuf);

    // Pull the committer time from the stash commit so we can show
    // "5 minutes ago" in the UI rather than just an SHA.
    git_commit* commit = nullptr;
    if (ctx->repo &&
        git_commit_lookup(&commit, ctx->repo, stashId) == 0 && commit) {
        const git_signature* committer = git_commit_committer(commit);
        if (committer) {
            e.when = QDateTime::fromSecsSinceEpoch(committer->when.time);
        }
        git_commit_free(commit);
    }

    ctx->out->push_back(std::move(e));
    return 0;
}

} // namespace

GitResult GitHandler::stashList(const QString& localPath,
                                std::vector<StashEntry>& out)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    StashListCtx ctx{&out, repo.get()};
    const int rc = git_stash_foreach(repo.get(), &stashListCb, &ctx);
    if (rc != 0) return lastError(QStringLiteral("List stashes"), rc);
    return GitResult::success();
}

GitResult GitHandler::stashApply(const QString& localPath, int index)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    git_stash_apply_options opts = GIT_STASH_APPLY_OPTIONS_INIT;
    // Default flags = GIT_STASH_APPLY_DEFAULT, which means "don't
    // restore the index". That matches `git stash apply` behaviour;
    // users wanting a full restore can re-stage afterwards.
    const int rc = git_stash_apply(repo.get(),
                                   static_cast<size_t>(index), &opts);
    if (rc == GIT_ECONFLICT) {
        return GitResult::failure(QStringLiteral(
            "Applying the stash produced merge conflicts. "
            "Resolve them and commit, or `git stash drop` to abandon."));
    }
    if (rc != 0) return lastError(QStringLiteral("Apply stash"), rc);
    return GitResult::success();
}

GitResult GitHandler::stashPop(const QString& localPath, int index)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    git_stash_apply_options opts = GIT_STASH_APPLY_OPTIONS_INIT;
    // git_stash_pop = apply + drop. If apply fails (conflicts), libgit2
    // does NOT drop, which is what `git stash pop` also does.
    const int rc = git_stash_pop(repo.get(),
                                 static_cast<size_t>(index), &opts);
    if (rc == GIT_ECONFLICT) {
        return GitResult::failure(QStringLiteral(
            "Pop produced merge conflicts; the stash was kept on the stack. "
            "Resolve, commit, then `git stash drop`."));
    }
    if (rc != 0) return lastError(QStringLiteral("Pop stash"), rc);
    return GitResult::success();
}

GitResult GitHandler::stashDrop(const QString& localPath, int index)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    const int rc = git_stash_drop(repo.get(), static_cast<size_t>(index));
    if (rc != 0) return lastError(QStringLiteral("Drop stash"), rc);
    return GitResult::success();
}

// ----- Tags ----------------------------------------------------------------

namespace {

struct TagListCtx {
    std::vector<TagInfo>* out;
    git_repository*       repo;
};

int tagListCb(const char* nameC, git_oid* /*oid*/, void* payload)
{
    // libgit2 hands us full ref names like "refs/tags/v1.0". Strip
    // the prefix so callers see just "v1.0".
    auto* ctx = static_cast<TagListCtx*>(payload);
    if (!ctx || !ctx->out || !nameC || !ctx->repo) return 0;

    QString fullRef = QString::fromUtf8(nameC);
    QString shortName = fullRef;
    if (shortName.startsWith(QStringLiteral("refs/tags/"))) {
        shortName = shortName.mid(QStringLiteral("refs/tags/").size());
    }

    // Resolve the ref to learn whether it's lightweight (points
    // straight at a commit) or annotated (points at a git_tag object).
    git_reference* ref = nullptr;
    QByteArray fullBytes = fullRef.toUtf8();
    if (git_reference_lookup(&ref, ctx->repo, fullBytes.constData()) != 0) {
        return 0;   // skip; iterate continues
    }

    TagInfo info;
    info.name = shortName;

    git_object* peeled = nullptr;
    if (git_reference_peel(&peeled, ref, GIT_OBJECT_ANY) == 0 && peeled) {
        const git_oid* oid = git_object_id(peeled);
        if (oid) {
            char buf[GIT_OID_SHA1_HEXSIZE + 1] = {};
            git_oid_tostr(buf, sizeof(buf), oid);
            info.targetSha = QString::fromLatin1(buf);
        }
        git_object_free(peeled);
    }

    // If the ref directly resolves to a tag object, it's annotated.
    git_object* direct = nullptr;
    if (git_reference_target(ref) &&
        git_object_lookup(&direct, ctx->repo, git_reference_target(ref),
                          GIT_OBJECT_ANY) == 0 && direct) {
        if (git_object_type(direct) == GIT_OBJECT_TAG) {
            info.isAnnotated = true;
            git_tag* tag = reinterpret_cast<git_tag*>(direct);
            const char* msg = git_tag_message(tag);
            if (msg) info.message = QString::fromUtf8(msg).trimmed();
        }
        git_object_free(direct);
    }
    git_reference_free(ref);

    ctx->out->push_back(std::move(info));
    return 0;
}

} // namespace

GitResult GitHandler::listTags(const QString& localPath,
                               std::vector<TagInfo>& out)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    TagListCtx ctx{&out, repo.get()};
    const int rc = git_tag_foreach(repo.get(), &tagListCb, &ctx);
    if (rc != 0) return lastError(QStringLiteral("List tags"), rc);

    // Alphabetical for predictable display. Tags don't have an
    // intrinsic ordering libgit2 enforces.
    std::sort(out.begin(), out.end(),
              [](const TagInfo& a, const TagInfo& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return GitResult::success();
}

GitResult GitHandler::createTag(const QString& localPath,
                                const QString& name,
                                const QString& message,
                                const QString& authorName,
                                const QString& authorEmail,
                                const SigningConfig& signingConfig)
{
    if (name.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Tag name is empty."));
    }

    // Reject SSH signing for tags up front — libgit2 doesn't expose
    // a tag-aware SSH signing path and manual byte construction
    // would require sshsig namespace handling that's out of scope.
    // User can fall back to `git tag -s` from the CLI.
    if (signingConfig.mode == SigningConfig::Mode::Ssh) {
        return GitResult::failure(QStringLiteral(
            "Tag signing with SSH is not supported by this app yet. "
            "Use GPG for tag signing, or run `git tag -s` from a "
            "terminal."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Resolve HEAD as the target — the UI flow always tags HEAD.
    ReferenceHandle head;
    int rc = git_repository_head(head.out(), repo.get());
    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND) {
        return GitResult::failure(QStringLiteral(
            "Cannot tag yet — make at least one commit first."));
    }
    if (rc != 0) return lastError(QStringLiteral("Resolve HEAD"), rc);

    ObjectHandle target;
    if (git_reference_peel(target.out(), head.get(), GIT_OBJECT_COMMIT) != 0) {
        return lastError(QStringLiteral("Peel HEAD to commit"), -1);
    }

    const QByteArray nameBytes = name.toUtf8();
    git_oid outOid;

    if (message.isEmpty()) {
        // Lightweight tag — no signature, no message.
        rc = git_tag_create_lightweight(&outOid, repo.get(),
                                        nameBytes.constData(),
                                        target.get(), /*force*/ 0);
        if (rc != 0) {
            return lastError(QStringLiteral("Create lightweight tag '%1'")
                                 .arg(name), rc);
        }
        return GitResult::success();
    }

    if (authorName.trimmed().isEmpty() || authorEmail.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral(
            "Annotated tags need a configured author identity. "
            "Set your name and email first, or leave the message "
            "empty for a lightweight tag."));
    }
    SignatureHandle sig;
    {
        const QByteArray nameU = authorName.toUtf8();
        const QByteArray emailU = authorEmail.toUtf8();
        rc = git_signature_now(sig.out(),
                               nameU.constData(), emailU.constData());
        if (rc != 0) return lastError(QStringLiteral("Create signature"), rc);
    }
    const QByteArray msgBytes = message.toUtf8();

    if (signingConfig.mode == SigningConfig::Mode::None) {
        // Unsigned annotated tag — simple path.
        rc = git_tag_create(&outOid, repo.get(),
                            nameBytes.constData(),
                            target.get(), sig.get(),
                            msgBytes.constData(), /*force*/ 0);
        if (rc != 0) {
            return lastError(QStringLiteral("Create annotated tag '%1'")
                                 .arg(name), rc);
        }
        return GitResult::success();
    }

    // -- Signed annotated tag (GPG) -----------------------------------
    //
    // libgit2 doesn't have git_tag_create_with_signature. Tag
    // signatures live in the tag object body (after the message,
    // separated by a blank line), not in a dedicated header. We
    // build the tag bytes manually:
    //
    //   object <target-oid>
    //   type commit
    //   tag <name>
    //   tagger <name> <email> <unix-time> <tz>
    //
    //   <message>
    //   <PGP signature block>
    //
    // git_tag_annotation_create gives us a starting buffer up to
    // and including the message; we then sign the WHOLE buffer
    // (that's what git does, see ref [1] in libgit2 issue #4969)
    // and append the signature with a trailing newline.

    // Step 1: create the annotation object (unsigned). This writes
    // a tag object to the odb that we then re-create from a buffer
    // with the signature appended. The intermediate object can be
    // pruned later — git gc handles it.
    git_oid annotOid;
    rc = git_tag_annotation_create(&annotOid, repo.get(),
                                    nameBytes.constData(),
                                    target.get(), sig.get(),
                                    msgBytes.constData());
    if (rc != 0) {
        return lastError(QStringLiteral("Build tag annotation '%1'")
                             .arg(name), rc);
    }

    // Step 2: read the annotation object's raw bytes from the odb.
    git_odb* odbRaw = nullptr;
    rc = git_repository_odb(&odbRaw, repo.get());
    if (rc != 0) return lastError(QStringLiteral("Open ODB"), rc);
    struct OdbGuard {
        git_odb* p;
        ~OdbGuard() { if (p) git_odb_free(p); }
    } odbG{odbRaw};

    git_odb_object* odbObj = nullptr;
    rc = git_odb_read(&odbObj, odbRaw, &annotOid);
    if (rc != 0) return lastError(QStringLiteral("Read annotation bytes"), rc);
    struct OdbObjGuard {
        git_odb_object* p;
        ~OdbObjGuard() { if (p) git_odb_object_free(p); }
    } objG{odbObj};

    const void*  rawData = git_odb_object_data(odbObj);
    const size_t rawSize = git_odb_object_size(odbObj);
    const QByteArray tagBuf(static_cast<const char*>(rawData),
                            static_cast<int>(rawSize));

    // Step 3: sign the buffer. Per git's spec, tag signing covers
    // the ENTIRE tag object body up to (but not including) the
    // signature — i.e. the bytes we just read.
    SignResult signRes = CommitSigner::signWithGpg(tagBuf, signingConfig.key);
    if (!signRes.ok) {
        return GitResult::failure(
            QStringLiteral("Tag signing failed:\n\n%1").arg(signRes.error));
    }

    // Step 4: construct the signed tag bytes by appending the
    // signature with a leading newline (so it lives on its own line
    // immediately after the message body, matching git's format).
    QByteArray signedTagBuf = tagBuf;
    if (!signedTagBuf.endsWith('\n')) signedTagBuf.append('\n');
    signedTagBuf.append(signRes.signature);
    if (!signedTagBuf.endsWith('\n')) signedTagBuf.append('\n');

    // Step 5: write the signed tag to the odb via
    // git_tag_create_from_buffer. The function writes the buffer
    // as a tag object AND creates the refs/tags/<name> reference
    // pointing at it.
    rc = git_tag_create_from_buffer(&outOid, repo.get(),
                                     signedTagBuf.constData(),
                                     /*force*/ 0);
    if (rc != 0) {
        return lastError(QStringLiteral("Write signed tag '%1'")
                             .arg(name), rc);
    }

    return GitResult::success();
}

GitResult GitHandler::deleteTag(const QString& localPath, const QString& name)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    const QByteArray nameBytes = name.toUtf8();
    const int rc = git_tag_delete(repo.get(), nameBytes.constData());
    if (rc != 0) return lastError(QStringLiteral("Delete tag '%1'").arg(name), rc);
    return GitResult::success();
}

// ----- Submodules ----------------------------------------------------------

namespace {

// Map libgit2's git_submodule_status_t bitmask to our single-state
// enum. The bitmask carries fine-grained info ("workdir has uncommitted
// changes", "url mismatched between .gitmodules and .git/config" etc.)
// but for the UI we collapse to one representative status. Order
// matters here — Missing wins over UrlMismatch wins over Modified
// wins over UpToDate, because that's the order users care about.
SubmoduleInfo::Status mapStatus(unsigned int statusBits, bool inWorkdir)
{
    // GIT_SUBMODULE_STATUS_IN_HEAD     — recorded in HEAD commit
    // GIT_SUBMODULE_STATUS_IN_INDEX    — recorded in index
    // GIT_SUBMODULE_STATUS_IN_CONFIG   — listed in .git/config
    // GIT_SUBMODULE_STATUS_IN_WD       — present in working dir
    // GIT_SUBMODULE_STATUS_INDEX_*     — diffs between index and HEAD
    // GIT_SUBMODULE_STATUS_WD_*        — diffs in workdir
    if (statusBits & GIT_SUBMODULE_STATUS_WD_UNINITIALIZED) {
        return SubmoduleInfo::Status::NotInitialized;
    }
    if (!inWorkdir) {
        return SubmoduleInfo::Status::NotInitialized;
    }
    // "Missing" here means listed in index but not in .gitmodules.
    // The .gitmodules-only listing happens later in listSubmodules
    // so we can't detect that here from bits alone.

    // Any modification = treat as Modified.
    const unsigned int dirtyMask =
        GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED |
        GIT_SUBMODULE_STATUS_WD_WD_MODIFIED    |
        GIT_SUBMODULE_STATUS_WD_UNTRACKED      |
        GIT_SUBMODULE_STATUS_WD_MODIFIED       |
        GIT_SUBMODULE_STATUS_INDEX_MODIFIED;
    if (statusBits & dirtyMask) {
        return SubmoduleInfo::Status::Modified;
    }
    return SubmoduleInfo::Status::UpToDate;
}

QString oidToShortSha(const git_oid* oid)
{
    if (!oid) return {};
    char buf[GIT_OID_HEXSZ + 1] = {0};
    git_oid_tostr(buf, sizeof(buf), oid);
    return QString::fromLatin1(buf);
}

} // namespace

GitResult GitHandler::listSubmodules(const QString& localPath,
                                      std::vector<SubmoduleInfo>& out)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // git_submodule_foreach iterates every name listed in .gitmodules.
    // The payload struct lets us push back into our vector without
    // needing C++ lambda capture (the callback is C-linkage).
    struct ForeachCtx {
        git_repository*              repo;
        std::vector<SubmoduleInfo>*  out;
        QString                      error;
    };
    ForeachCtx ctx{repo.get(), &out, {}};

    auto cb = [](git_submodule* sm, const char* name, void* payload) -> int {
        auto* fc = static_cast<ForeachCtx*>(payload);
        SubmoduleInfo info;
        info.name = QString::fromUtf8(name ? name : git_submodule_name(sm));
        info.path = QString::fromUtf8(git_submodule_path(sm));
        info.url  = QString::fromUtf8(git_submodule_url(sm));

        // .git/config URL may differ from .gitmodules URL. libgit2's
        // git_submodule_url() returns the .gitmodules value (the
        // "intended" URL); to get the .git/config value (the "active"
        // URL libgit2 will actually fetch from) we read the config
        // directly. When they differ, the user needs to run
        // git_submodule_sync to propagate the change.
        //
        // We read into a local git_config_entry. The entry pointer
        // is owned by libgit2 and freed by us via git_config_entry_free.
        info.configUrl = info.url;  // default: assume same
        git_config* repoCfg = nullptr;
        if (git_repository_config_snapshot(&repoCfg, fc->repo) == 0) {
            const QByteArray key =
                QStringLiteral("submodule.%1.url").arg(info.name).toUtf8();
            git_config_entry* entry = nullptr;
            if (git_config_get_entry(&entry, repoCfg, key.constData()) == 0 &&
                entry && entry->value) {
                info.configUrl = QString::fromUtf8(entry->value);
                git_config_entry_free(entry);
            }
            // If get_entry failed, the submodule isn't init'd yet —
            // .git/config has no submodule.<name>.url entry. That's
            // fine; we leave configUrl == url (matching state) so
            // we don't flag URL mismatch on uninitialized submodules.
            git_config_free(repoCfg);
        }

        // Recorded SHA: what the parent's index says this submodule
        // should be at.
        if (const git_oid* idxOid = git_submodule_index_id(sm)) {
            info.recordedSha = oidToShortSha(idxOid);
        }
        // Workdir SHA: what the submodule's HEAD actually is.
        if (const git_oid* wdOid = git_submodule_wd_id(sm)) {
            info.workdirSha = oidToShortSha(wdOid);
        }

        // Status bits.
        unsigned int statusBits = 0;
        const int src = git_submodule_status(&statusBits, fc->repo,
                                              info.name.toUtf8().constData(),
                                              GIT_SUBMODULE_IGNORE_UNSPECIFIED);
        if (src == 0) {
            const bool inWd = (statusBits & GIT_SUBMODULE_STATUS_IN_WD) != 0;
            info.status = mapStatus(statusBits, inWd);

            // URL mismatch supersedes other statuses (but only when
            // the submodule is initialized — uninit submodules
            // legitimately have no .git/config URL yet). The user
            // needs to know to sync before doing anything else.
            const bool initialized =
                info.status != SubmoduleInfo::Status::NotInitialized;
            if (initialized &&
                !info.configUrl.isEmpty() &&
                info.configUrl != info.url) {
                info.status = SubmoduleInfo::Status::UrlMismatch;
            }
        } else {
            info.status = SubmoduleInfo::Status::Unknown;
        }
        fc->out->push_back(std::move(info));
        return 0;  // keep iterating
    };

    const int rc = git_submodule_foreach(repo.get(), cb, &ctx);
    if (rc != 0) return lastError(QStringLiteral("List submodules"), rc);
    return GitResult::success();
}

GitResult GitHandler::initAndUpdateSubmodule(const QString& localPath,
                                              const QString& name,
                                              const QString& token,
                                              const ProgressFn& progress)
{
    // Delegate to the with-creds variant with empty SshCredentials.
    // Backward-compat shim so existing callers (worker) don't have
    // to pass an empty struct every time.
    return initAndUpdateSubmoduleWithCreds(
        localPath, name, token, SshCredentials{}, progress);
}

GitResult GitHandler::updateSubmodule(const QString& localPath,
                                       const QString& name,
                                       const QString& token,
                                       const ProgressFn& progress)
{
    return updateSubmoduleWithCreds(
        localPath, name, token, SshCredentials{}, progress);
}

GitResult GitHandler::syncSubmoduleUrl(const QString& localPath,
                                        const QString& name)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    git_submodule* smRaw = nullptr;
    const QByteArray nameBytes = name.toUtf8();
    int rc = git_submodule_lookup(&smRaw, repo.get(), nameBytes.constData());
    if (rc != 0) return lastError(
        QStringLiteral("Lookup submodule '%1'").arg(name), rc);
    struct SubmoduleHandle {
        git_submodule* p;
        ~SubmoduleHandle() { if (p) git_submodule_free(p); }
    } smGuard{smRaw};

    // git_submodule_sync copies the URL from .gitmodules into both
    // .git/config and the submodule's own .git/config (if init'd).
    // No network, fast.
    rc = git_submodule_sync(smRaw);
    if (rc != 0) return lastError(
        QStringLiteral("Sync submodule '%1'").arg(name), rc);
    return GitResult::success();
}

// Internal helper — does the actual init+update work used by both
// the no-creds and with-creds variants. The two public methods are
// thin wrappers that build a CallbackCtx with or without explicit
// SSH credentials. Splitting it this way avoids copy-pasting the
// libgit2 dance and keeps the cred-handling logic in one place.
namespace {
GitResult doSubmoduleInitUpdate(ProgressFn progress,
                                 git_repository* repo,
                                 const QString& name,
                                 const QString& token,
                                 const SshCredentials& sshCreds,
                                 bool runInit)
{
    git_submodule* smRaw = nullptr;
    const QByteArray nameBytes = name.toUtf8();
    int rc = git_submodule_lookup(&smRaw, repo, nameBytes.constData());
    if (rc != 0) return lastError(
        QStringLiteral("Lookup submodule '%1'").arg(name), rc);
    struct SubmoduleHandle {
        git_submodule* p;
        ~SubmoduleHandle() { if (p) git_submodule_free(p); }
    } smGuard{smRaw};

    if (runInit) {
        rc = git_submodule_init(smRaw, /*overwrite*/ 0);
        if (rc != 0) return lastError(
            QStringLiteral("Init submodule '%1'").arg(name), rc);
    }

    CallbackCtx ctx;
    ctx.token    = token.toUtf8();
    ctx.progress = progress;
    if (sshCreds.isExplicit()) {
        ctx.useExplicitKey = true;
        ctx.sshKeyPath     = sshCreds.keyPath.toUtf8();
        ctx.sshPubKeyPath  = sshCreds.publicKeyPath.toUtf8();
        ctx.sshPassphrase  = sshCreds.passphrase.toUtf8();
    }

    git_submodule_update_options opts = GIT_SUBMODULE_UPDATE_OPTIONS_INIT;
    opts.fetch_opts.callbacks.credentials       = &credCb;
    opts.fetch_opts.callbacks.transfer_progress = &transferCb;
    opts.fetch_opts.callbacks.certificate_check = &certificateCheckCb;
    opts.fetch_opts.callbacks.payload           = &ctx;

    rc = git_submodule_update(smRaw, /*init*/ runInit ? 1 : 0, &opts);
    if (rc != 0) {
        return rewriteSshAgentError(
            lastError(QStringLiteral("Update submodule '%1'").arg(name), rc),
            ctx);
    }
    return GitResult::success();
}
} // namespace

GitResult GitHandler::initAndUpdateSubmoduleWithCreds(
    const QString& localPath, const QString& name, const QString& token,
    const SshCredentials& sshCreds, const ProgressFn& progress)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;
    return doSubmoduleInitUpdate(progress, repo.get(),
                                  name, token, sshCreds, /*runInit*/ true);
}

GitResult GitHandler::updateSubmoduleWithCreds(
    const QString& localPath, const QString& name, const QString& token,
    const SshCredentials& sshCreds, const ProgressFn& progress)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;
    return doSubmoduleInitUpdate(progress, repo.get(),
                                  name, token, sshCreds, /*runInit*/ false);
}

GitResult GitHandler::addSubmodule(const QString& localPath,
                                    const QString& url,
                                    const QString& subPath,
                                    const QString& token,
                                    const SshCredentials& sshCreds,
                                    const ProgressFn& progress)
{
    // Pre-flight: sanity-check inputs before touching libgit2. Bad
    // inputs from the dialog should give clear errors here rather
    // than libgit2's internal "Reference 'refs/heads/...' is not
    // valid" or similar cryptic message.
    if (url.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Submodule URL is empty."));
    }
    if (subPath.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Submodule path is empty."));
    }
    // Target dir must not exist as a non-empty directory — that's
    // what `git submodule add` itself checks. Empty dir is OK (some
    // workflows mkdir the path first).
    const QString absSub = QDir(localPath).absoluteFilePath(subPath);
    if (QFileInfo::exists(absSub) && !QDir(absSub).isEmpty()) {
        return GitResult::failure(QStringLiteral(
            "Target path '%1' already exists and isn't empty.").arg(subPath));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    const QByteArray urlBytes  = url.toUtf8();
    const QByteArray pathBytes = subPath.toUtf8();

    // Step 1 — submodule_add_setup. Creates .gitmodules entry,
    // initialises a subrepo at subPath, returns a submodule handle.
    // use_gitlink=1 means store the subrepo under .git/modules/<name>
    // (the modern layout); set to 0 only for very old git compat.
    git_submodule* smRaw = nullptr;
    int rc = git_submodule_add_setup(&smRaw, repo.get(),
                                      urlBytes.constData(),
                                      pathBytes.constData(),
                                      /*use_gitlink*/ 1);
    if (rc != 0) return lastError(
        QStringLiteral("Set up submodule at '%1'").arg(subPath), rc);
    struct SubmoduleHandle {
        git_submodule* p;
        ~SubmoduleHandle() { if (p) git_submodule_free(p); }
    } smGuard{smRaw};

    // Step 2 — actually clone the submodule's contents. We have to
    // open the subrepo (just-created by add_setup) and run git_clone
    // through it. Credentials/host-key flow exactly like top-level
    // clone() — same callbacks, same CallbackCtx structure.
    git_repository* subRepoRaw = nullptr;
    rc = git_submodule_open(&subRepoRaw, smRaw);
    if (rc != 0) return lastError(
        QStringLiteral("Open new submodule subrepo"), rc);
    RepoHandle subRepo;
    *subRepo.out() = subRepoRaw;  // transfer ownership to RAII

    // libgit2 1.9 doesn't expose a single "fetch+checkout" call for
    // an already-set-up submodule — we have to do it via the update
    // codepath, which is what submodule_add ends up using internally
    // anyway. So: skip the manual clone and call our existing
    // doSubmoduleInitUpdate helper. The submodule is now "added"
    // (gitlink will be created), just not fetched yet.
    //
    // Get the name from the submodule handle for the helper.
    const QString smName = QString::fromUtf8(git_submodule_name(smRaw));
    auto updRes = doSubmoduleInitUpdate(progress, repo.get(),
                                         smName, token, sshCreds,
                                         /*runInit*/ true);
    if (!updRes.ok) {
        // Setup succeeded but fetch failed — the .gitmodules entry
        // and empty subdir are still there. User can retry with
        // Init & Update from the Submodules tab, or remove the
        // entry manually. We surface the error as-is so they know
        // what happened.
        return updRes;
    }

    // Step 3 — finalize. Stages .gitmodules + the gitlink in the
    // parent's index, so the user only needs to commit afterwards.
    rc = git_submodule_add_finalize(smRaw);
    if (rc != 0) return lastError(
        QStringLiteral("Finalize submodule add"), rc);

    return GitResult::success();
}

GitResult GitHandler::removeSubmodule(const QString& localPath,
                                       const QString& name)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Lookup so we know the submodule actually exists, and to get
    // its path (which may differ from its name).
    git_submodule* smRaw = nullptr;
    const QByteArray nameBytes = name.toUtf8();
    int rc = git_submodule_lookup(&smRaw, repo.get(), nameBytes.constData());
    if (rc != 0) return lastError(
        QStringLiteral("Lookup submodule '%1'").arg(name), rc);
    const QString subPath = QString::fromUtf8(git_submodule_path(smRaw));
    git_submodule_free(smRaw);

    // -- Step 1: remove from .gitmodules ----------------------------------
    //
    // libgit2 doesn't have a single "delete submodule section" call.
    // We open the .gitmodules file as a config and delete entries
    // matching submodule.<name>.*. git_config_delete_multivar with
    // a regex matches all keys in one go.
    const QByteArray sectionPattern =
        QStringLiteral("^submodule\\.%1\\.").arg(name).toUtf8();

    {
        const QString gitmodulesPath = QDir(localPath)
            .absoluteFilePath(QStringLiteral(".gitmodules"));
        git_config* cfgRaw = nullptr;
        rc = git_config_open_ondisk(&cfgRaw, gitmodulesPath.toUtf8().constData());
        if (rc == 0) {
            // Iterate all entries and delete the ones matching the
            // submodule. We can't use delete_multivar with a section
            // pattern in older libgit2 reliably, so we do it the
            // straightforward way.
            const QByteArray urlKey  = QStringLiteral("submodule.%1.url").arg(name).toUtf8();
            const QByteArray pathKey = QStringLiteral("submodule.%1.path").arg(name).toUtf8();
            git_config_delete_entry(cfgRaw, urlKey.constData());
            git_config_delete_entry(cfgRaw, pathKey.constData());
            // Optional keys — these may not exist; ignore errors.
            const QByteArray branchKey =
                QStringLiteral("submodule.%1.branch").arg(name).toUtf8();
            git_config_delete_entry(cfgRaw, branchKey.constData());
            git_config_free(cfgRaw);
        }
        // If .gitmodules doesn't exist, that's OK — nothing to remove.
    }

    // -- Step 2: remove from .git/config ----------------------------------
    {
        ConfigHandle cfg;
        if (git_repository_config(cfg.out(), repo.get()) == 0) {
            const QByteArray urlKey  = QStringLiteral("submodule.%1.url").arg(name).toUtf8();
            git_config_delete_entry(cfg.get(), urlKey.constData());
            // Other keys are sometimes left around — don't worry
            // about them, they're harmless once url is gone.
        }
    }

    // -- Step 3: remove gitlink from index --------------------------------
    {
        git_index* idxRaw = nullptr;
        if (git_repository_index(&idxRaw, repo.get()) == 0) {
            git_index_remove_bypath(idxRaw, subPath.toUtf8().constData());
            git_index_write(idxRaw);
            git_index_free(idxRaw);
        }
    }

    // -- Step 4: delete .git/modules/<name>/ ------------------------------
    //
    // The embedded subrepo lives at .git/modules/<name>/ — wipe it
    // so subsequent re-adds don't pick up stale state.
    {
        const QString modulesDir = QDir(localPath)
            .absoluteFilePath(QStringLiteral(".git/modules/") + name);
        if (QFileInfo::exists(modulesDir)) {
            QDir(modulesDir).removeRecursively();
        }
    }

    // -- Step 5: delete the workdir directory -----------------------------
    {
        const QString absSub = QDir(localPath).absoluteFilePath(subPath);
        if (QFileInfo::exists(absSub)) {
            QDir(absSub).removeRecursively();
        }
    }

    // User now has staged deletions of the gitlink + edits to
    // .gitmodules. They need to `git commit` to finish — same as
    // command-line `git rm <submodule>` workflow.
    return GitResult::success();
}

// ----- Conflict resolution -------------------------------------------------

namespace {

// Convert a git_index_entry's blob OID to a hex string. Returns empty
// string for null pointers (libgit2 hands us nullptr when a conflict
// stage is "missing", e.g. add/add has no ancestor).
QString oidToString(const git_oid* oid)
{
    if (!oid) return QString();
    char buf[GIT_OID_SHA1_HEXSIZE + 1] = {};
    git_oid_tostr(buf, sizeof(buf), oid);
    return QString::fromLatin1(buf);
}

} // namespace

GitResult GitHandler::listConflicts(const QString& localPath,
                                    std::vector<ConflictEntry>& out)
{
    out.clear();

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    IndexHandle index;
    int rc = git_repository_index(index.out(), repo.get());
    if (rc != 0) return lastError(QStringLiteral("Open index"), rc);

    if (git_index_has_conflicts(index.get()) == 0) {
        // No conflicts — empty result is success, not failure.
        return GitResult::success();
    }

    ConflictIterH iter;
    rc = git_index_conflict_iterator_new(iter.out(), index.get());
    if (rc != 0) return lastError(QStringLiteral("Create conflict iterator"), rc);

    while (true) {
        const git_index_entry* ancestor = nullptr;
        const git_index_entry* ours     = nullptr;
        const git_index_entry* theirs   = nullptr;
        rc = git_index_conflict_next(&ancestor, &ours, &theirs, iter.get());
        if (rc == GIT_ITEROVER) break;
        if (rc != 0) {
            return lastError(QStringLiteral("Next conflict"), rc);
        }

        ConflictEntry e;
        // The path lives in whichever stage entry is non-null. At
        // least one is always non-null for a real conflict; we prefer
        // ours → theirs → ancestor for the path display because that
        // matches the order users think about (HEAD's name first).
        if      (ours)     e.path = QString::fromUtf8(ours->path);
        else if (theirs)   e.path = QString::fromUtf8(theirs->path);
        else if (ancestor) e.path = QString::fromUtf8(ancestor->path);

        if (ancestor) e.ancestorSha = oidToString(&ancestor->id);
        if (ours)     e.oursSha     = oidToString(&ours->id);
        if (theirs)   e.theirsSha   = oidToString(&theirs->id);

        if (!e.path.isEmpty()) out.push_back(std::move(e));
    }
    return GitResult::success();
}

GitResult GitHandler::loadConflictBlobs(const QString& localPath,
                                        ConflictEntry& entry)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    auto loadOne = [&](const QString& sha, QByteArray& out) -> GitResult {
        out.clear();
        if (sha.isEmpty()) return GitResult::success();   // nothing to load

        git_oid oid;
        if (git_oid_fromstr(&oid, sha.toLatin1().constData()) != 0) {
            return lastError(QStringLiteral("Parse OID %1").arg(sha), -1);
        }
        BlobHandle blob;
        const int rc = git_blob_lookup(blob.out(), repo.get(), &oid);
        if (rc != 0) return lastError(QStringLiteral("Lookup blob %1").arg(sha), rc);

        const void* data = git_blob_rawcontent(blob.get());
        const git_object_size_t size = git_blob_rawsize(blob.get());
        out = QByteArray(static_cast<const char*>(data),
                         static_cast<int>(size));
        return GitResult::success();
    };

    if (auto rc = loadOne(entry.ancestorSha, entry.ancestorContent); !rc.ok) return rc;
    if (auto rc = loadOne(entry.oursSha,     entry.oursContent);     !rc.ok) return rc;
    if (auto rc = loadOne(entry.theirsSha,   entry.theirsContent);   !rc.ok) return rc;
    return GitResult::success();
}

GitResult GitHandler::markResolved(const QString& localPath, const QString& path)
{
    if (path.trimmed().isEmpty()) {
        return GitResult::failure(QStringLiteral("Path is empty."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    IndexHandle index;
    int rc = git_repository_index(index.out(), repo.get());
    if (rc != 0) return lastError(QStringLiteral("Open index"), rc);

    const QByteArray pathBytes = path.toUtf8();

    // Two-step "resolve":
    //   1. Drop the conflict stages (ancestor/ours/theirs entries) for
    //      this path. Without this, git still considers the path
    //      conflicted even after we add the resolved version.
    //   2. Add the working-tree version to stage 0 (the normal index).
    //
    // git_index_remove_bypath removes ALL stages for the path, which
    // is exactly what we want — both the conflict entries and any
    // existing stage-0 entry are cleared before re-adding.
    rc = git_index_remove_bypath(index.get(), pathBytes.constData());
    if (rc != 0 && rc != GIT_ENOTFOUND) {
        return lastError(QStringLiteral("Remove conflict stages for '%1'").arg(path), rc);
    }

    rc = git_index_add_bypath(index.get(), pathBytes.constData());
    if (rc != 0) {
        // If the file was deleted-resolved (user wants to keep the
        // delete), add_bypath returns ENOTFOUND — that's fine, the
        // earlier remove_bypath already did the right thing.
        if (rc == GIT_ENOTFOUND) {
            // Persist the index as-is (with the path removed).
            if (git_index_write(index.get()) != 0) {
                return lastError(QStringLiteral("Write index"), -1);
            }
            return GitResult::success();
        }
        return lastError(QStringLiteral("Stage resolved '%1'").arg(path), rc);
    }

    if (git_index_write(index.get()) != 0) {
        return lastError(QStringLiteral("Write index"), -1);
    }
    return GitResult::success();
}

GitResult GitHandler::abortMerge(const QString& localPath)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // libgit2 doesn't expose a direct "merge --abort". The recipe is:
    //   1. git_repository_state_cleanup() — delete MERGE_HEAD,
    //      MERGE_MSG, etc. so the repo is no longer "merging".
    //   2. Hard-reset the working tree and index back to HEAD so any
    //      half-applied merge changes go away.
    //
    // The order matters: if we reset first, libgit2 may complain about
    // the merge state during checkout.
    if (git_repository_state_cleanup(repo.get()) != 0) {
        return lastError(QStringLiteral("Clean up merge state"), -1);
    }

    ReferenceHandle head;
    if (git_repository_head(head.out(), repo.get()) != 0) {
        return lastError(QStringLiteral("Resolve HEAD"), -1);
    }
    ObjectHandle headObj;
    if (git_reference_peel(headObj.out(), head.get(), GIT_OBJECT_COMMIT) != 0) {
        return lastError(QStringLiteral("Peel HEAD"), -1);
    }

    // GIT_RESET_HARD throws away working-tree changes — that's exactly
    // what `git merge --abort` does too.
    if (git_reset(repo.get(), headObj.get(), GIT_RESET_HARD, nullptr) != 0) {
        return lastError(QStringLiteral("Reset to HEAD"), -1);
    }
    return GitResult::success();
}

bool GitHandler::isMerging(const QString& localPath)
{
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return false;

    const int state = git_repository_state(repo.get());
    // We treat any non-NONE state as "in progress" because the conflict
    // UI is meaningful for merge, rebase, cherry-pick, revert, and
    // bisect. The user's edit-then-mark-resolved flow is the same
    // across them; only the final commit-or-continue step differs,
    // and that's handled by the user via the regular commit dialog.
    return state != GIT_REPOSITORY_STATE_NONE;
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
                             QString* outSha,
                             const SigningConfig& signingConfig)
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
                const git_oid* poid = git_object_id(headObj.get());
                if (git_commit_lookup(parent.out(), repo.get(), poid) == 0) {
                    hasParent = true;
                }
            }
        } else if (hrc != GIT_EUNBORNBRANCH && hrc != GIT_ENOTFOUND) {
            return lastError(QStringLiteral("Resolve HEAD"), hrc);
        }
    }

    const QByteArray msg = message.toUtf8();
    const git_commit* parents[1];
    size_t parentCount = 0;
    if (hasParent) {
        parents[0] = parent.get();
        parentCount = 1;
    }

    git_oid commitOid;

    // Two paths: signed and unsigned. The unsigned path is the
    // simple one — git_commit_create writes directly to HEAD. The
    // signed path is multi-step:
    //   1. git_commit_create_buffer  → unsigned commit bytes
    //   2. CommitSigner (subprocess) → signature bytes
    //   3. git_commit_create_with_signature  → writes signed commit
    //   4. Manually update HEAD (because create_with_signature doesn't)
    //
    // The signed path costs a subprocess invocation (gpg/ssh-keygen)
    // plus a few extra libgit2 calls. For a successful commit it's
    // typically 100-500ms slower than unsigned.

    if (signingConfig.mode == SigningConfig::Mode::None) {
        rc = git_commit_create(&commitOid, repo.get(), "HEAD",
                               sig.get(), sig.get(),
                               nullptr /* utf-8 default */,
                               msg.constData(),
                               tree.get(),
                               parentCount, parentCount ? parents : nullptr);
        if (rc != 0) return lastError(QStringLiteral("Create commit"), rc);
    } else {
        // Step 1: build the unsigned commit buffer.
        git_buf bufRaw = GIT_BUF_INIT_CONST(nullptr, 0);
        rc = git_commit_create_buffer(&bufRaw, repo.get(),
                                       sig.get(), sig.get(),
                                       nullptr /* utf-8 default */,
                                       msg.constData(),
                                       tree.get(),
                                       parentCount,
                                       parentCount ? parents : nullptr);
        if (rc != 0) return lastError(QStringLiteral("Build commit buffer"), rc);
        // RAII for the buffer — libgit2 needs git_buf_dispose to free.
        struct BufGuard {
            git_buf* b;
            ~BufGuard() { if (b) git_buf_dispose(b); }
        } bg{&bufRaw};

        const QByteArray bufBytes(bufRaw.ptr, static_cast<int>(bufRaw.size));

        // Step 2: sign with the external tool. CommitSigner returns
        // a SignResult with either the signature bytes or an error
        // string. The error already explains the typical failure
        // modes (gpg not installed, agent missing, etc.) so we just
        // surface it as-is.
        SignResult signRes;
        if (signingConfig.mode == SigningConfig::Mode::Gpg) {
            signRes = CommitSigner::signWithGpg(bufBytes, signingConfig.key);
        } else {
            signRes = CommitSigner::signWithSsh(bufBytes, signingConfig.key);
        }
        if (!signRes.ok) {
            return GitResult::failure(
                QStringLiteral("Commit signing failed:\n\n%1")
                    .arg(signRes.error));
        }

        // Step 3: write the signed commit object. "gpgsig" is the
        // header git uses for BOTH GPG and SSH signatures — yes,
        // even SSH ones live under "gpgsig". (git could have named
        // it "sig" but didn't, for backward compat.)
        rc = git_commit_create_with_signature(
            &commitOid, repo.get(),
            bufBytes.constData(),
            signRes.signature.constData(),
            "gpgsig");
        if (rc != 0) return lastError(
            QStringLiteral("Write signed commit"), rc);

        // Step 4: update HEAD ref to point at the new commit. The
        // _buffer/_with_signature pair doesn't move HEAD for us
        // (unlike git_commit_create, which takes the ref name and
        // moves it automatically). We do it explicitly here.
        ReferenceHandle head;
        if (git_repository_head(head.out(), repo.get()) == 0) {
            // HEAD already points at a branch — update that ref.
            ReferenceHandle newRef;
            rc = git_reference_set_target(newRef.out(), head.get(),
                                           &commitOid,
                                           "commit (signed)");
            if (rc != 0) return lastError(
                QStringLiteral("Update HEAD after signed commit"), rc);
        } else {
            // Unborn HEAD (first commit). Create the branch ref
            // ourselves. We look at HEAD's symbolic target to get
            // the branch name (e.g. "refs/heads/master").
            const char* headTarget = nullptr;
            ReferenceHandle headSym;
            if (git_reference_lookup(headSym.out(), repo.get(), "HEAD") == 0) {
                headTarget = git_reference_symbolic_target(headSym.get());
            }
            const QByteArray refName = headTarget
                ? QByteArray(headTarget)
                : QByteArray("refs/heads/master");
            ReferenceHandle newRef;
            rc = git_reference_create(newRef.out(), repo.get(),
                                       refName.constData(),
                                       &commitOid,
                                       /*force*/ 0,
                                       "first commit (signed)");
            if (rc != 0) return lastError(
                QStringLiteral("Create branch for first signed commit"), rc);
        }
    }

    if (outSha) {
        char buf[GIT_OID_SHA1_HEXSIZE + 1] = {};
        git_oid_tostr(buf, sizeof(buf), &commitOid);
        *outSha = QString::fromUtf8(buf);
    }
    return GitResult::success();
}

GitResult GitHandler::verifyCommitSignature(const QString& localPath,
                                              const QString& sha,
                                              const QString& allowedSignersPath,
                                              VerifyResult& out)
{
    out = VerifyResult{};  // default: Unsigned

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    git_oid oid;
    int rc = git_oid_fromstr(&oid, sha.toUtf8().constData());
    if (rc != 0) return lastError(
        QStringLiteral("Parse commit SHA '%1'").arg(sha), rc);

    // git_commit_extract_signature: pulls the gpgsig header into the
    // first git_buf and the signed-data (commit without the header)
    // into the second. Passing NULL as the field name uses the
    // default "gpgsig".
    git_buf sigBuf  = GIT_BUF_INIT_CONST(nullptr, 0);
    git_buf dataBuf = GIT_BUF_INIT_CONST(nullptr, 0);
    struct BufGuard {
        git_buf* a;
        git_buf* b;
        ~BufGuard() {
            if (a) git_buf_dispose(a);
            if (b) git_buf_dispose(b);
        }
    } bg{&sigBuf, &dataBuf};

    rc = git_commit_extract_signature(&sigBuf, &dataBuf,
                                       repo.get(), &oid, nullptr);
    if (rc == GIT_ENOTFOUND) {
        // No signature → leave out.status = Unsigned. Not an error.
        out.status = VerifyResult::Status::Unsigned;
        return GitResult::success();
    }
    if (rc != 0) return lastError(
        QStringLiteral("Extract signature for %1").arg(sha), rc);

    const QByteArray signature(sigBuf.ptr,  static_cast<int>(sigBuf.size));
    const QByteArray signedBody(dataBuf.ptr, static_cast<int>(dataBuf.size));

    // For SSH verify we need the committer's email as the principal
    // identity. Pull it from the commit object.
    CommitHandle commit;
    if (git_commit_lookup(commit.out(), repo.get(), &oid) != 0) {
        // Couldn't load the commit object — surface as invalid sig
        // rather than crashing. Caller treats Invalid as a verdict
        // worth showing the user.
        out.status = VerifyResult::Status::Invalid;
        out.error  = QStringLiteral("Couldn't load commit object.");
        return GitResult::success();
    }
    QString committerEmail;
    if (const git_signature* c = git_commit_committer(commit.get())) {
        committerEmail = QString::fromUtf8(c->email);
    }

    bool isSsh = false;
    if (!SignatureVerifier::detectFormat(signature, &isSsh)) {
        out.status = VerifyResult::Status::Invalid;
        out.error  = QStringLiteral("Unrecognized signature format.");
        return GitResult::success();
    }

    out = isSsh
        ? SignatureVerifier::verifySsh(signature, signedBody,
                                        committerEmail, allowedSignersPath)
        : SignatureVerifier::verifyGpg(signature, signedBody);
    return GitResult::success();
}

GitResult GitHandler::log(const QString& localPath, int maxCount,
                          std::vector<CommitInfo>& out,
                          bool computeStats,
                          const QString& afterSha)
{
    out.clear();
    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    RevwalkHandle walk;
    int rc = git_revwalk_new(walk.out(), repo.get());
    if (rc != 0) return lastError(QStringLiteral("Create revwalk"), rc);

    git_revwalk_sorting(walk.get(), GIT_SORT_TIME | GIT_SORT_TOPOLOGICAL);

    // Two start modes:
    //   afterSha empty → start at HEAD (initial fetch)
    //   afterSha set   → start at the given commit (pagination).
    //                    The starting commit itself is then skipped
    //                    so the caller doesn't get a duplicate of
    //                    the oldest entry they already had.
    bool skipFirst = false;
    if (afterSha.isEmpty()) {
        rc = git_revwalk_push_head(walk.get());
        if (rc == GIT_ENOTFOUND || rc == GIT_EUNBORNBRANCH) {
            // No commits yet; empty log is fine.
            return GitResult::success();
        }
        if (rc != 0) return lastError(QStringLiteral("Push HEAD onto revwalk"), rc);
    } else {
        git_oid afterOid;
        const QByteArray bytes = afterSha.toLatin1();
        if (git_oid_fromstr(&afterOid, bytes.constData()) != 0) {
            return GitResult::failure(QStringLiteral(
                "Invalid SHA passed as afterSha: %1").arg(afterSha));
        }
        rc = git_revwalk_push(walk.get(), &afterOid);
        if (rc != 0) return lastError(
            QStringLiteral("Push %1 onto revwalk").arg(afterSha.left(7)), rc);
        skipFirst = true;
    }

    git_oid oid;
    int count = 0;
    while (git_revwalk_next(&oid, walk.get()) == 0) {
        // First entry when paginating is the afterSha commit itself —
        // caller already has it, so we drop it without counting.
        if (skipFirst) { skipFirst = false; continue; }

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

        // Cheap signature presence check — no subprocess, just
        // libgit2 parsing the commit object's headers. The verify
        // proper happens later, lazily, only for commits we
        // actually display.
        {
            git_buf sigBuf  = GIT_BUF_INIT_CONST(nullptr, 0);
            git_buf dataBuf = GIT_BUF_INIT_CONST(nullptr, 0);
            const int sigRc = git_commit_extract_signature(
                &sigBuf, &dataBuf, repo.get(), &oid, nullptr);
            info.hasSignature = (sigRc == 0);
            git_buf_dispose(&sigBuf);
            git_buf_dispose(&dataBuf);
        }

        out.push_back(std::move(info));
        ++count;
    }

    // Second pass: optional diff-stat computation. We do this after
    // the walk because each diff needs its own libgit2 objects and
    // mixing the two would clutter the main loop. Worst-case cost is
    // 200 commits × ~20ms = ~4s for medium repos — that's why
    // computeStats defaults to false.
    if (computeStats) {
        for (auto& info : out) {
            // Look up commit by id.
            git_oid coid;
            const QByteArray idBytes = info.id.toLatin1();
            if (git_oid_fromstr(&coid, idBytes.constData()) != 0) continue;

            CommitHandle commit;
            if (git_commit_lookup(commit.out(), repo.get(), &coid) != 0) continue;

            TreeHandle thisTree;
            if (git_commit_tree(thisTree.out(), commit.get()) != 0) continue;

            // First-parent diff matches what `git log --shortstat`
            // produces. Merge commits: we report against first parent
            // (octopus merges thus undercount, but that matches git's
            // default behaviour).
            TreeHandle parentTree;
            git_tree* parentTreePtr = nullptr;
            if (git_commit_parentcount(commit.get()) > 0) {
                CommitHandle parent;
                if (git_commit_parent(parent.out(), commit.get(), 0) == 0 &&
                    git_commit_tree(parentTree.out(), parent.get()) == 0) {
                    parentTreePtr = parentTree.get();
                }
            }

            // Default options match `git log` — no whitespace tweaks,
            // standard rename detection off (it costs another pass
            // and the shortstat numbers don't need it).
            git_diff_options dopts = GIT_DIFF_OPTIONS_INIT;
            DiffHandle diff;
            if (git_diff_tree_to_tree(diff.out(), repo.get(),
                                      parentTreePtr, thisTree.get(),
                                      &dopts) != 0) continue;

            git_diff_stats* statsPtr = nullptr;
            if (git_diff_get_stats(&statsPtr, diff.get()) != 0) continue;
            // Stats has no RAII wrapper yet; manual free at the end.
            info.filesChanged = static_cast<int>(git_diff_stats_files_changed(statsPtr));
            info.insertions   = static_cast<int>(git_diff_stats_insertions  (statsPtr));
            info.deletions    = static_cast<int>(git_diff_stats_deletions   (statsPtr));
            git_diff_stats_free(statsPtr);
        }
    }

    return GitResult::success();
}

GitResult GitHandler::fileDiff(const QString& localPath,
                               const QString& repoRelPath,
                               DiffScope      scope,
                               FileDiff&      out)
{
    out = {};
    out.path = repoRelPath;

    if (repoRelPath.isEmpty()) {
        return GitResult::failure(QStringLiteral("Empty path."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Pathspec restricts the diff to a single file. The QByteArray
    // backing storage must outlive the call.
    const QByteArray pathBytes = repoRelPath.toUtf8();
    const char* pathPtrs[] = { pathBytes.constData() };

    git_diff_options dopts = GIT_DIFF_OPTIONS_INIT;
    dopts.flags = GIT_DIFF_INCLUDE_UNTRACKED
                | GIT_DIFF_RECURSE_UNTRACKED_DIRS
                | GIT_DIFF_SHOW_UNTRACKED_CONTENT
                | GIT_DIFF_INCLUDE_TYPECHANGE;
    dopts.pathspec.strings = const_cast<char**>(pathPtrs);
    dopts.pathspec.count   = 1;
    dopts.context_lines    = 3;
    dopts.interhunk_lines  = 1;

    DiffHandle diff;

    // Decide how to populate the diff based on scope. For scopes that
    // need a HEAD tree, fall back gracefully when HEAD is unborn —
    // there's nothing to diff against, so we treat everything as new.
    bool unborn = false;
    TreeHandle headTree;
    {
        ReferenceHandle head;
        const int rc = git_repository_head(head.out(), repo.get());
        if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND) {
            unborn = true;
        } else if (rc != 0) {
            return lastError(QStringLiteral("Resolve HEAD"), rc);
        } else {
            ObjectHandle peeled;
            if (git_reference_peel(peeled.out(), head.get(), GIT_OBJECT_TREE) == 0) {
                // Move ownership: peel returns a generic git_object*
                // pointing at a tree; transfer it into headTree.
                headTree = TreeHandle(reinterpret_cast<git_tree*>(peeled.p));
                peeled.p = nullptr;
            } else {
                unborn = true;
            }
        }
    }

    int rc = 0;
    if (scope == DiffScope::HeadToIndex) {
        if (unborn) {
            // No HEAD → nothing staged that isn't fully "new". Show
            // index-to-workdir's "new file" entries instead.
            rc = git_diff_index_to_workdir(diff.out(), repo.get(), nullptr, &dopts);
        } else {
            rc = git_diff_tree_to_index(diff.out(), repo.get(), headTree.get(),
                                        nullptr, &dopts);
        }
    } else if (scope == DiffScope::IndexToWorkdir) {
        rc = git_diff_index_to_workdir(diff.out(), repo.get(), nullptr, &dopts);
    } else { // HeadToWorkdir (combined)
        if (unborn) {
            rc = git_diff_index_to_workdir(diff.out(), repo.get(), nullptr, &dopts);
        } else {
            rc = git_diff_tree_to_workdir_with_index(
                diff.out(), repo.get(), headTree.get(), &dopts);
        }
    }
    if (rc != 0) return lastError(QStringLiteral("Compute diff"), rc);

    // Iterate deltas; with our pathspec there's typically 0 or 1.
    const size_t nDeltas = git_diff_num_deltas(diff.get());
    if (nDeltas == 0) {
        // No diff for this file means it's unchanged in this scope.
        out.status = ' ';
        return GitResult::success();
    }

    // We only care about the first matching delta for our pathspec;
    // extractDelta populates the rest.
    extractDelta(diff.get(), 0, out);

    return GitResult::success();
}

GitResult GitHandler::commitDiff(const QString& localPath,
                                 const QString& sha,
                                 std::vector<FileDiff>& out)
{
    out.clear();
    if (sha.isEmpty()) {
        return GitResult::failure(QStringLiteral("Empty commit SHA."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Resolve the commit. We accept anything git_revparse_single takes
    // (full SHA, abbrev, even refs), but the UI only ever passes full
    // hex strings — we still go through revparse for convenience.
    ObjectHandle commitObj;
    {
        const QByteArray sb = sha.toUtf8();
        const int rc = git_revparse_single(commitObj.out(), repo.get(), sb.constData());
        if (rc != 0) {
            return lastError(QStringLiteral("Resolve commit %1").arg(sha), rc);
        }
    }

    CommitHandle commit;
    {
        const git_oid* oid = git_object_id(commitObj.get());
        if (git_commit_lookup(commit.out(), repo.get(), oid) != 0) {
            return lastError(QStringLiteral("Lookup commit %1").arg(sha), -1);
        }
    }

    // The commit's own tree.
    TreeHandle thisTree;
    if (git_commit_tree(thisTree.out(), commit.get()) != 0) {
        return lastError(QStringLiteral("Read commit tree"), -1);
    }

    // Parent tree, if any. For a root commit there is none — we pass
    // NULL to git_diff_tree_to_tree, which then returns "everything as
    // additions" relative to an empty baseline, exactly what `git show`
    // does for the first commit.
    TreeHandle parentTree;
    git_tree* parentTreePtr = nullptr;
    const unsigned int parentCount = git_commit_parentcount(commit.get());
    if (parentCount > 0) {
        // Use the first parent. For merge commits this matches `git show`
        // defaults; combined diff (-c) is intentionally out of scope.
        CommitHandle parent;
        if (git_commit_parent(parent.out(), commit.get(), 0) == 0) {
            if (git_commit_tree(parentTree.out(), parent.get()) != 0) {
                return lastError(QStringLiteral("Read parent tree"), -1);
            }
            parentTreePtr = parentTree.get();
        }
    }

    git_diff_options dopts = GIT_DIFF_OPTIONS_INIT;
    dopts.flags = GIT_DIFF_INCLUDE_TYPECHANGE;
    dopts.context_lines    = 3;
    dopts.interhunk_lines  = 1;

    DiffHandle diff;
    int rc = git_diff_tree_to_tree(diff.out(), repo.get(),
                                   parentTreePtr, thisTree.get(), &dopts);
    if (rc != 0) return lastError(QStringLiteral("Compute commit diff"), rc);

    // Detect renames/copies — `git show` does this by default, makes
    // diffs much more readable when files move.
    git_diff_find_options fopts = GIT_DIFF_FIND_OPTIONS_INIT;
    fopts.flags = GIT_DIFF_FIND_RENAMES | GIT_DIFF_FIND_COPIES;
    // Ignore the return value: rename detection is best-effort, an
    // error here just leaves the diff without rename annotations.
    (void)git_diff_find_similar(diff.get(), &fopts);

    const size_t nDeltas = git_diff_num_deltas(diff.get());
    out.reserve(nDeltas);
    for (size_t i = 0; i < nDeltas; ++i) {
        FileDiff fd;
        if (extractDelta(diff.get(), i, fd)) {
            out.push_back(std::move(fd));
        }
    }
    return GitResult::success();
}

GitResult GitHandler::commitDiffBetween(const QString& localPath,
                                        const QString& shaA,
                                        const QString& shaB,
                                        std::vector<FileDiff>& out)
{
    out.clear();
    if (shaA.isEmpty() && shaB.isEmpty()) {
        return GitResult::failure(QStringLiteral("Both commit SHAs are empty."));
    }

    RepoHandle repo;
    if (auto rc = openRepo(localPath, repo); !rc.ok) return rc;

    // Helper to resolve a SHA into its tree. Empty SHA means "no tree"
    // (caller wants every file in the other tree to look like a fresh
    // addition). Returns nullptr in *outTree on the empty-side path.
    auto resolveTree = [&](const QString& sha, TreeHandle& outTree) -> GitResult {
        if (sha.isEmpty()) return GitResult::success();
        ObjectHandle obj;
        const QByteArray sb = sha.toUtf8();
        const int rc = git_revparse_single(obj.out(), repo.get(), sb.constData());
        if (rc != 0) {
            return lastError(QStringLiteral("Resolve commit %1").arg(sha), rc);
        }
        CommitHandle commit;
        const git_oid* oid = git_object_id(obj.get());
        if (git_commit_lookup(commit.out(), repo.get(), oid) != 0) {
            return lastError(QStringLiteral("Lookup commit %1").arg(sha), -1);
        }
        if (git_commit_tree(outTree.out(), commit.get()) != 0) {
            return lastError(QStringLiteral("Read tree for %1").arg(sha), -1);
        }
        return GitResult::success();
    };

    TreeHandle treeA;
    if (auto rc = resolveTree(shaA, treeA); !rc.ok) return rc;
    TreeHandle treeB;
    if (auto rc = resolveTree(shaB, treeB); !rc.ok) return rc;

    git_diff_options dopts = GIT_DIFF_OPTIONS_INIT;
    dopts.flags = GIT_DIFF_INCLUDE_TYPECHANGE;
    dopts.context_lines    = 3;
    dopts.interhunk_lines  = 1;

    DiffHandle diff;
    int rc = git_diff_tree_to_tree(diff.out(), repo.get(),
                                   treeA.get(), treeB.get(), &dopts);
    if (rc != 0) return lastError(QStringLiteral("Diff between commits"), rc);

    // Same rename detection as commitDiff() — keeps cross-commit diffs
    // readable when files moved.
    git_diff_find_options fopts = GIT_DIFF_FIND_OPTIONS_INIT;
    fopts.flags = GIT_DIFF_FIND_RENAMES | GIT_DIFF_FIND_COPIES;
    (void)git_diff_find_similar(diff.get(), &fopts);

    const size_t nDeltas = git_diff_num_deltas(diff.get());
    out.reserve(nDeltas);
    for (size_t i = 0; i < nDeltas; ++i) {
        FileDiff fd;
        if (extractDelta(diff.get(), i, fd)) {
            out.push_back(std::move(fd));
        }
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
    if (rc != 0) {
        return rewriteSshAgentError(
            lastError(QStringLiteral("Fetch"), rc), ctx);
    }

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
    if (rc != 0) {
        return rewriteSshAgentError(
            lastError(QStringLiteral("Push"), rc), ctx);
    }

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
