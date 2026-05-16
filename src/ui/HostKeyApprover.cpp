#include "ui/HostKeyApprover.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>

#include "ui/HostKeyApprovalDialog.h"

namespace ghm::ui {

namespace {
// Global singleton pointer. See class docstring for why this is safe.
// We avoid std::atomic because the only writers are constructor and
// destructor in the GUI thread; readers (libgit2 callbacks) come
// strictly after construction completes and before destruction starts.
HostKeyApprover* g_instance = nullptr;

// Append a hashed known_hosts line for `host` with the given raw key.
// Returns true on success; on failure (file unwritable, malformed
// inputs) we silently skip the write — the user will be prompted
// again next time. We don't surface this as an error because the
// clone itself can still succeed; the persistence is a convenience.
bool appendKnownHosts(const QString& host,
                      const QString& keyType,
                      const QString& rawKeyBase64)
{
    if (host.isEmpty() || keyType.isEmpty() || rawKeyBase64.isEmpty()) {
        return false;
    }

    const QString sshDir = QDir::homePath() + QStringLiteral("/.ssh");
    if (!QDir().mkpath(sshDir)) return false;

    const QString khPath = sshDir + QStringLiteral("/known_hosts");

    // OpenSSH HashKnownHosts format:
    //   |1|<base64-salt>|<base64-sha1-hmac(salt, hostname)> <keytype> <key>
    //
    // Salt is 20 random bytes (size of SHA1 output). HMAC-SHA1 is
    // computed with the salt as the key and the lowercased hostname
    // (no port) as the message. ssh strips any ":port" suffix before
    // hashing — we do the same.
    QByteArray salt(20, 0);
    auto* rng = QRandomGenerator::system();
    for (int i = 0; i < salt.size(); ++i) {
        salt[i] = static_cast<char>(rng->bounded(256));
    }

    // Strip ":port" if present — known_hosts hashes the bare host.
    QString hostBare = host;
    const int colon = hostBare.lastIndexOf(QLatin1Char(':'));
    if (colon > 0) hostBare.truncate(colon);

    const QByteArray hashed = QMessageAuthenticationCode::hash(
        hostBare.toUtf8(), salt, QCryptographicHash::Sha1);

    const QString line = QStringLiteral("|1|%1|%2 %3 %4\n")
        .arg(QString::fromLatin1(salt.toBase64()))
        .arg(QString::fromLatin1(hashed.toBase64()))
        .arg(keyType)
        .arg(rawKeyBase64);

    QFile f(khPath);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return false;
    f.write(line.toUtf8());
    return true;
}

} // namespace

HostKeyApprover* HostKeyApprover::instance()
{
    return g_instance;
}

HostKeyApprover::HostKeyApprover(QWidget* dialogParent, QObject* parent)
    : QObject(parent)
    , dialogParent_(dialogParent)
{
    // We assume there's only ever one MainWindow, and thus only one
    // approver. Asserting on duplicate construction would catch a
    // future split into multi-window code that doesn't account for
    // the global.
    Q_ASSERT(g_instance == nullptr);
    g_instance = this;
}

HostKeyApprover::~HostKeyApprover()
{
    if (g_instance == this) g_instance = nullptr;
}

bool HostKeyApprover::requestApproval(const QString& host,
                                       const QString& fingerprint,
                                       const QString& keyType,
                                       const QString& rawKeyBase64)
{
    // We're now on the GUI thread (invoked via BlockingQueuedConnection
    // from the worker), so it's safe to show a modal dialog.
    HostKeyApprovalDialog dlg(host, fingerprint, keyType, dialogParent_);
    const bool accepted = (dlg.exec() == QDialog::Accepted);

    if (accepted) {
        // Best-effort persistence. Even if we can't write to
        // known_hosts (read-only filesystem, permission denied, …),
        // the user's "trust this one" choice still applies to the
        // current clone — libgit2 proceeds because we returned true.
        // Next clone will prompt again, which is annoying but not
        // worse than today.
        appendKnownHosts(host, keyType, rawKeyBase64);
    }
    return accepted;
}

} // namespace ghm::ui
