#include "session/OAuthFlowController.h"

#include "github/GitHubClient.h"

namespace ghm::session {

OAuthFlowController::OAuthFlowController(ghm::github::GitHubClient& client,
                                          QObject* parent)
    : QObject(parent)
    , client_(client)
{
    pollTimer_.setSingleShot(false);
    connect(&pollTimer_, &QTimer::timeout, this, &OAuthFlowController::onTick);

    // Subscribe once. Each handler short-circuits if state_ doesn't
    // match — that way another part of the app calling client_
    // methods (e.g. LoginDialog with a PAT) doesn't accidentally
    // trigger our state machine.
    connect(&client_, &ghm::github::GitHubClient::deviceCodeReceived,
            this, &OAuthFlowController::onDeviceCodeReceived);
    connect(&client_, &ghm::github::GitHubClient::accessTokenReceived,
            this, &OAuthFlowController::onAccessTokenReceived);
    connect(&client_, &ghm::github::GitHubClient::pollPending,
            this, &OAuthFlowController::onPollPending);
    connect(&client_, &ghm::github::GitHubClient::pollSlowDown,
            this, &OAuthFlowController::onPollSlowDown);
    connect(&client_, &ghm::github::GitHubClient::oauthError,
            this, &OAuthFlowController::onOauthError);
}

bool OAuthFlowController::start(const QString& clientId, const QString& scope)
{
    if (isActive()) return false;
    if (clientId.isEmpty()) {
        Q_EMIT failed(tr("OAuth client_id is not configured for this build."));
        return false;
    }
    clientId_   = clientId;
    deviceCode_.clear();
    state_      = State::FetchingDeviceCode;

    Q_EMIT statusChanged(tr("Requesting device code from GitHub…"));
    client_.startDeviceFlow(clientId, scope);
    return true;
}

void OAuthFlowController::cancel()
{
    reset();
}

void OAuthFlowController::reset()
{
    state_ = State::Idle;
    pollTimer_.stop();
    deviceCode_.clear();
    clientId_.clear();
}

void OAuthFlowController::fail(const QString& message)
{
    reset();
    Q_EMIT failed(message);
}

void OAuthFlowController::onDeviceCodeReceived(const QString& userCode,
                                                const QString& verificationUri,
                                                const QString& deviceCode,
                                                int expiresInSeconds,
                                                int pollIntervalSeconds)
{
    if (state_ != State::FetchingDeviceCode) return;

    deviceCode_      = deviceCode;
    pollIntervalMs_  = pollIntervalSeconds * 1000;
    expiresInMs_     = expiresInSeconds * 1000;
    state_           = State::WaitingForUser;
    expiryTimer_.start();

    Q_EMIT userCodeReady(userCode, verificationUri);
    Q_EMIT statusChanged(tr("Waiting for you to authorize in the browser…"));

    pollTimer_.start(pollIntervalMs_);
}

void OAuthFlowController::onTick()
{
    if (state_ != State::WaitingForUser) return;

    // Expiry check: GitHub returns expired_token when we exceed it,
    // but we can stop proactively to give a cleaner error and avoid
    // one wasted round-trip.
    if (expiryTimer_.elapsed() > expiresInMs_) {
        fail(tr("The authorization code has expired. Please try signing in again."));
        return;
    }

    client_.pollAccessToken(clientId_, deviceCode_);
}

void OAuthFlowController::onAccessTokenReceived(const QString& token,
                                                 const QString& scope)
{
    if (state_ != State::WaitingForUser) return;
    reset();
    Q_EMIT signedIn(token, scope);
}

void OAuthFlowController::onPollPending()
{
    // No-op — the user just hasn't authorised yet. Next tick will
    // poll again at the same interval.
    if (state_ != State::WaitingForUser) return;
}

void OAuthFlowController::onPollSlowDown()
{
    // RFC 8628 §3.5: add 5 seconds to the polling interval.
    if (state_ != State::WaitingForUser) return;
    pollIntervalMs_ += 5'000;
    pollTimer_.start(pollIntervalMs_);  // restart with new interval
    Q_EMIT statusChanged(tr(
        "Polling more slowly to respect GitHub's rate limit…"));
}

void OAuthFlowController::onOauthError(const QString& message)
{
    // Could fire during FetchingDeviceCode (bad client_id) or
    // WaitingForUser (user denied, code expired, etc.) — either way
    // we abort with the message.
    if (state_ == State::Idle) return;  // not ours
    fail(message);
}

} // namespace ghm::session
