#pragma once

// SigningSettingsDialog — lets the user pick commit signing mode and
// key. Three modes:
//   * None  → unsigned commits (default)
//   * GPG   → enter a GPG key ID (e.g. "0x1234ABCD" or full fingerprint)
//   * SSH   → pick an SSH private key file
//
// The dialog is settings-only — it doesn't validate the key or attempt
// a test signature. Validation happens lazily at commit time, where
// the error (gpg not installed, key not found, agent missing) gets
// surfaced to the user with a specific message from CommitSigner.
//
// We could pre-test by signing a dummy buffer when the user clicks OK,
// but that costs a subprocess and may pop the user's pinentry — too
// intrusive for a settings change. Better to let real commits fail
// with a clear message.

#include <QDialog>
#include "core/Settings.h"

class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QStackedWidget;

namespace ghm::ui {

class SigningSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SigningSettingsDialog(ghm::core::Settings& settings,
                                    QWidget* parent = nullptr);

private Q_SLOTS:
    void onModeChanged(int index);
    void onBrowseSshKey();
    void onImportFromGitConfig();
    void onAccepted();

private:
    void loadFromSettings();

    ghm::core::Settings& settings_;

    QComboBox*       modeCombo_;
    QStackedWidget*  modePages_;     // None / GPG / SSH detail pages
    QLineEdit*       gpgKeyEdit_;
    QLineEdit*       sshKeyPathEdit_;
    QPushButton*     browseBtn_;
    QPushButton*     importBtn_;
    QPushButton*     okBtn_;
    QPushButton*     cancelBtn_;
};

} // namespace ghm::ui
