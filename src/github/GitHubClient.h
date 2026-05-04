#pragma once

// GitHubClient - asynchronous wrapper around the REST API v3.
//
// All methods are non-blocking: they return immediately and the caller
// connects to the relevant signal. QNetworkAccessManager handles HTTP,
// pagination is followed automatically via the Link header.

#include <QObject>
#include <QString>
#include <QList>

#include "github/Repository.h"

class QNetworkAccessManager;
class QNetworkReply;

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
    // On success emits repositoryCreated() with the populated Repository.
    // On failure emits networkError() with GitHub's "message" field
    // (so the user sees e.g. "name already exists on this account").
    void createRepository(const QString& name,
                          const QString& description,
                          bool           isPrivate,
                          bool           autoInit = false);

Q_SIGNALS:
    void authenticated(const QString& username);
    void authenticationFailed(const QString& reason);
    void repositoriesReady(const QList<ghm::github::Repository>& repos);
    void repositoryCreated(const ghm::github::Repository& repo);
    void networkError(const QString& message);

private:
    void getJson(const QUrl& url,
                 std::function<void(QNetworkReply*)> onFinished);

    void postJson(const QUrl& url,
                  const QByteArray& body,
                  std::function<void(QNetworkReply*)> onFinished);

    void fetchReposPage(const QUrl& url,
                        QList<Repository> accumulated,
                        int maxRepos);

    QNetworkAccessManager* nam_;
    QString                token_;

    static constexpr const char* kApiBase = "https://api.github.com";
};

} // namespace ghm::github
