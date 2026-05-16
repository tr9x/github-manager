#pragma once

// AddSubmoduleDialog — prompts for URL + path + (optionally) explicit
// SSH key when adding a new submodule.
//
// Flow:
//   1. User enters the submodule URL and the target path within the
//      parent repo (e.g. "vendor/foo")
//   2. If the URL is SSH (git@host:owner/repo or ssh://…), an
//      "Explicit key" checkbox appears. Default checked = false →
//      use ssh-agent.
//   3. On accept, the host calls worker_->addSubmodule with the
//      collected fields. The dialog itself does no git work.
//
// We don't try to validate the URL is reachable before submitting —
// that requires network and slows the dialog. If the URL is wrong,
// the worker errors out and the user sees the message.

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;

namespace ghm::ui {

class AddSubmoduleDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddSubmoduleDialog(QWidget* parent = nullptr);

    QString url()        const;
    QString subPath()    const;

    // True when the user wants to provide a specific key file for
    // this clone (instead of ssh-agent). The host should then pop a
    // SshKeyDialog after this one accepts.
    bool    useExplicitKey() const;

private Q_SLOTS:
    void onUrlChanged(const QString& url);
    void onSubPathChanged(const QString& path);

private:
    void updateOkEnabled();

    QLineEdit*   urlEdit_;
    QLineEdit*   pathEdit_;
    QLabel*      hintLabel_;
    QCheckBox*   explicitKeyCheckbox_;
    QPushButton* okBtn_;
    QPushButton* cancelBtn_;
};

} // namespace ghm::ui
