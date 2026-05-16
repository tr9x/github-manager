#pragma once

// OAuthFlowController — drives the GitHub device-flow login from
// "user clicks Sign in with GitHub" all the way to "we have an
// access token". Sits between OAuthLoginDialog (which shows the
// user code and a cancel button) and GitHubClient (which does the
// raw HTTP).
//
// State machine:
//
//   Idle
//     ↓ start(clientId, scope)
//   FetchingDeviceCode
//     ↓ client.deviceCodeReceived
//   WaitingForUser  (polling every `pollIntervalSeconds`)
//     ↓ client.accessTokenReceived
//   Success → emit signedIn(token, scope)
//   On error at any step → emit failed(message)
//   On cancel by user    → reset; no signal
//
// The timer-driven polling is implemented here, not in the dialog,
// so the dialog stays purely presentational. Caller (LoginDialog or
// MainWindow) wires the controller's signals to UI feedback.

#include <QObject>
#include <QString>
#include <QTimer>
#include <QElapsedTimer>

namespace ghm::github { class GitHubClient; }

namespace ghm::session {

class OAuthFlowController : public QObject {
    Q_OBJECT
public:
    OAuthFlowController(ghm::github::GitHubClient& client,
                        QObject* parent = nullptr);

    // True between start() and the corresponding signedIn()/failed()
    // (or cancel()).
    bool isActive() const { return state_ != State::Idle; }

    // Begin the device flow. `clientId` is the OAuth App's client_id
    // (build-time configured in CMake); `scope` is the GitHub OAuth
    // scope string ("repo" + " read:user" for our needs). Returns
    // false if already active.
    bool start(const QString& clientId, const QString& scope);

    // Abort the flow. No signals fire — caller is presumed to know.
    // Used when the user clicks Cancel or closes the login dialog
    // mid-poll.
    void cancel();

Q_SIGNALS:
    // GitHub returned the device code. The dialog should now show
    // `userCode` to the user (big, easy to read) and open
    // `verificationUri` in their browser.
    void userCodeReady(const QString& userCode,
                       const QString& verificationUri);

    // Polling state changes the dialog might want to show:
    //   * "Waiting for authorization in browser…"
    //   * "Polling slowed down…" (when we hit slow_down)
    //   * "Authorization code will expire in X minutes" (countdown)
    void statusChanged(const QString& message);

    // Terminal outcomes.
    void signedIn(const QString& token, const QString& scope);
    void failed(const QString& message);

private Q_SLOTS:
    void onTick();
    void onDeviceCodeReceived(const QString& userCode,
                               const QString& verificationUri,
                               const QString& deviceCode,
                               int expiresInSeconds,
                               int pollIntervalSeconds);
    void onAccessTokenReceived(const QString& token, const QString& scope);
    void onPollPending();
    void onPollSlowDown();
    void onOauthError(const QString& message);

private:
    enum class State {
        Idle,
        FetchingDeviceCode,
        WaitingForUser,
    };

    void fail(const QString& message);
    void reset();

    ghm::github::GitHubClient&  client_;
    QTimer                      pollTimer_;
    QElapsedTimer               expiryTimer_;

    State    state_{State::Idle};
    QString  clientId_;
    QString  deviceCode_;
    int      pollIntervalMs_{5000};
    int      expiresInMs_{900'000};
};

} // namespace ghm::session
