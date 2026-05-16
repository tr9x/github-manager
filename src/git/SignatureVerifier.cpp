#include "git/SignatureVerifier.h"

#include <QProcess>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QFile>

namespace ghm::git {

namespace {

constexpr int kVerifyTimeoutMs = 15'000;  // 15s — verify is faster than sign

VerifyResult mkError(const QString& msg)
{
    VerifyResult r;
    r.status = VerifyResult::Status::Invalid;
    r.error  = msg;
    return r;
}

// Write bytes to a temp file, return the path. The QTemporaryFile is
// returned by value (move) so the caller can keep it alive while the
// path is used.
bool writeTemp(QTemporaryFile& f, const QByteArray& bytes)
{
    if (!f.open()) return false;
    f.write(bytes);
    f.flush();
    f.close();  // close so other processes can read it
    return true;
}

} // namespace

bool SignatureVerifier::detectFormat(const QByteArray& signature, bool* isSsh)
{
    // First non-whitespace bytes determine format. PGP starts with
    // "-----BEGIN PGP SIGNATURE-----"; SSH with "-----BEGIN SSH
    // SIGNATURE-----". The "BEGIN" line is canonical even for X.509
    // signatures (which start with CERTIFICATE).
    const QByteArray trimmed = signature.trimmed();
    if (trimmed.startsWith("-----BEGIN PGP SIGNATURE-----")) {
        if (isSsh) *isSsh = false;
        return true;
    }
    if (trimmed.startsWith("-----BEGIN SSH SIGNATURE-----")) {
        if (isSsh) *isSsh = true;
        return true;
    }
    return false;
}

VerifyResult SignatureVerifier::verifyGpg(const QByteArray& signature,
                                           const QByteArray& signedData)
{
    QString gpgPath = QStandardPaths::findExecutable(QStringLiteral("gpg"));
    if (gpgPath.isEmpty()) {
        gpgPath = QStandardPaths::findExecutable(QStringLiteral("gpg2"));
    }
    if (gpgPath.isEmpty()) {
        return mkError(QStringLiteral(
            "gpg not installed — can't verify GPG signatures."));
    }

    // Write the signature and the signed data to temp files.
    // gpg --verify wants two file paths (sig + data) or sig on stdin
    // and data as the only arg. Two files is simpler.
    QTemporaryFile sigFile, dataFile;
    if (!writeTemp(sigFile, signature) || !writeTemp(dataFile, signedData)) {
        return mkError(QStringLiteral(
            "Couldn't create temp files for GPG verify."));
    }

    QProcess proc;
    const QStringList args = {
        QStringLiteral("--verify"),
        QStringLiteral("--status-fd=2"),  // machine-readable on stderr
        sigFile.fileName(),
        dataFile.fileName(),
    };
    proc.start(gpgPath, args);
    if (!proc.waitForStarted(5000)) {
        return mkError(QStringLiteral(
            "Couldn't start gpg: %1").arg(proc.errorString()));
    }
    if (!proc.waitForFinished(kVerifyTimeoutMs)) {
        proc.kill();
        return mkError(QStringLiteral(
            "gpg verify timed out after 15s."));
    }

    // Parse gpg's --status-fd=2 output. Lines we care about:
    //   [GNUPG:] GOODSIG <keyid> <name>       → signature matches, key known
    //   [GNUPG:] VALIDSIG <fingerprint> ...   → also signature matches
    //   [GNUPG:] TRUST_ULTIMATE / TRUST_FULLY → key is trusted
    //   [GNUPG:] TRUST_MARGINAL / TRUST_UNDEFINED → key signed but trust unknown
    //   [GNUPG:] BADSIG <keyid> <name>        → signature does NOT match
    //   [GNUPG:] NO_PUBKEY <keyid>            → key not in keyring
    //   [GNUPG:] EXPSIG ... / REVKEYSIG ...   → expired/revoked
    //
    // Exit code: 0 = good signature (any trust), 1 = bad, 2 = error
    // (missing key, etc.) — but we still parse status for detail.
    const QByteArray stderrBytes = proc.readAllStandardError();
    const QString    statusText  = QString::fromUtf8(stderrBytes);

    VerifyResult r;

    // Look for the strongest verdict first. Order matters here:
    // BADSIG should override GOODSIG if both appear (shouldn't happen
    // but be defensive).
    bool sawBad      = statusText.contains(QStringLiteral("[GNUPG:] BADSIG "));
    bool sawRev      = statusText.contains(QStringLiteral("[GNUPG:] REVKEYSIG ")) ||
                       statusText.contains(QStringLiteral("[GNUPG:] EXPKEYSIG "));
    bool sawNoKey    = statusText.contains(QStringLiteral("[GNUPG:] NO_PUBKEY "));
    bool sawGood     = statusText.contains(QStringLiteral("[GNUPG:] GOODSIG ")) ||
                       statusText.contains(QStringLiteral("[GNUPG:] VALIDSIG "));
    bool sawTrusted  = statusText.contains(QStringLiteral("[GNUPG:] TRUST_ULTIMATE")) ||
                       statusText.contains(QStringLiteral("[GNUPG:] TRUST_FULLY"));

    // Extract keyId and signer from GOODSIG line if present.
    // Format: "[GNUPG:] GOODSIG <keyid> <userid>"
    const int goodIdx = statusText.indexOf(QStringLiteral("[GNUPG:] GOODSIG "));
    if (goodIdx >= 0) {
        const int lineEnd = statusText.indexOf('\n', goodIdx);
        const QString line = statusText.mid(goodIdx,
            lineEnd < 0 ? -1 : (lineEnd - goodIdx));
        const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() >= 3) {
            r.keyId  = parts[2];
            r.signer = parts.mid(3).join(' ');
        }
    }

    if (sawBad) {
        r.status = VerifyResult::Status::Invalid;
        r.error  = QStringLiteral("Bad signature");
    } else if (sawRev) {
        r.status = VerifyResult::Status::Invalid;
        r.error  = QStringLiteral("Signing key revoked or expired");
    } else if (sawNoKey) {
        r.status = VerifyResult::Status::Invalid;
        r.error  = QStringLiteral("Signing key not in keyring");
    } else if (sawGood) {
        r.status = sawTrusted
            ? VerifyResult::Status::Verified
            : VerifyResult::Status::Signed;
    } else {
        // No verdict line — treat as Invalid with the raw output as
        // diagnostic. Shouldn't happen with a well-formed signature.
        r.status = VerifyResult::Status::Invalid;
        r.error  = QStringLiteral("gpg gave no verdict (exit %1)")
            .arg(proc.exitCode());
    }

    return r;
}

VerifyResult SignatureVerifier::verifySsh(const QByteArray& signature,
                                           const QByteArray& signedData,
                                           const QString&    committerEmail,
                                           const QString&    allowedSignersPath)
{
    const QString sshKeygen =
        QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
    if (sshKeygen.isEmpty()) {
        return mkError(QStringLiteral(
            "ssh-keygen not installed — can't verify SSH signatures."));
    }

    // SSH verify needs an allowed_signers file to bind identity →
    // public key. Without it, the most we can say is "signature is
    // mathematically valid" but not "signed by Alice". The verify
    // call needs the file path even if we treat the result loosely.
    QString allowedFile = allowedSignersPath;
    if (allowedFile.isEmpty() || !QFile::exists(allowedFile)) {
        // No allowed_signers configured → we can still check the
        // signature is well-formed by attempting a check-novalidate
        // (ssh-keygen -Y check-novalidate). That returns 0 if the
        // signature is parseable and matches the data, regardless
        // of trust.
        QTemporaryFile sigFile;
        if (!writeTemp(sigFile, signature)) {
            return mkError(QStringLiteral(
                "Couldn't create temp file for SSH verify."));
        }
        QProcess proc;
        const QStringList args = {
            QStringLiteral("-Y"), QStringLiteral("check-novalidate"),
            QStringLiteral("-n"), QStringLiteral("git"),
            QStringLiteral("-s"), sigFile.fileName(),
        };
        proc.start(sshKeygen, args);
        if (!proc.waitForStarted(5000)) {
            return mkError(QStringLiteral(
                "Couldn't start ssh-keygen: %1").arg(proc.errorString()));
        }
        proc.write(signedData);
        proc.closeWriteChannel();
        if (!proc.waitForFinished(kVerifyTimeoutMs)) {
            proc.kill();
            return mkError(QStringLiteral(
                "ssh-keygen verify timed out after 15s."));
        }
        VerifyResult r;
        if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
            r.status = VerifyResult::Status::Signed;  // valid sig, no trust
        } else {
            r.status = VerifyResult::Status::Invalid;
            r.error  = QString::fromUtf8(
                proc.readAllStandardError()).trimmed();
        }
        return r;
    }

    // With allowed_signers — full identity verify.
    QTemporaryFile sigFile;
    if (!writeTemp(sigFile, signature)) {
        return mkError(QStringLiteral(
            "Couldn't create temp file for SSH verify."));
    }
    QProcess proc;
    const QStringList args = {
        QStringLiteral("-Y"), QStringLiteral("verify"),
        QStringLiteral("-f"), allowedFile,
        QStringLiteral("-I"), committerEmail,
        QStringLiteral("-n"), QStringLiteral("git"),
        QStringLiteral("-s"), sigFile.fileName(),
    };
    proc.start(sshKeygen, args);
    if (!proc.waitForStarted(5000)) {
        return mkError(QStringLiteral(
            "Couldn't start ssh-keygen: %1").arg(proc.errorString()));
    }
    proc.write(signedData);
    proc.closeWriteChannel();
    if (!proc.waitForFinished(kVerifyTimeoutMs)) {
        proc.kill();
        return mkError(QStringLiteral(
            "ssh-keygen verify timed out after 15s."));
    }
    VerifyResult r;
    if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        r.status = VerifyResult::Status::Verified;
        r.signer = committerEmail;
    } else {
        r.status = VerifyResult::Status::Invalid;
        r.error  = QString::fromUtf8(
            proc.readAllStandardError()).trimmed();
    }
    return r;
}

} // namespace ghm::git
