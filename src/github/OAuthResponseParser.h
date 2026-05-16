#pragma once

// OAuth device-flow response parsers. Pure functions, header-only —
// they take JSON bytes and return parsed structs. Kept separate from
// GitHubClient so we can unit-test the parsing logic without spinning
// up a network mock.
//
// References:
//   * https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps#device-flow
//   * RFC 8628 (OAuth 2.0 Device Authorization Grant)

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

namespace ghm::github {

// Response to POST /login/device/code.
//
// Lifetime: the user has `expiresInSeconds` to enter `userCode` at
// `verificationUri`. Our app polls `/login/oauth/access_token` with
// `deviceCode` every `pollIntervalSeconds` until either the user
// authorises (success) or the device code expires (failure).
struct DeviceCodeResponse {
    bool    ok{false};
    QString error;             // populated when ok=false
    QString deviceCode;        // long opaque string we keep server-side
    QString userCode;          // short code to display ("WDJB-MJHT")
    QString verificationUri;   // URL we open in the browser
    int     expiresInSeconds{0};
    int     pollIntervalSeconds{5};  // GitHub default
};

inline DeviceCodeResponse parseDeviceCodeResponse(const QByteArray& body)
{
    DeviceCodeResponse r;
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = QStringLiteral("Invalid JSON: %1").arg(err.errorString());
        return r;
    }
    const auto obj = doc.object();
    if (obj.contains(QStringLiteral("error"))) {
        // GitHub returns 200 OK with {error, error_description} for
        // some failure cases (e.g. unknown client_id). Surface the
        // description to the user.
        r.error = obj.value(QStringLiteral("error_description")).toString(
            obj.value(QStringLiteral("error")).toString());
        return r;
    }
    r.deviceCode      = obj.value(QStringLiteral("device_code")).toString();
    r.userCode        = obj.value(QStringLiteral("user_code")).toString();
    r.verificationUri = obj.value(QStringLiteral("verification_uri")).toString();
    r.expiresInSeconds       = obj.value(QStringLiteral("expires_in")).toInt(0);
    r.pollIntervalSeconds    = obj.value(QStringLiteral("interval")).toInt(5);
    r.ok = !r.deviceCode.isEmpty() && !r.userCode.isEmpty();
    if (!r.ok && r.error.isEmpty()) {
        r.error = QStringLiteral("Malformed device-code response (missing fields).");
    }
    return r;
}

// Response to POST /login/oauth/access_token. There are three possible
// outcomes during polling:
//
//   * Success: returns access_token (and possibly refresh_token,
//     token_type=bearer, scope). We treat just access_token as
//     mandatory — the rest are nice-to-have.
//   * Pending: returns {error: "authorization_pending"} — user
//     hasn't entered the code yet. Caller should keep polling at
//     the original interval.
//   * Slow-down: returns {error: "slow_down"} — caller is polling
//     too fast. Caller should add 5 seconds to its interval (per RFC).
//   * Terminal failure: returns {error: <other>} — device-code
//     expired, user denied access, etc. Caller stops polling and
//     surfaces the error.
struct AccessTokenResponse {
    enum class State {
        Success,
        Pending,
        SlowDown,
        Error,
    };

    State   state{State::Error};
    QString error;             // populated when state=Error (human-readable)
    QString accessToken;       // populated when state=Success
    QString refreshToken;      // optional; long-lived OAuth flows may include
    QString scope;             // space-separated, what GitHub actually granted
};

inline AccessTokenResponse parseAccessTokenResponse(const QByteArray& body)
{
    AccessTokenResponse r;
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        r.error = QStringLiteral("Invalid JSON: %1").arg(err.errorString());
        return r;
    }
    const auto obj = doc.object();

    const QString errStr = obj.value(QStringLiteral("error")).toString();
    if (!errStr.isEmpty()) {
        if (errStr == QLatin1String("authorization_pending")) {
            r.state = AccessTokenResponse::State::Pending;
            return r;
        }
        if (errStr == QLatin1String("slow_down")) {
            r.state = AccessTokenResponse::State::SlowDown;
            return r;
        }
        // Anything else is a terminal failure. GitHub usually provides
        // a description but fall back to the error code if not.
        r.state = AccessTokenResponse::State::Error;
        r.error = obj.value(QStringLiteral("error_description")).toString(errStr);
        return r;
    }

    const QString token = obj.value(QStringLiteral("access_token")).toString();
    if (token.isEmpty()) {
        // No error AND no token — malformed response. We err toward
        // "terminal error" rather than re-polling, because polling
        // forever on a broken response is worse than failing fast.
        r.state = AccessTokenResponse::State::Error;
        r.error = QStringLiteral(
            "Server returned a response with neither an error nor an access token.");
        return r;
    }
    r.state        = AccessTokenResponse::State::Success;
    r.accessToken  = token;
    r.refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
    r.scope        = obj.value(QStringLiteral("scope")).toString();
    return r;
}

} // namespace ghm::github
