// Unit tests for github/OAuthResponseParser.h.
//
// Each test feeds a hand-written JSON body that mirrors what GitHub
// (or RFC 8628-compliant servers) actually returns at each stage of
// the device flow. Test data was captured from real GitHub responses
// and from RFC 8628 §3.5 "Device Authorization Response" examples.

#include <QtTest>
#include <QObject>
#include <QByteArray>

#include "github/OAuthResponseParser.h"

using ghm::github::parseDeviceCodeResponse;
using ghm::github::parseAccessTokenResponse;
using ghm::github::AccessTokenResponse;

class TestOAuthResponseParser : public QObject {
    Q_OBJECT

private slots:

    // ----- device code parsing ------------------------------------------

    void deviceCode_happyPath()
    {
        // Canonical GitHub device-code response.
        const auto r = parseDeviceCodeResponse(R"({
            "device_code": "3584d83530557fdd1f46af8289938c8ef79f9dc5",
            "user_code": "WDJB-MJHT",
            "verification_uri": "https://github.com/login/device",
            "expires_in": 900,
            "interval": 5
        })");
        QVERIFY(r.ok);
        QCOMPARE(r.deviceCode, QStringLiteral("3584d83530557fdd1f46af8289938c8ef79f9dc5"));
        QCOMPARE(r.userCode,   QStringLiteral("WDJB-MJHT"));
        QCOMPARE(r.verificationUri, QStringLiteral("https://github.com/login/device"));
        QCOMPARE(r.expiresInSeconds, 900);
        QCOMPARE(r.pollIntervalSeconds, 5);
    }

    void deviceCode_intervalFallsBackTo5()
    {
        // GitHub sometimes omits "interval" — we default to 5s.
        const auto r = parseDeviceCodeResponse(R"({
            "device_code": "x",
            "user_code": "Y",
            "verification_uri": "https://github.com/login/device",
            "expires_in": 600
        })");
        QVERIFY(r.ok);
        QCOMPARE(r.pollIntervalSeconds, 5);
    }

    void deviceCode_errorResponse()
    {
        // GitHub returns 200 with {error} for bad client_id.
        const auto r = parseDeviceCodeResponse(R"({
            "error": "incorrect_client_credentials",
            "error_description": "The client_id provided is invalid."
        })");
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("The client_id provided is invalid."));
    }

    void deviceCode_errorWithoutDescription()
    {
        // When no description is given, surface the error code itself.
        const auto r = parseDeviceCodeResponse(R"({
            "error": "incorrect_client_credentials"
        })");
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("incorrect_client_credentials"));
    }

    void deviceCode_malformedJson()
    {
        const auto r = parseDeviceCodeResponse("not json at all");
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    void deviceCode_missingRequiredFields()
    {
        // Empty object — no device_code, no user_code. Should report
        // malformed rather than silently succeed.
        const auto r = parseDeviceCodeResponse("{}");
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    // ----- access token parsing -----------------------------------------

    void accessToken_success()
    {
        const auto r = parseAccessTokenResponse(R"({
            "access_token": "ghu_AbCdEf1234567890",
            "token_type": "bearer",
            "scope": "repo,read:user"
        })");
        QCOMPARE(r.state, AccessTokenResponse::State::Success);
        QCOMPARE(r.accessToken, QStringLiteral("ghu_AbCdEf1234567890"));
        QCOMPARE(r.scope, QStringLiteral("repo,read:user"));
    }

    void accessToken_successWithRefresh()
    {
        // Some flows include refresh tokens (long-lived OAuth Apps).
        const auto r = parseAccessTokenResponse(R"({
            "access_token": "gho_xyz",
            "refresh_token": "ghr_abc",
            "expires_in": 28800,
            "token_type": "bearer"
        })");
        QCOMPARE(r.state, AccessTokenResponse::State::Success);
        QCOMPARE(r.accessToken,  QStringLiteral("gho_xyz"));
        QCOMPARE(r.refreshToken, QStringLiteral("ghr_abc"));
    }

    void accessToken_pending()
    {
        // User hasn't entered the code yet — caller keeps polling.
        const auto r = parseAccessTokenResponse(R"({
            "error": "authorization_pending",
            "error_description": "The user has not yet entered the code."
        })");
        QCOMPARE(r.state, AccessTokenResponse::State::Pending);
        // Pending is not an error from the caller's perspective.
    }

    void accessToken_slowDown()
    {
        const auto r = parseAccessTokenResponse(R"({
            "error": "slow_down",
            "error_description": "You are polling too quickly. Slow down."
        })");
        QCOMPARE(r.state, AccessTokenResponse::State::SlowDown);
    }

    void accessToken_expired()
    {
        // Device code expired (typically after 15 minutes).
        const auto r = parseAccessTokenResponse(R"({
            "error": "expired_token",
            "error_description": "The device code has expired."
        })");
        QCOMPARE(r.state, AccessTokenResponse::State::Error);
        QCOMPARE(r.error, QStringLiteral("The device code has expired."));
    }

    void accessToken_accessDenied()
    {
        const auto r = parseAccessTokenResponse(R"({
            "error": "access_denied",
            "error_description": "The user denied the authorization request."
        })");
        QCOMPARE(r.state, AccessTokenResponse::State::Error);
        QCOMPARE(r.error, QStringLiteral("The user denied the authorization request."));
    }

    void accessToken_malformedReturnsError()
    {
        // No error and no access_token — neither pending nor success.
        // We classify as Error rather than poll forever.
        const auto r = parseAccessTokenResponse(R"({"token_type": "bearer"})");
        QCOMPARE(r.state, AccessTokenResponse::State::Error);
        QVERIFY(!r.error.isEmpty());
    }

    void accessToken_invalidJson()
    {
        const auto r = parseAccessTokenResponse("garbage{");
        QCOMPARE(r.state, AccessTokenResponse::State::Error);
        QVERIFY(!r.error.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestOAuthResponseParser)
#include "test_OAuthResponseParser.moc"
