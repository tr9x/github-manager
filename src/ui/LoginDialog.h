#pragma once

// LoginDialog - prompts the user for a GitHub username + PAT.
//
// The dialog validates the token against the GitHub API before accepting.
// On success it exposes the verified username (which may differ from
// what the user typed) and the token, ready for SecureStorage.

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;

namespace ghm::github { class GitHubClient; }

namespace ghm::ui {

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget* parent = nullptr);
    ~LoginDialog() override;

    QString verifiedUsername() const { return verifiedUsername_; }
    QString token() const            { return token_; }

private Q_SLOTS:
    void onSignIn();
    void onAuthenticated(const QString& login);
    void onAuthFailed(const QString& reason);

private:
    void setBusy(bool busy);

    QLineEdit*    tokenEdit_;
    QPushButton*  signInBtn_;
    QPushButton*  cancelBtn_;
    QLabel*       statusLabel_;
    QProgressBar* spinner_;

    ghm::github::GitHubClient* client_;
    QString                    token_;
    QString                    verifiedUsername_;
};

} // namespace ghm::ui
