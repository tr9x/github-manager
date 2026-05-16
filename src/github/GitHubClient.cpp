#include "github/GitHubClient.h"
#include "github/LinkHeaderParser.h"
#include "github/OAuthResponseParser.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QRegularExpression>
#include <QSslError>
#include <QSslCertificate>
#include <QBuffer>
#include <QSslConfiguration>
#include <QCryptographicHash>

#include "ui/TlsCertApprover.h"

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

// Link-header parsing moved to github/LinkHeaderParser.h so unit
// tests can exercise it without instantiating the network stack.
// We use the QByteArray overload below at the call site.

Repository parseRepo(const QJsonObject& o)
{
    Repository r;
    r.name          = o.value(QStringLiteral("name")).toString();
    r.fullName      = o.value(QStringLiteral("full_name")).toString();
    r.description   = o.value(QStringLiteral("description")).toString();
    r.cloneUrl      = o.value(QStringLiteral("clone_url")).toString();
    r.sshUrl        = o.value(QStringLiteral("ssh_url")).toString();
    r.htmlUrl       = o.value(QStringLiteral("html_url")).toString();
    r.defaultBranch = o.value(QStringLiteral("default_branch")).toString();
    r.primaryLanguage = o.value(QStringLiteral("language")).toString();
    r.isPrivate     = o.value(QStringLiteral("private")).toBool();
    r.isFork        = o.value(QStringLiteral("fork")).toBool();
    r.isArchived    = o.value(QStringLiteral("archived")).toBool();
    r.sizeKb        = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble());
    r.stargazers    = o.value(QStringLiteral("stargazers_count")).toInt();
    r.forks         = o.value(QStringLiteral("forks_count")).toInt();
    r.openIssues    = o.value(QStringLiteral("open_issues_count")).toInt();
    r.watchers      = o.value(QStringLiteral("watchers_count")).toInt();

    const QString updated = o.value(QStringLiteral("updated_at")).toString();
    r.updatedAt = QDateTime::fromString(updated, Qt::ISODate);
    const QString pushed = o.value(QStringLiteral("pushed_at")).toString();
    r.pushedAt = QDateTime::fromString(pushed, Qt::ISODate);

    // Topics live under `topics` (array of strings). May be absent
    // in older API versions or for some endpoints; default to empty
    // is fine.
    const auto topicsArr = o.value(QStringLiteral("topics")).toArray();
    for (const auto& t : topicsArr) {
        r.topics.append(t.toString());
    }
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
{
    // SSL error handling. By default, QNAM aborts requests when the
    // peer cert doesn't validate against the system trust store. To
    // support self-signed / internal-CA / MITM-proxy users we route
    // SSL errors through TlsCertApprover (same approver used by the
    // libgit2 cert callback). Already-trusted fingerprints proceed
    // silently; unknown ones pop the approval dialog inline. The
    // request stays paused for the duration of the dialog —
    // acceptable because dialog latency is bounded by user action.
    connect(nam_, &QNetworkAccessManager::sslErrors,
            this, [this](QNetworkReply* reply,
                          const QList<QSslError>& errors) {
        handleSslErrors(reply, errors);
    });
}

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

void GitHubClient::patchJson(const QUrl& url,
                              const QByteArray& body,
                              std::function<void(QNetworkReply*)> onFinished)
{
    QNetworkRequest req = makeAuthedRequest(url, token_);
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    // QNAM has no patch() — use sendCustomRequest with the method
    // verb explicitly. The body has to be wrapped in a QBuffer that
    // outlives the reply; we let the lambda own it so it gets
    // cleaned up alongside the reply itself.
    auto* buf = new QBuffer;
    buf->setData(body);
    buf->open(QIODevice::ReadOnly);
    QNetworkReply* reply = nam_->sendCustomRequest(req, "PATCH", buf);
    buf->setParent(reply);  // tie buffer lifetime to the reply
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
                                    bool           autoInit,
                                    const QString& licenseTemplate,
                                    const QString& gitignoreTemplate)
{
    if (token_.isEmpty()) {
        Q_EMIT networkError(QStringLiteral("Not signed in — cannot create a repository."));
        return;
    }
    if (name.trimmed().isEmpty()) {
        Q_EMIT networkError(QStringLiteral("Repository name is required."));
        return;
    }

    // License / gitignore templates force auto_init: GitHub creates
    // the LICENSE / .gitignore as part of the initial commit, so
    // there must BE an initial commit. Upgrade silently if caller
    // didn't ask for auto_init explicitly.
    const bool needsInit = autoInit ||
                            !licenseTemplate.isEmpty() ||
                            !gitignoreTemplate.isEmpty();

    QJsonObject body;
    body.insert(QStringLiteral("name"),        name.trimmed());
    body.insert(QStringLiteral("description"), description);
    body.insert(QStringLiteral("private"),     isPrivate);
    body.insert(QStringLiteral("auto_init"),   needsInit);
    if (!licenseTemplate.isEmpty()) {
        body.insert(QStringLiteral("license_template"), licenseTemplate);
    }
    if (!gitignoreTemplate.isEmpty()) {
        body.insert(QStringLiteral("gitignore_template"), gitignoreTemplate);
    }

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

void GitHubClient::updateRepositoryVisibility(const QString& fullName,
                                                bool           makePrivate)
{
    if (token_.isEmpty()) {
        Q_EMIT networkError(QStringLiteral("Not signed in — cannot change visibility."));
        return;
    }
    if (fullName.isEmpty() || !fullName.contains('/')) {
        Q_EMIT networkError(QStringLiteral(
            "Invalid repository name — expected owner/repo."));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("private"), makePrivate);

    // Endpoint: PATCH /repos/{owner}/{repo}
    const QUrl url(QStringLiteral("%1/repos/%2")
                     .arg(QString::fromLatin1(kApiBase), fullName));
    patchJson(url, QJsonDocument(body).toJson(QJsonDocument::Compact),
        [this](QNetworkReply* reply) {
            if (reply->error() != QNetworkReply::NoError) {
                Q_EMIT networkError(humaniseHttpError(reply));
                return;
            }
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isObject()) {
                Q_EMIT networkError(QStringLiteral(
                    "GitHub returned an unexpected response when "
                    "updating the repository."));
                return;
            }
            Q_EMIT visibilityChanged(parseRepo(doc.object()));
        });
}

// ----- Repository detail endpoints -----------------------------------------

void GitHubClient::fetchReadme(const QString& fullName)
{
    if (token_.isEmpty() || fullName.isEmpty()) return;

    const QUrl url(QStringLiteral("%1/repos/%2/readme")
                     .arg(QString::fromLatin1(kApiBase), fullName));
    getJson(url, [this, fullName](QNetworkReply* reply) {
        const int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            // Not an error — many repos legitimately lack a README.
            Q_EMIT readmeNotFound(fullName);
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT networkError(humaniseHttpError(reply));
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            Q_EMIT networkError(QStringLiteral(
                "GitHub returned an unexpected README response."));
            return;
        }
        const auto obj = doc.object();
        // GitHub returns content base64-encoded with newlines every
        // 60 chars (RFC 2045). QByteArray::fromBase64 handles the
        // newlines transparently in default decode mode.
        const QString b64 = obj.value(QStringLiteral("content")).toString();
        const auto bytes = QByteArray::fromBase64(b64.toLatin1());
        const QString markdown = QString::fromUtf8(bytes);
        Q_EMIT readmeFetched(fullName, markdown);
    });
}

void GitHubClient::fetchContents(const QString& fullName, const QString& path)
{
    if (token_.isEmpty() || fullName.isEmpty()) return;

    // Path is URL-encoded but slashes inside the path are part of the
    // resource hierarchy, so we pass the path as-is and trust GitHub
    // to interpret it. Empty path = root listing.
    QString urlStr = QStringLiteral("%1/repos/%2/contents")
                       .arg(QString::fromLatin1(kApiBase), fullName);
    if (!path.isEmpty()) {
        urlStr += QLatin1Char('/');
        urlStr += path;
    }
    const QUrl url(urlStr);
    getJson(url, [this, fullName, path](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT networkError(humaniseHttpError(reply));
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        QList<ContentEntry> entries;
        // Endpoint returns array for dirs, single object for files.
        // We only call it on dirs (UI doesn't fetch single-file blobs
        // through this path), so we expect an array. If we get a
        // single object, fold it into a one-element list defensively.
        QJsonArray arr;
        if (doc.isArray()) {
            arr = doc.array();
        } else if (doc.isObject()) {
            arr.append(doc.object());
        }
        for (const auto& v : arr) {
            if (!v.isObject()) continue;
            const auto o = v.toObject();
            ContentEntry e;
            e.name        = o.value(QStringLiteral("name")).toString();
            e.type        = o.value(QStringLiteral("type")).toString();
            e.size        = static_cast<qint64>(
                              o.value(QStringLiteral("size")).toDouble());
            e.htmlUrl     = o.value(QStringLiteral("html_url")).toString();
            e.downloadUrl = o.value(QStringLiteral("download_url")).toString();
            entries.append(e);
        }
        // Sort: dirs first (alpha), then files (alpha). Matches the
        // convention every file manager and the github.com web UI
        // use; users expect it.
        std::sort(entries.begin(), entries.end(),
                  [](const ContentEntry& a, const ContentEntry& b) {
                      const bool aDir = a.type == QLatin1String("dir");
                      const bool bDir = b.type == QLatin1String("dir");
                      if (aDir != bDir) return aDir;  // dirs first
                      return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                  });
        Q_EMIT contentsFetched(fullName, path, entries);
    });
}

void GitHubClient::fetchLanguages(const QString& fullName)
{
    if (token_.isEmpty() || fullName.isEmpty()) return;

    const QUrl url(QStringLiteral("%1/repos/%2/languages")
                     .arg(QString::fromLatin1(kApiBase), fullName));
    getJson(url, [this, fullName](QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT networkError(humaniseHttpError(reply));
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            Q_EMIT networkError(QStringLiteral(
                "GitHub returned an unexpected languages response."));
            return;
        }
        // Returned shape: {"Python": 12345, "C": 6789, ...}
        // We keep ALL languages — the UI decides how to display them.
        QMap<QString, qint64> bytesByLang;
        const auto obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            bytesByLang.insert(it.key(),
                                static_cast<qint64>(it.value().toDouble()));
        }
        Q_EMIT languagesFetched(fullName, bytesByLang);
    });
}

// ----- OAuth device flow ---------------------------------------------------

// GitHub's device flow endpoints. These are FIXED — not parameterised by
// kApiBase — because they live at github.com, not api.github.com.
constexpr const char* kDeviceCodeUrl   = "https://github.com/login/device/code";
constexpr const char* kAccessTokenUrl  = "https://github.com/login/oauth/access_token";

void GitHubClient::startDeviceFlow(const QString& clientId, const QString& scope)
{
    if (clientId.isEmpty()) {
        Q_EMIT oauthError(tr("OAuth is not configured for this build "
                             "(no client_id). Use a Personal Access Token instead."));
        return;
    }

    // Form-encoded POST, not JSON. GitHub's device-flow endpoint is
    // the only place in our API surface that uses form encoding; the
    // rest of the API speaks JSON.
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId);
    form.addQueryItem(QStringLiteral("scope"),     scope);
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkRequest req((QUrl(QString::fromLatin1(kDeviceCodeUrl))));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Accept", "application/json");

    QNetworkReply* reply = nam_->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto bodyBytes = reply->readAll();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT oauthError(humaniseHttpError(reply));
            return;
        }
        const auto r = parseDeviceCodeResponse(bodyBytes);
        if (!r.ok) {
            Q_EMIT oauthError(r.error);
            return;
        }
        Q_EMIT deviceCodeReceived(r.userCode, r.verificationUri,
                                  r.deviceCode, r.expiresInSeconds,
                                  r.pollIntervalSeconds);
    });
}

void GitHubClient::pollAccessToken(const QString& clientId,
                                    const QString& deviceCode)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"),    clientId);
    form.addQueryItem(QStringLiteral("device_code"),  deviceCode);
    form.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8();

    QNetworkRequest req((QUrl(QString::fromLatin1(kAccessTokenUrl))));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setRawHeader("Accept", "application/json");

    QNetworkReply* reply = nam_->post(req, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const auto bodyBytes = reply->readAll();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // HTTP-level error (network down, 5xx) — caller stops
            // polling. We don't retry network errors here because
            // the OAuthFlowController has its own state and a
            // retry-friendly user-visible UI.
            Q_EMIT oauthError(humaniseHttpError(reply));
            return;
        }
        const auto r = parseAccessTokenResponse(bodyBytes);
        switch (r.state) {
            case AccessTokenResponse::State::Success:
                Q_EMIT accessTokenReceived(r.accessToken, r.scope);
                return;
            case AccessTokenResponse::State::Pending:
                Q_EMIT pollPending();
                return;
            case AccessTokenResponse::State::SlowDown:
                Q_EMIT pollSlowDown();
                return;
            case AccessTokenResponse::State::Error:
                Q_EMIT oauthError(r.error);
                return;
        }
    });
}

void GitHubClient::handleSslErrors(QNetworkReply* reply,
                                     const QList<QSslError>& errors)
{
    if (!reply) return;

    // Pull the peer cert. If we somehow don't have one (cert errors
    // BEFORE the handshake completed?) we can't compute a fingerprint
    // and must let the request fail.
    const QSslCertificate peerCert =
        reply->sslConfiguration().peerCertificate();
    if (peerCert.isNull()) {
        // No cert to evaluate — leave the request to fail with the
        // original error.
        return;
    }

    const QString host = reply->url().host();
    const QByteArray der = peerCert.toDer();
    const QString sha256Hex = QString::fromLatin1(
        QCryptographicHash::hash(der, QCryptographicHash::Sha256)
            .toHex().toLower());

    auto* approver = ghm::ui::TlsCertApprover::instance();
    if (!approver) {
        // Headless / test mode — no approver registered. Fail safe.
        return;
    }

    // Already-trusted fingerprint? Proceed silently.
    if (approver->isFingerprintTrusted(host, sha256Hex)) {
        reply->ignoreSslErrors(errors);
        return;
    }

    // Pop the approval dialog inline. We're on the GUI thread (QNAM
    // signals fire there) so no invokeMethod needed.
    const bool approved = approver->requestApproval(host, der);
    if (approved) {
        reply->ignoreSslErrors(errors);
    }
    // If rejected, do nothing — the request continues with its
    // SSL errors and QNAM will fail it with the original error.
}

} // namespace ghm::github
