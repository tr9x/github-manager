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

Q_SIGNALS:
    void authenticated(const QString& username);
    void authenticationFailed(const QString& reason);
    void repositoriesReady(const QList<ghm::github::Repository>& repos);
    void networkError(const QString& message);

private:
    void getJson(const QUrl& url,
                 std::function<void(QNetworkReply*)> onFinished);

    void fetchReposPage(const QUrl& url,
                        QList<Repository> accumulated,
                        int maxRepos);

    QNetworkAccessManager* nam_;
    QString                token_;

    static constexpr const char* kApiBase = "https://api.github.com";
};

} // namespace ghm::github
