#pragma once

// TlsCertApprovalDialog — modal that shows TLS X.509 certificate
// details (subject, issuer, valid range, fingerprints) and lets the
// user accept the cert once or permanently.
//
// Surfaces when libgit2 (or our QNAM client) hits a TLS cert that
// the system trust store rejected. Same affordance as your browser's
// "Your connection is not private" → "Advanced → Proceed" screen,
// but scoped to this app's connections only.
//
// Three outcomes the user can pick:
//   * Reject       — connection aborts (default if user just hits Esc)
//   * Accept once  — proceed THIS time; next clone re-prompts
//   * Accept always — proceed AND persist the fingerprint in Settings
//
// `result()` after exec() distinguishes these. Permanent accepts get
// the fingerprint stored by the caller (TlsCertApprover); the dialog
// itself doesn't touch Settings.

#include <QDialog>
#include <QString>
#include <QByteArray>

class QLabel;
class QPushButton;

namespace ghm::ui {

class TlsCertApprovalDialog : public QDialog {
    Q_OBJECT
public:
    // The dialog parses the DER bytes itself to display subject,
    // issuer, validity, and to compute SHA-256/SHA-1 fingerprints.
    // Parsing failures (corrupt DER) are surfaced in the dialog —
    // the user can still accept if they explicitly want to.
    explicit TlsCertApprovalDialog(const QString& host,
                                    const QByteArray& derBytes,
                                    QWidget* parent = nullptr);

    enum Outcome {
        Reject = 0,
        AcceptOnce,
        AcceptAlways,
    };

    // Available after exec() returns. Reject if the user closed the
    // dialog or hit the Reject button.
    Outcome outcome() const { return outcome_; }

    // SHA-256 fingerprint as lowercase hex, computed from the DER
    // bytes. Available after construction; the caller uses this to
    // persist trust on AcceptAlways.
    QString sha256Hex() const { return sha256Hex_; }

private Q_SLOTS:
    void onAcceptOnce();
    void onAcceptAlways();
    void onReject();

private:
    void buildUi(const QString& host);
    void parseCertificate(const QByteArray& der);

    Outcome      outcome_{Reject};
    QString      sha256Hex_;
    QString      sha1Hex_;
    QString      subject_;
    QString      issuer_;
    QString      validFrom_;
    QString      validUntil_;
    bool         parseOk_{false};
    QString      parseError_;
};

} // namespace ghm::ui
