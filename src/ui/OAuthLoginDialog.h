#pragma once

// OAuthLoginDialog — UI for the GitHub device-flow login.
//
// Shows:
//   * The user code (large, monospace, easy to read aloud or type)
//   * A "Copy code" button so it goes onto the clipboard
//   * A "Open GitHub" button that fires QDesktopServices::openUrl
//     with the verification URI
//   * A status message that updates from the controller as polling
//     progresses ("Waiting…", "Polling slowed down…", etc.)
//   * A Cancel button that emits cancelled() and closes the dialog
//
// The dialog is "dumb" — it owns no state beyond what's shown. The
// OAuthFlowController drives the state machine and pushes updates
// via setUserCode() / setStatus().

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;

namespace ghm::ui {

class OAuthLoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit OAuthLoginDialog(QWidget* parent = nullptr);

    // Called by the host once the controller emits userCodeReady.
    // Populates the on-screen code + activates the "Open GitHub"
    // button. Both inputs come straight from the device-code response.
    void setUserCode(const QString& code, const QString& verificationUri);

    // Called by the host on every controller statusChanged().
    // Shown in a small label below the code.
    void setStatus(const QString& message);

Q_SIGNALS:
    // User clicked Cancel (or closed via window manager). Host should
    // call controller.cancel() and dismiss the dialog.
    void cancelled();

private Q_SLOTS:
    void onCopyCode();
    void onOpenBrowser();

private:
    QLabel*      codeLabel_;
    QLabel*      statusLabel_;
    QPushButton* copyBtn_;
    QPushButton* openBtn_;
    QPushButton* cancelBtn_;

    QString verificationUri_;
};

} // namespace ghm::ui
