#pragma once

// LoginDialog - prompts the user for GitHub credentials.
//
// Two paths:
//   * OAuth device flow (preferred): user clicks "Sign in with GitHub",
//     a browser opens, they authorize the app, token comes back.
//     Only available when the build has GHM_OAUTH_CLIENT_ID defined.
//   * Personal Access Token (fallback): user pastes a PAT they
//     generated at github.com/settings/tokens.
//
// The dialog validates whichever credential the user provided
// against the GitHub API before accepting. On success it exposes
// the verified username (which may differ from what the user typed)
// and the token, ready for SecureStorage.

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;

namespace ghm::github { class GitHubClient; }
namespace ghm::session { class OAuthFlowController; }

namespace ghm::ui {

class OAuthLoginDialog;

class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget* parent = nullptr);
    ~LoginDialog() override;

    QString verifiedUsername() const { return verifiedUsername_; }
    QString token() const            { return token_; }

    // True if the accepted token came from OAuth (vs PAT). Useful
    // for SecureStorage metadata so we know which flow to retry
    // when the token eventually fails.
    bool    tokenIsOAuth() const     { return tokenIsOAuth_; }

private Q_SLOTS:
    void onSignIn();              // PAT validation
    void onSignInWithOAuth();     // start device flow
    void onAuthenticated(const QString& login);
    void onAuthFailed(const QString& reason);

    // OAuth flow plumbing.
    void onOAuthUserCodeReady(const QString& userCode,
                              const QString& verificationUri);
    void onOAuthStatusChanged(const QString& message);
    void onOAuthSignedIn(const QString& token, const QString& scope);
    void onOAuthFailed(const QString& message);
    void onOAuthCancelled();

private:
    void setBusy(bool busy);

    QLineEdit*    tokenEdit_;
    QPushButton*  signInBtn_;
    QPushButton*  oauthBtn_;
    QPushButton*  cancelBtn_;
    QLabel*       statusLabel_;
    QProgressBar* spinner_;

    ghm::github::GitHubClient*           client_;
    ghm::session::OAuthFlowController*   oauthCtrl_{nullptr};
    OAuthLoginDialog*                    oauthDialog_{nullptr};

    QString                    token_;
    QString                    verifiedUsername_;
    bool                       tokenIsOAuth_{false};
};

} // namespace ghm::ui
