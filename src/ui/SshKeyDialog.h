#pragma once

// SshKeyDialog — modal prompt for an SSH key path and (when needed)
// its passphrase. Shown by MainWindow in the GUI thread, BEFORE
// the worker is invoked, so the credential callback running in
// the worker has all the bytes ready and doesn't need to call back
// into the UI from off-thread.
//
// Flow:
//   1. Default key path is filled in (~/.ssh/id_ed25519 then id_rsa)
//   2. As user edits path, we re-inspect via SshKeyInfo and:
//      * Disable passphrase field when key is unencrypted
//      * Enable + require passphrase when encrypted
//   3. On accept(), the caller reads keyPath() and passphrase()
//      and stuffs them into CallbackCtx for the worker.
//
// Security:
//   * Passphrase is held in a QString inside this dialog and copied
//     once into a QByteArray on the controller's stack. We don't
//     persist or log it. After the worker finishes, the CallbackCtx
//     goes out of scope and the bytes are destroyed.
//   * The passphrase field has QLineEdit::Password echo mode.
//   * No timing-attack hardening — this is a one-shot prompt, not
//     a credential vault.

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;

namespace ghm::ui {

class SshKeyDialog : public QDialog {
    Q_OBJECT
public:
    explicit SshKeyDialog(QWidget* parent = nullptr);

    // Caller may pre-populate the key path; the field defaults to
    // ~/.ssh/id_ed25519 if this is empty.
    void setKeyPath(const QString& path);

    // Caller can set an optional contextual message shown above the
    // form. Used e.g. for "Using remembered key for this submodule…"
    // when re-prompting in the per-submodule key memory flow.
    void setMessage(const QString& message);

    QString keyPath()    const;
    QString passphrase() const;

    // True if the inspected key file appears to be encrypted —
    // helps the caller decide whether to even feed the passphrase
    // into the credential callback.
    bool    keyIsEncrypted() const;

private Q_SLOTS:
    void onBrowse();
    void onPathChanged(const QString& text);

private:
    void refreshKeyInfo();

    QLineEdit*   pathEdit_;
    QLineEdit*   passphraseEdit_;
    QLabel*      statusLabel_;
    QLabel*      messageLabel_{nullptr};  // optional contextual header
    QPushButton* okBtn_;
    QPushButton* cancelBtn_;

    bool        encrypted_{false};
};

} // namespace ghm::ui
