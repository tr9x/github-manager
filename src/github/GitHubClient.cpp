#include "github/GitHubClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QRegularExpression>

namespace ghm::github {

namespace {

QNetworkRequest makeAuthedRequest(const QUrl& url, const QString& token)
{
    QNetworkRequest req(url);
    req.setRawHeader("Accept",               "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setRawHeader("User-Agent",           "github-manager/" GHM_VERSION);
    if (!token.isEmpty()) {
        req.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + token.toUtf8());
    }
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
}

// Extracts the next-page URL from a Link header.
//   Link: <https://api.github.com/...&page=2>; rel="next", <...>; rel="last"
QUrl nextPageFromLink(const QByteArray& linkHeader)
{
    if (linkHeader.isEmpty()) return {};

    static const QRegularExpression re(
        QStringLiteral(R"(<([^>]+)>\s*;\s*rel="next")"));
    const auto m = re.match(QString::fromUtf8(linkHeader));
    return m.hasMatch() ? QUrl(m.captured(1)) : QUrl();
}

Repository parseRepo(const QJsonObject& o)
{
    Repository r;
    r.name          = o.value(QStringLiteral("name")).toString();
    r.fullName      = o.value(QStringLiteral("full_name")).toString();
    r.description   = o.value(QStringLiteral("description")).toString();
    r.cloneUrl      = o.value(QStringLiteral("clone_url")).toString();
    r.sshUrl        = o.value(QStringLiteral("ssh_url")).toString();
    r.defaultBranch = o.value(QStringLiteral("default_branch")).toString();
    r.isPrivate     = o.value(QStringLiteral("private")).toBool();
    r.isFork        = o.value(QStringLiteral("fork")).toBool();
    r.sizeKb        = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble());

    const QString updated = o.value(QStringLiteral("updated_at")).toString();
    r.updatedAt = QDateTime::fromString(updated, Qt::ISODate);
    return r;
}

QString humaniseHttpError(QNetworkReply* reply)
{
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // Try to pull the GitHub error message out of the body.
    const QByteArray body = reply->readAll();
    QString gh;
    const auto doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        gh = doc.object().value(QStringLiteral("message")).toString();
    }

    if (status > 0) {
        return gh.isEmpty()
            ? QStringLiteral("HTTP %1: %2").arg(status).arg(reply->errorString())
            : QStringLiteral("HTTP %1: %2").arg(status).arg(gh);
    }
    return reply->errorString();
}

} // namespace

GitHubClient::GitHubClient(QObject* parent)
    : QObject(parent)
    , nam_(new QNetworkAccessManager(this))
{}

GitHubClient::~GitHubClient() = default;

void GitHubClient::setToken(const QString& token) { token_ = token; }
bool GitHubClient::hasToken() const               { return !token_.isEmpty(); }

void GitHubClient::getJson(const QUrl& url,
                           std::function<void(QNetworkReply*)> onFinished)
{
    QNetworkReply* reply = nam_->get(makeAuthedRequest(url, token_));
    connect(reply, &QNetworkReply::finished, this, [reply, cb = std::move(onFinished)] {
        cb(reply);
        reply->deleteLater();
    });
}

void GitHubClient::postJson(const QUrl& url,
                            const QByteArray& body,
                            std::function<void(QNetworkReply*)> onFinished)
{
    QNetworkRequest req = makeAuthedRequest(url, token_);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    QNetworkReply* reply = nam_->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [reply, cb = std::move(onFinished)] {
        cb(reply);
        reply->deleteLater();
    });
}

void GitHubClient::validateToken()
{
    if (token_.isEmpty()) {
        Q_EMIT authenticationFailed(QStringLiteral("No token configured."));
        return;
    }

    getJson(QUrl(QStringLiteral("%1/user").arg(QString::fromLatin1(kApiBase))),
        [this](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) {
                Q_EMIT authenticationFailed(humaniseHttpError(reply));
                return;
            }
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                Q_EMIT authenticationFailed(QStringLiteral("Malformed response from GitHub."));
                return;
            }
            const QString login = doc.object().value(QStringLiteral("login")).toString();
            if (login.isEmpty()) {
                Q_EMIT authenticationFailed(QStringLiteral("Token accepted but user has no login?"));
                return;
            }
            Q_EMIT authenticated(login);
        });
}

void GitHubClient::fetchRepositories(int maxRepos)
{
    QUrl url(QStringLiteral("%1/user/repos").arg(QString::fromLatin1(kApiBase)));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
    q.addQueryItem(QStringLiteral("sort"),     QStringLiteral("updated"));
    q.addQueryItem(QStringLiteral("affiliation"),
                   QStringLiteral("owner,collaborator,organization_member"));
    url.setQuery(q);

    fetchReposPage(url, {}, maxRepos);
}

void GitHubClient::fetchReposPage(const QUrl& url,
                                  QList<Repository> accumulated,
                                  int maxRepos)
{
    getJson(url, [this, accumulated = std::move(accumulated), maxRepos]
                 (QNetworkReply* reply) mutable {
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT networkError(humaniseHttpError(reply));
            return;
        }

        const QByteArray body = reply->readAll();
        const QByteArray linkHdr = reply->rawHeader("Link");

        const auto doc = QJsonDocument::fromJson(body);
        if (!doc.isArray()) {
            Q_EMIT networkError(QStringLiteral("Unexpected payload from /user/repos."));
            return;
        }
        for (const auto& v : doc.array()) {
            if (!v.isObject()) continue;
            accumulated.append(parseRepo(v.toObject()));
            if (accumulated.size() >= maxRepos) break;
        }

        const QUrl next = nextPageFromLink(linkHdr);
        if (next.isValid() && accumulated.size() < maxRepos) {
            fetchReposPage(next, std::move(accumulated), maxRepos);
        } else {
            Q_EMIT repositoriesReady(accumulated);
        }
    });
}

void GitHubClient::createRepository(const QString& name,
                                    const QString& description,
                                    bool           isPrivate,
                                    bool           autoInit)
{
    if (token_.isEmpty()) {
        Q_EMIT networkError(QStringLiteral("Not signed in — cannot create a repository."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        Q_EMIT networkError(QStringLiteral("Repository name is required."));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("name"),        name.trimmed());
    body.insert(QStringLiteral("description"), description);
    body.insert(QStringLiteral("private"),     isPrivate);
    body.insert(QStringLiteral("auto_init"),   autoInit);

    const QUrl url(QStringLiteral("%1/user/repos").arg(QString::fromLatin1(kApiBase)));
    postJson(url, QJsonDocument(body).toJson(QJsonDocument::Compact),
        [this](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) {
                Q_EMIT networkError(humaniseHttpError(reply));
                return;
            }
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                Q_EMIT networkError(QStringLiteral(
                    "GitHub returned an unexpected response when creating the repository."));
                return;
            }
            Q_EMIT repositoryCreated(parseRepo(doc.object()));
        });
}

} // namespace ghm::github
