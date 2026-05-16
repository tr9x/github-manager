#include "session/SessionController.h"

#include "core/SecureStorage.h"
#include "core/Settings.h"
#include "github/GitHubClient.h"
#include "ui/LoginDialog.h"

#include <QString>
#include <QDialog>

namespace ghm::session {

SessionController::SessionController(ghm::core::SecureStorage& storage,
                                     ghm::core::Settings&      settings,
                                     QObject*                  parent)
    : QObject(parent)
    , storage_(storage)
    , settings_(settings)
    , client_(new ghm::github::GitHubClient(this))
{
    // Wire up the underlying GitHubClient. We translate its low-level
    // signals into our higher-level signedIn/authError/etc. so the
    // host doesn't have to know two vocabularies.
    connect(client_, &ghm::github::GitHubClient::authenticated,
            this, [this](const QString& login) {
                onClientUserValidated(true, login, QString());
            });
    connect(client_, &ghm::github::GitHubClient::authenticationFailed,
            this, [this](const QString& reason) {
                onClientUserValidated(false, QString(), reason);
            });
    connect(client_, &ghm::github::GitHubClient::repositoriesReady,
            this, [this](const QList<ghm::github::Repository>& repos) {
                onClientReposReceived(true, repos, QString());
            });
    connect(client_, &ghm::github::GitHubClient::networkError,
            this, [this](const QString& message) {
                onClientReposReceived(false, {}, message);
            });
}

void SessionController::tryRestoreSession()
{
    const QString lastUser = settings_.lastUsername();
    if (lastUser.isEmpty()) {
        Q_EMIT signedOut();
        return;
    }

    QString err;
    auto saved = storage_.loadToken(lastUser, &err);
    if (!saved) {
        if (!err.isEmpty()) {
            // Keyring is wedged (D-Bus down, no keyring service, locked
            // wallet, etc.) — surface it but stay usable; the rest of
            // the app degrades gracefully without a session.
            Q_EMIT keyringError(err);
        }
        Q_EMIT signedOut();
        return;
    }

    token_       = saved->token;
    tokenType_   = saved->type;
    username_    = lastUser;
    restoring_   = true;
    client_->setToken(token_);

    Q_EMIT busy(tr("Validating saved credentials…"));
    // Hits /user — the smallest endpoint that requires authentication.
    // The result fires onClientUserValidated via the lambda relay.
    client_->validateToken();
}

void SessionController::signIn(QWidget* dialogParent)
{
    ghm::ui::LoginDialog dlg(dialogParent);
    if (dlg.exec() != QDialog::Accepted) return;

    username_  = dlg.verifiedUsername();
    token_     = dlg.token();
    tokenType_ = dlg.tokenIsOAuth()
                 ? ghm::core::TokenType::Oauth
                 : ghm::core::TokenType::Pat;
    client_->setToken(token_);
    settings_.setLastUsername(username_);

    // Persist before announcing — if the keyring write fails the user
    // is still signed in for this session, but they should know.
    if (auto r = storage_.saveToken(username_, token_, tokenType_); !r.ok) {
        Q_EMIT keyringError(r.error);
    }

    // The dialog already validated the token via its own client during
    // confirm; we can announce signedIn() immediately.
    Q_EMIT signedIn(username_);
    refreshRepositories();
}

void SessionController::signOut()
{
    if (!username_.isEmpty()) {
        storage_.clearToken(username_);
    }
    token_.clear();
    username_.clear();
    tokenType_ = ghm::core::TokenType::Unknown;
    Q_EMIT signedOut();
}

void SessionController::refreshRepositories()
{
    if (!client_->hasToken()) return;
    Q_EMIT busy(tr("Loading repositories…"));
    client_->fetchRepositories();
}

// ----- GitHubClient relays -------------------------------------------------

void SessionController::onClientUserValidated(bool ok,
                                              const QString& login,
                                              const QString& error)
{
    const bool wasRestoring = restoring_;
    restoring_ = false;

    if (!ok) {
        // Wipe state — both restore and explicit-validate paths should
        // leave the session firmly logged-out on failure.
        if (!username_.isEmpty()) storage_.clearToken(username_);
        token_.clear();
        username_.clear();
        Q_EMIT authError(error);
        Q_EMIT signedOut();
        return;
    }

    username_ = login;
    settings_.setLastUsername(login);

    if (wasRestoring) {
        Q_EMIT sessionRestored(login);
    } else {
        Q_EMIT signedIn(login);
    }
    refreshRepositories();
}

void SessionController::onClientReposReceived(
    bool ok,
    const QList<ghm::github::Repository>& repos,
    const QString& error)
{
    if (!ok) {
        Q_EMIT networkError(error);
        return;
    }
    Q_EMIT repositoriesLoaded(repos);
}

} // namespace ghm::session
