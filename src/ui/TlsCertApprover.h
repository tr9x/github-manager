#pragma once

// TlsCertApprover — bridge between libgit2's certificate_check
// callback (running on the worker thread) and the GUI thread that
// owns the modal TlsCertApprovalDialog.
//
// Same pattern as HostKeyApprover, but for TLS X.509 certificates
// rather than SSH host keys. Used when a clone/fetch/push targets
// a server whose TLS cert isn't valid per system trust store:
// self-signed certs, internal CAs (GitHub Enterprise), or corporate
// MITM proxies.
//
// Threading: see HostKeyApprover.h for the rationale on
// BlockingQueuedConnection. Same reasoning applies here.
//
// Persistence: accepted certs are stored by fingerprint in
// QSettings under "TrustedTlsFingerprints/<host>=<sha256-hex>".
// The next connection to the same host with the same fingerprint
// proceeds silently. A different fingerprint at the same host
// re-prompts (could indicate cert rotation OR a MITM attempt;
// we let the user judge).

#include <QObject>
#include <QString>
#include <QByteArray>

class QWidget;

namespace ghm::core { class Settings; }

namespace ghm::ui {

class TlsCertApprover : public QObject {
    Q_OBJECT
public:
    explicit TlsCertApprover(QWidget* dialogParent,
                              ghm::core::Settings& settings,
                              QObject* parent = nullptr);
    ~TlsCertApprover() override;

    // Worker-thread side calls this. Pops the dialog on the GUI thread
    // and returns user choice. Returns true if the user approved
    // (one-time or permanent); false if rejected.
    //
    // Inputs:
    //   * host        — server hostname libgit2 connected to
    //   * derBytes    — raw DER-encoded X.509 cert (used to compute
    //                   SHA-256 fingerprint and parse fields for the
    //                   dialog)
    //
    // We do the fingerprint hash + cert parsing on the GUI thread
    // (cheap, ~ms) rather than on the worker, so the worker callback
    // stays small.
    Q_INVOKABLE bool requestApproval(const QString& host,
                                      const QByteArray& derBytes);

    // Check whether a cert with the given fingerprint is already
    // trusted for this host. Called from the libgit2 callback
    // BEFORE popping the dialog — if already trusted, no UI needed.
    // Safe to call from any thread (reads QSettings, which is
    // thread-safe).
    bool isFingerprintTrusted(const QString& host,
                                const QString& sha256Hex) const;

    // Singleton accessor — same pattern as HostKeyApprover.
    static TlsCertApprover* instance();

private:
    // Persist an approved fingerprint. Writes to QSettings.
    // Permanent vs one-time is decided by the dialog and reflected
    // here by whether we call this at all.
    void rememberFingerprint(const QString& host,
                              const QString& sha256Hex);

    QWidget*               dialogParent_;
    ghm::core::Settings&   settings_;
};

} // namespace ghm::ui
