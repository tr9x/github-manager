#pragma once

// AddRemoteDialog - lets the user attach a remote to a local repo.
//
// The "smart paste" field accepts either of:
//
//   - A full GitHub-style command:
//       git remote add origin https://github.com/foo/bar.git
//       git remote add upstream git@github.com:foo/bar.git
//
//   - A bare URL:
//       https://github.com/foo/bar.git
//       git@github.com:foo/bar.git
//
// On accept(), name() and url() are populated. SSH URLs are flagged
// because libgit2's PAT auth path won't pick them up — we surface a
// warning rather than failing later in the push.

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QDialogButtonBox;
class QCheckBox;

namespace ghm::ui {

class AddRemoteDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddRemoteDialog(const QString& suggestedBranch,
                             QWidget* parent = nullptr);

    QString name() const;
    QString url()  const;
    bool    setUpstreamOnPush() const;

private Q_SLOTS:
    void onPasteChanged(const QString& text);
    void onAccept();

private:
    void updateWarning();

    QLineEdit* pasteEdit_;
    QLineEdit* nameEdit_;
    QLineEdit* urlEdit_;
    QLabel*    warningLabel_;
    QCheckBox* setUpstreamBox_;
    QDialogButtonBox* buttons_;

    QString suggestedBranch_;
};

} // namespace ghm::ui
