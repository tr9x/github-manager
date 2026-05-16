#pragma once

// SignatureVerifier — runs gpg/ssh-keygen to verify a detached commit
// signature against the commit's unsigned body bytes.
//
// Like CommitSigner, this spawns a subprocess per verification. That's
// expensive for a History tab with 200 commits, so the caller is
// expected to:
//   * verify lazily (only commits currently in the viewport), and
//   * cache results keyed by commit SHA (verification is deterministic
//     for a given commit + key state, so caching is safe within a
//     session).
//
// Verification outcomes:
//   * Verified  — signature valid + key trusted (GPG: ultimate/full
//                 trust; SSH: present in allowed_signers)
//   * Signed    — signature valid but key trust unknown (GPG: marginal/
//                 unknown trust; SSH: no allowed_signers file)
//   * Invalid   — signature does not match commit body, OR key was
//                 revoked/expired, OR the subprocess failed for any
//                 other reason. The error string carries the details.
//   * Unsigned  — commit has no gpgsig header at all
//
// Threading: same as CommitSigner — no shared state, safe to call
// concurrently from different threads, expected to run on the worker
// thread.

#include <QString>
#include <QByteArray>

namespace ghm::git {

struct VerifyResult {
    enum class Status {
        Unsigned,   // no gpgsig header
        Verified,   // signature valid + key trusted
        Signed,     // signature valid, key trust unknown
        Invalid,    // signature mismatch / revoked / expired / etc.
    };

    Status   status{Status::Unsigned};
    QString  signer;       // human-readable identity ("Alice <a@b>")
    QString  keyId;        // GPG key ID or SSH key fingerprint
    QString  error;        // populated when status == Invalid
};

class SignatureVerifier {
public:
    // Detect signature kind from the header bytes. We need this to
    // route between gpg and ssh-keygen — the same "gpgsig" commit
    // header carries either format. Returns true with `isSsh` set
    // when the signature is recognised; false otherwise (in which
    // case we treat the commit as Invalid with "unknown format").
    static bool detectFormat(const QByteArray& signature, bool* isSsh);

    // GPG verify:
    //   gpg --verify --status-fd=2 <sig-file> <data-file>
    // We use status-fd output to determine the trust level
    // (VALIDSIG vs GOODSIG vs BADSIG vs NO_PUBKEY).
    static VerifyResult verifyGpg(const QByteArray& signature,
                                   const QByteArray& signedData);

    // SSH verify:
    //   ssh-keygen -Y verify -f allowed_signers -I <email>
    //                        -n git -s <sig-file> < data
    // Without an allowed_signers file we can't determine trust,
    // so SSH-signed commits get Status::Signed (not Verified)
    // unless the caller provides one.
    //
    // `committerEmail` comes from the commit author/committer
    // field — used as the principal identity for verify.
    static VerifyResult verifySsh(const QByteArray& signature,
                                   const QByteArray& signedData,
                                   const QString&    committerEmail,
                                   const QString&    allowedSignersPath);
};

} // namespace ghm::git
