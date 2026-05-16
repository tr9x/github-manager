#pragma once

// HostKeyApprovalDialog — modal asking whether to trust an SSH host
// key fingerprint we've never seen before. Shown when libgit2's
// certificate_check callback fires for a host that isn't in
// ~/.ssh/known_hosts yet.
//
// The information presented mirrors what OpenSSH itself shows on
// first connection:
//   * Host name
//   * Fingerprint (SHA-256 in base64, what ssh-keygen -lf produces)
//   * Key type ("ssh-ed25519", "ssh-rsa", etc.)
//
// User outcome:
//   * "Trust" → returns Accepted. Caller writes a known_hosts entry
//     and proceeds with the clone.
//   * "Cancel" → returns Rejected. Caller aborts the clone.
//
// We deliberately do NOT offer "Trust forever" vs "Trust once" —
// known_hosts is the trust store and we always write there on accept.
// If the user changes their mind they can edit the file by hand.

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;

namespace ghm::ui {

class HostKeyApprovalDialog : public QDialog {
    Q_OBJECT
public:
    HostKeyApprovalDialog(const QString& host,
                          const QString& fingerprint,
                          const QString& keyType,
                          QWidget* parent = nullptr);

private:
    QLabel*      info_;
    QPushButton* trustBtn_;
    QPushButton* cancelBtn_;
};

} // namespace ghm::ui
