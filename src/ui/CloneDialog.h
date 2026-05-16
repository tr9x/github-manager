#pragma once

// CloneDialog - asks the user where to clone a selected GitHub repo,
// and (since 0.19.0) whether to use SSH or HTTPS for this particular
// clone. The caller can prefill the SSH default from settings, but
// the user can still flip it per-clone.
//
// This is presentation-only. The actual clone runs from MainWindow on
// accept() so we can stream progress into the main status bar instead
// of a transient modal.

#include <QDialog>
#include <QString>

#include "github/Repository.h"

class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;

namespace ghm::ui {

class CloneDialog : public QDialog {
    Q_OBJECT
public:
    CloneDialog(const ghm::github::Repository& repo,
                const QString& defaultParentDir,
                QWidget* parent = nullptr);
    ~CloneDialog() override;

    QString targetPath() const;

    // Whether the user wants this clone over SSH. The caller can
    // set the default (typically from Settings::clonePreferSsh()),
    // and the user can override before accepting.
    void setSshDefault(bool useSsh);
    bool useSsh() const;

    // Whether the user wants to point at a specific key file (with
    // its own passphrase if encrypted) instead of using ssh-agent.
    // Only meaningful when useSsh() is true. The host typically
    // pops a SshKeyDialog when this is true so the actual key path
    // and passphrase get collected.
    bool useExplicitKey() const;

private Q_SLOTS:
    void onBrowse();
    void onPathChanged(const QString& s);

private:
    ghm::github::Repository repo_;

    QLineEdit*   parentDirEdit_;
    QLineEdit*   folderNameEdit_;
    QLabel*      finalPathLabel_;
    QCheckBox*   sshCheckbox_;
    QCheckBox*   sshExplicitKeyCheckbox_;
    QPushButton* okBtn_;
    QPushButton* cancelBtn_;
};

} // namespace ghm::ui
