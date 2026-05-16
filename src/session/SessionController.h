#pragma once

// SessionController — owns everything related to "who is signed in".
//
// Pulled out of the original MainWindow to scope down the coordinator
// blob. Specifically responsible for:
//   * Validating saved tokens on app startup (silent re-sign-in)
//   * Showing LoginDialog on demand and persisting accepted tokens
//   * Signing out and wiping the keyring entry
//   * Fetching the user's repository list
//
// Owns the GitHubClient and the keyring access. Does not touch the
// repository list widget or status bar directly — instead it emits
// signals that MainWindow translates into UI updates.

#include <QObject>
#include <QString>
#include <QList>

#include "github/Repository.h"
#include "core/SecureStorage.h"  // TokenType enum

namespace ghm::core   { class Settings; }
namespace ghm::github { class GitHubClient; }

namespace ghm::session {

class SessionController : public QObject {
    Q_OBJECT
public:
    SessionController(ghm::core::SecureStorage& storage,
                      ghm::core::Settings&      settings,
                      QObject*                  parent = nullptr);

    // Read-only inspection of state.
    bool    isSignedIn() const { return !token_.isEmpty(); }
    QString username()   const { return username_; }
    QString token()      const { return token_; }
    ghm::core::TokenType tokenType() const { return tokenType_; }

    // Direct access — needed by other controllers (e.g. PublishController
    // for repo creation). The pointer is stable for the lifetime of this
    // controller.
    ghm::github::GitHubClient* client() { return client_; }

    // App-startup entry point. Looks for a saved token and validates it.
    // Emits sessionRestored() on success, signedOut() if no token was
    // present, or authError() if validation failed.
    void tryRestoreSession();

    // User-initiated. Opens LoginDialog (the host wires the dialog so we
    // don't pull QtWidgets in here as a dep — yes, we already have it,
    // but cleaner this way for future test targets). Returns the
    // accepted token via signedIn() if confirmed.
    void signIn(QWidget* dialogParent);

    // Discards the in-memory + persisted token. Emits signedOut().
    void signOut();

    // Refresh the repository list against the GitHub API. Emits
    // repositoriesLoaded() or networkError().
    void refreshRepositories();

Q_SIGNALS:
    // Emitted with the username after a successful token validation
    // (either silent restore or user-initiated sign-in).
    void signedIn(const QString& username);

    // Convenience: same as signedIn but specifically for the silent
    // startup path. Lets the host distinguish "user just clicked
    // Sign in" from "we found a saved token". Useful for UX hints.
    void sessionRestored(const QString& username);

    // Emitted whenever the session goes from authenticated to not
    // (sign out, app start with no token, validation failure).
    void signedOut();

    // The keyring couldn't be read or written. Non-fatal but the user
    // probably wants to know.
    void keyringError(const QString& message);

    // Token validation against /user failed. Means the saved token
    // was revoked, expired, or never valid.
    void authError(const QString& message);

    // Network error during repo listing. Distinct from auth errors —
    // those mean "your token is bad", these mean "we couldn't reach
    // GitHub right now".
    void networkError(const QString& message);

    // Repo list ready to display. The host (RepositoryListWidget) is
    // responsible for rendering.
    void repositoriesLoaded(const QList<ghm::github::Repository>& repos);

    // Fired during the brief async window where we're still validating
    // a stored token at startup. Lets the host show a "loading…" hint.
    void busy(const QString& statusMessage);

private:
    // Internal handlers called from lambda relays in the constructor.
    // Not slots — there's no GitHubClient signal that hands us all
    // three arguments directly; we synthesise them in the lambdas.
    void onClientUserValidated(bool ok, const QString& login, const QString& error);
    void onClientReposReceived(bool ok,
                               const QList<ghm::github::Repository>& repos,
                               const QString& error);

    ghm::core::SecureStorage&  storage_;
    ghm::core::Settings&       settings_;
    ghm::github::GitHubClient* client_;

    QString token_;
    QString username_;

    // Set on every successful sign-in (or restore). Tells the UI
    // whether the current session uses an OAuth token or a manually-
    // entered PAT. Used by the "About / Account" affordance to show
    // "Authenticated via OAuth" vs "via Personal Access Token", and
    // (in future) by a token-refresh path that's only meaningful
    // for OAuth.
    ghm::core::TokenType tokenType_{ghm::core::TokenType::Unknown};

    // True while we're in the silent-restore code path. Lets us
    // distinguish a startup auth failure from a user-typed-bad-token
    // failure when the same client signal fires for both.
    bool restoring_{false};
};

} // namespace ghm::session
