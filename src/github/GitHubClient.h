#pragma once

// GitHubClient - asynchronous wrapper around the REST API v3.
//
// All methods are non-blocking: they return immediately and the caller
// connects to the relevant signal. QNetworkAccessManager handles HTTP,
// pagination is followed automatically via the Link header.

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>

#include "github/Repository.h"

class QNetworkAccessManager;
class QNetworkReply;
class QSslError;

namespace ghm::github {

class GitHubClient : public QObject {
    Q_OBJECT
public:
    explicit GitHubClient(QObject* parent = nullptr);
    ~GitHubClient() override;

    void setToken(const QString& token);
    bool hasToken() const;

    // Validates the current token. On success emits authenticated() with
    // the resolved username; on failure authenticationFailed().
    void validateToken();

    // Fetches the authenticated user's repos (all pages, up to maxRepos).
    // Emits repositoriesReady on completion or networkError on failure.
    void fetchRepositories(int maxRepos = 500);

    // Creates a new repository under the authenticated user via
    // POST /user/repos. `autoInit=false` is the right choice for
    // publishing an existing local folder — auto-init creates an
    // initial README which then conflicts with `git push`.
    //
    // licenseTemplate / gitignoreTemplate: GitHub's "Choose a License"
    // templates (e.g. "mit", "apache-2.0") and gitignore templates
    // (e.g. "Python", "Node"). Empty = don't add. Either of these
    // FORCES auto_init=true on GitHub's side — the templates can only
    // be applied at repo creation along with the initial commit. If
    // the caller passes a license/gitignore alongside autoInit=false,
    // we silently upgrade autoInit to true and the caller is expected
    // to handle the resulting push conflict (typically by pulling
    // GitHub's initial commit before pushing local commits, or by
    // accepting that the local push will be rejected on non-ff).
    //
    // On success emits repositoryCreated() with the populated Repository.
    // On failure emits networkError() with GitHub's "message" field
    // (so the user sees e.g. "name already exists on this account").
    void createRepository(const QString& name,
                          const QString& description,
                          bool           isPrivate,
                          bool           autoInit = false,
                          const QString& licenseTemplate   = QString(),
                          const QString& gitignoreTemplate = QString());

    // PATCH /repos/{owner}/{repo} with body {"private": <bool>}.
    // Toggles visibility of an existing repository the user owns
    // (or has admin access to). On success emits visibilityChanged
    // with the updated Repository (refreshed from the response —
    // GitHub returns the full repo object on PATCH).
    //
    // Failure cases worth knowing:
    //   * 403 — user lacks admin permission on the repo
    //   * 422 — org policy disallows visibility changes
    //   * 404 — repo doesn't exist or token lacks the right scope
    // All surface via networkError() with GitHub's humanised message.
    //
    // fullName: "owner/name" form (e.g. "tr9xpl/github-manager").
    // Caller is responsible for confirming user intent before calling
    // — there's no preview/staging step here.
    void updateRepositoryVisibility(const QString& fullName,
                                      bool           makePrivate);

    // -- Repository detail endpoints (for "preview" panel) -----------------
    //
    // These three populate the new repository detail tabs. Each is
    // a separate fire-and-forget call; widgets coalesce results via
    // the corresponding signals. All three accept fullName in the
    // "owner/repo" form.

    // GET /repos/{owner}/{repo}/readme
    // Returns the default README in base64-encoded form. On success
    // emits readmeFetched(fullName, decoded markdown text). On 404
    // (no README in repo) emits readmeNotFound(fullName) — not an
    // error from the UI's perspective.
    void fetchReadme(const QString& fullName);

    // GET /repos/{owner}/{repo}/contents/{path}
    // Lists files/dirs at the given path (empty path = root).
    // Emits contentsFetched(fullName, path, list). Each entry
    // carries name, type ("file"/"dir"), size, htmlUrl.
    void fetchContents(const QString& fullName, const QString& path);

    // GET /repos/{owner}/{repo}/languages
    // Returns {language → byte-count}. Used to render the languages
    // bar in the About tab. Emits languagesFetched(fullName, map).
    void fetchLanguages(const QString& fullName);

    // -- OAuth device flow ------------------------------------------------
    //
    // Start the device-authorization flow. clientId is the GitHub
    // OAuth App's client_id (build-time configured); scope is the
    // space-separated permission scope ("repo" gets read/write to
    // private repos, which is what we need for clone/push).
    //
    // On the wire: POST https://github.com/login/device/code
    //              body: client_id=<id>&scope=<scope>
    //
    // Emits deviceCodeReceived on success or oauthError on failure.
    // The caller is then expected to call pollAccessToken() in a
    // timer loop until either accessTokenReceived or oauthError fires.
    void startDeviceFlow(const QString& clientId,
                         const QString& scope);

    // Poll for the user's authorisation completion. Caller should
    // call this every `pollIntervalSeconds` (from the
    // deviceCodeReceived response) until either accessTokenReceived
    // fires (success), oauthError fires (terminal failure), or the
    // caller times out from the deviceCode expiry.
    //
    // On the wire: POST https://github.com/login/oauth/access_token
    //              body: client_id=<id>&device_code=<dc>
    //                    &grant_type=urn:ietf:params:oauth:grant-type:device_code
    //
    // Possible outcomes:
    //   * Success → accessTokenReceived(token)
    //   * "authorization_pending" → pollPending() (caller keeps polling)
    //   * "slow_down" → pollSlowDown() (caller adds 5s to its interval)
    //   * Any other error → oauthError(message)
    void pollAccessToken(const QString& clientId,
                         const QString& deviceCode);

Q_SIGNALS:
    void authenticated(const QString& username);
    void authenticationFailed(const QString& reason);
    void repositoriesReady(const QList<ghm::github::Repository>& repos);
    void repositoryCreated(const ghm::github::Repository& repo);
    // Emitted after a successful PATCH /repos/{owner}/{repo}. The
    // updated Repository carries the new `isPrivate` flag. Caller
    // typically refreshes the sidebar listing on receipt.
    void visibilityChanged(const ghm::github::Repository& repo);
    void networkError(const QString& message);

    // Repository-detail signals (see fetchReadme/fetchContents/
    // fetchLanguages above for context).
    void readmeFetched(const QString& fullName, const QString& markdown);
    void readmeNotFound(const QString& fullName);
    void contentsFetched(const QString& fullName,
                          const QString& path,
                          const QList<ghm::github::ContentEntry>& entries);
    void languagesFetched(const QString& fullName,
                           const QMap<QString, qint64>& bytesByLang);

    // OAuth device flow signals.
    void deviceCodeReceived(const QString& userCode,
                             const QString& verificationUri,
                             const QString& deviceCode,
                             int expiresInSeconds,
                             int pollIntervalSeconds);
    void accessTokenReceived(const QString& token,
                              const QString& scope);
    // Non-terminal poll feedback so the caller's timer knows to keep
    // going or to back off, without parsing JSON itself.
    void pollPending();
    void pollSlowDown();
    void oauthError(const QString& message);

private:
    void getJson(const QUrl& url,
                 std::function<void(QNetworkReply*)> onFinished);

    void postJson(const QUrl& url,
                  const QByteArray& body,
                  std::function<void(QNetworkReply*)> onFinished);

    // PATCH variant of postJson. QNetworkAccessManager doesn't have
    // a first-class patch() method, so we route through
    // sendCustomRequest("PATCH", ...). Same callback signature.
    void patchJson(const QUrl& url,
                    const QByteArray& body,
                    std::function<void(QNetworkReply*)> onFinished);

    void fetchReposPage(const QUrl& url,
                        QList<Repository> accumulated,
                        int maxRepos);

    // Inline handler for QNetworkAccessManager::sslErrors. Routes
    // through TlsCertApprover: already-trusted fingerprints get
    // ignoreSslErrors() silently; unknown ones pop the approval
    // dialog. Defined in .cpp.
    void handleSslErrors(QNetworkReply* reply,
                          const QList<QSslError>& errors);

    QNetworkAccessManager* nam_;
    QString                token_;

    static constexpr const char* kApiBase = "https://api.github.com";
};

} // namespace ghm::github
