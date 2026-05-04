#include "core/SecureStorage.h"

#include <libsecret/secret.h>

namespace ghm::core {

namespace {

// Schema describing what we store. The "username" attribute lets multiple
// GitHub accounts coexist in the same keyring.
const SecretSchema* ghmSchema()
{
    static const SecretSchema schema = {
        "local.github-manager.Token",
        SECRET_SCHEMA_NONE,
        {
            { "username", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "service",  SECRET_SCHEMA_ATTRIBUTE_STRING },
            { nullptr,    SECRET_SCHEMA_ATTRIBUTE_STRING },
        },
        // padding
        0, 0, 0, 0, 0, 0, 0, 0,
    };
    return &schema;
}

QString gerrorToQt(GError* err)
{
    if (!err) return {};
    QString msg = QString::fromUtf8(err->message ? err->message : "unknown error");
    g_error_free(err);
    return msg;
}

} // namespace

SecureStorage::SecureStorage() = default;

StorageResult SecureStorage::saveToken(const QString& username, const QString& token)
{
    if (username.isEmpty() || token.isEmpty()) {
        return StorageResult::failure(QStringLiteral("username and token must be non-empty"));
    }

    GError* err = nullptr;
    const QByteArray userUtf8  = username.toUtf8();
    const QByteArray tokenUtf8 = token.toUtf8();
    const QByteArray label     = QStringLiteral("GitHub Manager — %1").arg(username).toUtf8();

    const gboolean ok = secret_password_store_sync(
        ghmSchema(),
        SECRET_COLLECTION_DEFAULT,
        label.constData(),
        tokenUtf8.constData(),
        nullptr,    // GCancellable
        &err,
        "username", userUtf8.constData(),
        "service",  "github.com",
        nullptr);

    if (!ok) {
        return StorageResult::failure(
            QStringLiteral("Failed to store credential: %1").arg(gerrorToQt(err)));
    }
    return StorageResult::success();
}

std::optional<QString> SecureStorage::loadToken(const QString& username, QString* errorOut)
{
    if (username.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("username is empty");
        return std::nullopt;
    }

    GError* err = nullptr;
    const QByteArray userUtf8 = username.toUtf8();

    gchar* secret = secret_password_lookup_sync(
        ghmSchema(),
        nullptr,
        &err,
        "username", userUtf8.constData(),
        "service",  "github.com",
        nullptr);

    if (err) {
        if (errorOut) *errorOut = gerrorToQt(err);
        return std::nullopt;
    }
    if (!secret) {
        return std::nullopt;  // no token stored — not an error
    }

    QString token = QString::fromUtf8(secret);
    secret_password_free(secret);
    return token;
}

StorageResult SecureStorage::clearToken(const QString& username)
{
    if (username.isEmpty()) {
        return StorageResult::failure(QStringLiteral("username is empty"));
    }

    GError* err = nullptr;
    const QByteArray userUtf8 = username.toUtf8();

    secret_password_clear_sync(
        ghmSchema(),
        nullptr,
        &err,
        "username", userUtf8.constData(),
        "service",  "github.com",
        nullptr);

    if (err) {
        return StorageResult::failure(
            QStringLiteral("Failed to clear credential: %1").arg(gerrorToQt(err)));
    }
    return StorageResult::success();
}

} // namespace ghm::core
