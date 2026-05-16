#include "ui/TlsCertApprover.h"
#include "ui/TlsCertApprovalDialog.h"
#include "core/Settings.h"

#include <QCryptographicHash>
#include <QPointer>

namespace ghm::ui {

namespace {
// Singleton pointer set in ctor, cleared in dtor. Read by worker
// threads through TlsCertApprover::instance(). Same pattern as
// HostKeyApprover — no synchronisation needed because writes happen
// only at GUI-thread init/shutdown when worker isn't running.
TlsCertApprover* g_instance = nullptr;
} // anonymous namespace

TlsCertApprover::TlsCertApprover(QWidget* dialogParent,
                                   ghm::core::Settings& settings,
                                   QObject* parent)
    : QObject(parent)
    , dialogParent_(dialogParent)
    , settings_(settings)
{
    g_instance = this;
}

TlsCertApprover::~TlsCertApprover()
{
    if (g_instance == this) g_instance = nullptr;
}

TlsCertApprover* TlsCertApprover::instance()
{
    return g_instance;
}

bool TlsCertApprover::isFingerprintTrusted(const QString& host,
                                              const QString& sha256Hex) const
{
    if (host.isEmpty() || sha256Hex.isEmpty()) return false;
    const QString saved = settings_.trustedTlsFingerprint(host);
    return !saved.isEmpty() && saved.compare(sha256Hex,
                                              Qt::CaseInsensitive) == 0;
}

void TlsCertApprover::rememberFingerprint(const QString& host,
                                            const QString& sha256Hex)
{
    settings_.setTrustedTlsFingerprint(host, sha256Hex);
}

bool TlsCertApprover::requestApproval(const QString& host,
                                        const QByteArray& derBytes)
{
    // Compute fingerprint up front — needed for the
    // already-trusted check AND for the persistence step on
    // accept-always. We could compute it twice (once here, once
    // in the dialog) but doing it once is cheaper.
    const QString sha256Hex = QString::fromLatin1(
        QCryptographicHash::hash(derBytes, QCryptographicHash::Sha256)
            .toHex().toLower());

    // First: check if this exact fingerprint was previously approved.
    // This SHOULD already have been checked in the worker callback
    // (we want to avoid the QMetaObject::invokeMethod round-trip
    // when not needed) but we check again here as a defensive
    // double-check.
    if (isFingerprintTrusted(host, sha256Hex)) {
        return true;
    }

    // Pop the modal. We're guaranteed to be on the GUI thread here
    // (invokeMethod with BlockingQueuedConnection ensures that).
    TlsCertApprovalDialog dlg(host, derBytes, dialogParent_);
    const int result = dlg.exec();

    if (result != QDialog::Accepted) {
        // Reject button or Esc — don't approve.
        return false;
    }

    if (dlg.outcome() == TlsCertApprovalDialog::AcceptAlways) {
        rememberFingerprint(host, sha256Hex);
    }
    // AcceptOnce: proceed but don't remember. The fingerprint is
    // approved for this connection only.
    return true;
}

} // namespace ghm::ui
