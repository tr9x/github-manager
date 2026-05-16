#pragma once

// CommitSigner — produces an ASCII-armored detached signature for a
// commit buffer, ready to be passed to git_commit_create_with_signature.
//
// libgit2 doesn't do signing itself — it gives us the unsigned commit
// bytes, and we have to spawn an external signer (gpg or ssh-keygen)
// and capture its output. This class encapsulates that subprocess
// dance with its own error reporting, so the handler code that uses
// it stays clean.
//
// The signer runs synchronously on whatever thread calls it — for
// our purposes that's the worker thread (GitHandler::commit runs
// there). Subprocess wait can take 100-500ms with no agent and
// considerably longer if pinentry/passphrase prompts the user.
// That's OK for a commit op, which is already a deliberate user
// action.
//
// Failure modes (each maps to a SignResult with `ok=false`):
//   * Executable not found (gpg/ssh-keygen missing)
//   * Subprocess exited non-zero
//   * Subprocess produced empty output
//   * Subprocess timed out (we use 30s — passphrase prompts have
//     plenty of time, but a hung pinentry won't lock the GUI forever)
//
// Threading: this class has no shared state. Two threads can call
// sign() concurrently and they won't interfere — each spawns its
// own QProcess. We do NOT call this from the GUI thread.

#include <QString>
#include <QByteArray>

namespace ghm::git {

struct SignResult {
    bool       ok{false};
    QString    error;        // human-readable; surfaced through GitResult
    QByteArray signature;    // ASCII-armored, ready for libgit2
};

class CommitSigner {
public:
    // Sign `commitBuffer` (the bytes git_commit_create_buffer
    // produced) using GPG with the given key ID. The output is a
    // detached ASCII-armored signature ("-----BEGIN PGP SIGNATURE-----"
    // … "-----END PGP SIGNATURE-----"), suitable for the gpgsig
    // header in a commit object.
    //
    // Equivalent to:
    //   gpg --status-fd=2 -bsau <keyId>
    //
    // gpg may pop its own pinentry for an encrypted key — that's
    // expected and we don't try to suppress it. If the user has no
    // pinentry configured (server box, headless), the call fails
    // and our error message says to set up gpg-agent.
    static SignResult signWithGpg(const QByteArray& commitBuffer,
                                   const QString& keyId);

    // Sign with SSH (git 2.34+ style). Uses `ssh-keygen -Y sign`.
    // keyPath is the private key file (e.g. ~/.ssh/id_ed25519);
    // ssh-keygen will use the corresponding agent identity if the
    // key has a passphrase and the agent has it loaded.
    //
    // Equivalent to:
    //   ssh-keygen -Y sign -f <keyPath> -n git < commitBuffer
    //
    // The "-n git" namespace is what git itself uses for commit
    // signatures (so e.g. ssh-keygen -Y verify accepts them).
    static SignResult signWithSsh(const QByteArray& commitBuffer,
                                   const QString& keyPath);
};

} // namespace ghm::git
