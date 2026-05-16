#include "git/CommitSigner.h"

#include <QProcess>
#include <QTemporaryFile>
#include <QTextStream>
#include <QStandardPaths>

namespace ghm::git {

namespace {

// Cap subprocess wait at 30s. Long enough for slow pinentry + user
// typing a passphrase; short enough that a stuck process doesn't
// lock the worker thread forever.
constexpr int kSignerTimeoutMs = 30'000;

SignResult mkError(const QString& msg)
{
    SignResult r;
    r.ok    = false;
    r.error = msg;
    return r;
}

} // namespace

SignResult CommitSigner::signWithGpg(const QByteArray& commitBuffer,
                                      const QString& keyId)
{
    if (keyId.isEmpty()) {
        return mkError(
            QStringLiteral("GPG signing requires a key ID — "
                           "set one in Settings → Signing."));
    }
    // Locate gpg. Most distros have it in $PATH; we also accept gpg2
    // in case the host has it under that name (some older Debian).
    QString gpgPath = QStandardPaths::findExecutable(QStringLiteral("gpg"));
    if (gpgPath.isEmpty()) {
        gpgPath = QStandardPaths::findExecutable(QStringLiteral("gpg2"));
    }
    if (gpgPath.isEmpty()) {
        return mkError(QStringLiteral(
            "GPG is not installed (couldn't find gpg or gpg2 in PATH). "
            "Install it (Arch: `pacman -S gnupg`) or switch signing off "
            "in Settings."));
    }

    QProcess proc;
    // -bsau   detached, sign, ASCII armor, with user key
    // --status-fd=2  send machine-readable status to stderr (helps with
    //                error parsing; we just want it out of stdout)
    // --no-tty       don't try to grab a controlling TTY (we have none)
    // --batch        don't ask interactive questions
    //                — but passphrase via gpg-agent/pinentry is still
    //                allowed, that's how an encrypted key works
    QStringList args = {
        QStringLiteral("--status-fd=2"),
        QStringLiteral("-bsau"), keyId,
    };
    proc.start(gpgPath, args);
    if (!proc.waitForStarted(5000)) {
        return mkError(QStringLiteral(
            "Couldn't start gpg: %1").arg(proc.errorString()));
    }
    proc.write(commitBuffer);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(kSignerTimeoutMs)) {
        proc.kill();
        return mkError(QStringLiteral(
            "gpg timed out after 30s. If you have an encrypted key, make "
            "sure gpg-agent is running and pinentry can prompt for the "
            "passphrase (try `gpg --sign --output /dev/null /dev/null` "
            "in a terminal to test)."));
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QByteArray stderr_ = proc.readAllStandardError();
        return mkError(QStringLiteral(
            "gpg failed (exit code %1):\n%2").arg(proc.exitCode())
            .arg(QString::fromUtf8(stderr_).trimmed()));
    }
    const QByteArray sig = proc.readAllStandardOutput();
    if (sig.isEmpty()) {
        return mkError(QStringLiteral(
            "gpg returned an empty signature. Check the key ID is "
            "correct (Settings → Signing key)."));
    }

    SignResult r;
    r.ok        = true;
    r.signature = sig;
    return r;
}

SignResult CommitSigner::signWithSsh(const QByteArray& commitBuffer,
                                      const QString& keyPath)
{
    if (keyPath.isEmpty()) {
        return mkError(
            QStringLiteral("SSH signing requires a key path — "
                           "set one in Settings → Signing."));
    }
    const QString sshKeygen =
        QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
    if (sshKeygen.isEmpty()) {
        return mkError(QStringLiteral(
            "ssh-keygen is not installed (couldn't find it in PATH). "
            "It comes with OpenSSH — install openssh package."));
    }

    // ssh-keygen -Y sign needs the commit data in a file, not stdin.
    // We write it to a temp file, then delete after. QTemporaryFile
    // auto-deletes on destruction.
    QTemporaryFile bufFile;
    if (!bufFile.open()) {
        return mkError(QStringLiteral(
            "Couldn't create temp file for SSH signing."));
    }
    bufFile.write(commitBuffer);
    bufFile.flush();

    QProcess proc;
    // -Y sign       perform a sign operation
    // -f <key>      private key path (uses agent if key is encrypted)
    // -n git        namespace; git itself uses "git" so verify works
    // <bufFile>     positional: file to sign
    //
    // Output is the signature to stdout (when "-" is the file) or to
    // <file>.sig (when a real path). We want stdout, but ssh-keygen
    // doesn't have a "-O stdout" flag. Workaround: read the .sig
    // sidecar file ssh-keygen writes.
    const QStringList args = {
        QStringLiteral("-Y"), QStringLiteral("sign"),
        QStringLiteral("-f"), keyPath,
        QStringLiteral("-n"), QStringLiteral("git"),
        bufFile.fileName(),
    };
    proc.start(sshKeygen, args);
    if (!proc.waitForStarted(5000)) {
        return mkError(QStringLiteral(
            "Couldn't start ssh-keygen: %1").arg(proc.errorString()));
    }
    if (!proc.waitForFinished(kSignerTimeoutMs)) {
        proc.kill();
        return mkError(QStringLiteral(
            "ssh-keygen timed out after 30s. If your key is encrypted, "
            "make sure it's loaded into ssh-agent (`ssh-add %1`).")
            .arg(keyPath));
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QByteArray stderr_ = proc.readAllStandardError();
        return mkError(QStringLiteral(
            "ssh-keygen failed (exit code %1):\n%2").arg(proc.exitCode())
            .arg(QString::fromUtf8(stderr_).trimmed()));
    }

    // ssh-keygen wrote the signature to <bufFile>.sig
    const QString sigPath = bufFile.fileName() + QStringLiteral(".sig");
    QFile sigFile(sigPath);
    if (!sigFile.open(QIODevice::ReadOnly)) {
        return mkError(QStringLiteral(
            "Couldn't read SSH signature file at %1.").arg(sigPath));
    }
    const QByteArray sig = sigFile.readAll();
    sigFile.close();
    QFile::remove(sigPath);  // clean up sidecar — temp dir wouldn't catch it

    if (sig.isEmpty()) {
        return mkError(QStringLiteral(
            "ssh-keygen produced an empty signature."));
    }

    SignResult r;
    r.ok        = true;
    r.signature = sig;
    return r;
}

} // namespace ghm::git
