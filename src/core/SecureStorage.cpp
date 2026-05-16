#include "core/SecureStorage.h"

#include <libsecret/secret.h>

namespace ghm::core {

namespace {

// Schema describing what we store. Three attributes:
//   * username  — lets multiple GitHub accounts coexist
//   * service   — fixed "github.com", reserved for multi-host support
//   * tokenType — "pat" / "oauth" (added in 0.27.0)
//
// Legacy items written before 0.27.0 don't have tokenType. They're
// still findable because lookup() omits the attribute filter — it
// only fills in `username` and `service`, so libsecret returns any
// item matching those two regardless of tokenType.
const SecretSchema* ghmSchema()
{
    static const SecretSchema schema = {
        "local.github-manager.Token",
        SECRET_SCHEMA_NONE,
        {
            { "username",  SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "service",   SECRET_SCHEMA_ATTRIBUTE_STRING },
            { "tokenType", SECRET_SCHEMA_ATTRIBUTE_STRING },
            { nullptr,     SECRET_SCHEMA_ATTRIBUTE_STRING },
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

const char* tokenTypeToStr(TokenType t)
{
    switch (t) {
        case TokenType::Pat:   return "pat";
        case TokenType::Oauth: return "oauth";
        case TokenType::Unknown: break;
    }
    return "";  // shouldn't happen — saveToken rejects Unknown below
}

TokenType strToTokenType(const QString& s)
{
    if (s == QLatin1String("pat"))   return TokenType::Pat;
    if (s == QLatin1String("oauth")) return TokenType::Oauth;
    return TokenType::Unknown;
}

} // namespace

SecureStorage::SecureStorage() = default;

StorageResult SecureStorage::saveToken(const QString& username,
                                        const QString& token,
                                        TokenType type)
{
    if (username.isEmpty() || token.isEmpty()) {
        return StorageResult::failure(QStringLiteral(
            "username and token must be non-empty"));
    }
    if (type == TokenType::Unknown) {
        // Defensive — refuses to write Unknown so we never end up with
        // ambiguous newly-written items. Existing legacy items still
        // load fine; we just don't WRITE Unknown.
        return StorageResult::failure(QStringLiteral(
            "tokenType must be Pat or Oauth when saving"));
    }

    GError* err = nullptr;
    const QByteArray userUtf8  = username.toUtf8();
    const QByteArray tokenUtf8 = token.toUtf8();
    const QByteArray label     =
        QStringLiteral("GitHub Manager — %1").arg(username).toUtf8();
    const char* typeStr        = tokenTypeToStr(type);

    // We pass tokenType in the attribute list. Because libsecret keys
    // an item by ALL its non-empty attributes, calling store_sync with
    // the same (username, service) but a DIFFERENT tokenType would
    // produce a SECOND item rather than overwriting. To avoid that,
    // we clear any prior item for this (username, service) first —
    // ignoring failures (no prior item is the normal case).
    {
        GError* clearErr = nullptr;
        secret_password_clear_sync(
            ghmSchema(), nullptr, &clearErr,
            "username", userUtf8.constData(),
            "service",  "github.com",
            nullptr);
        if (clearErr) g_error_free(clearErr);  // not our problem
    }

    const gboolean ok = secret_password_store_sync(
        ghmSchema(),
        SECRET_COLLECTION_DEFAULT,
        label.constData(),
        tokenUtf8.constData(),
        nullptr,    // GCancellable
        &err,
        "username",  userUtf8.constData(),
        "service",   "github.com",
        "tokenType", typeStr,
        nullptr);

    if (!ok) {
        return StorageResult::failure(
            QStringLiteral("Failed to store credential: %1").arg(gerrorToQt(err)));
    }
    return StorageResult::success();
}

std::optional<LoadedToken> SecureStorage::loadToken(const QString& username,
                                                      QString* errorOut)
{
    if (username.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("username is empty");
        return std::nullopt;
    }

    // We need both the password AND the tokenType attribute. The
    // simple `secret_password_lookup_sync` only returns the password.
    // For attributes we have to use `secret_service_search_sync`,
    // which returns SecretItem objects we can introspect.
    GError* err = nullptr;
    const QByteArray userUtf8 = username.toUtf8();

    // Build the attribute table the search() API wants — just
    // username + service (NOT tokenType, so legacy items match).
    GHashTable* attrs = g_hash_table_new_full(
        g_str_hash, g_str_equal, g_free, g_free);
    g_hash_table_insert(attrs, g_strdup("username"),
                                g_strdup(userUtf8.constData()));
    g_hash_table_insert(attrs, g_strdup("service"),
                                g_strdup("github.com"));

    SecretService* service = secret_service_get_sync(
        SECRET_SERVICE_NONE, nullptr, &err);
    if (err || !service) {
        if (errorOut) *errorOut = gerrorToQt(err);
        g_hash_table_unref(attrs);
        return std::nullopt;
    }

    GList* items = secret_service_search_sync(
        service, ghmSchema(), attrs,
        static_cast<SecretSearchFlags>(
            SECRET_SEARCH_LOAD_SECRETS | SECRET_SEARCH_UNLOCK),
        nullptr, &err);

    g_hash_table_unref(attrs);
    g_object_unref(service);

    if (err) {
        if (errorOut) *errorOut = gerrorToQt(err);
        if (items) g_list_free_full(items, g_object_unref);
        return std::nullopt;
    }
    if (!items) {
        return std::nullopt;  // no token stored — not an error
    }

    // We expect at most one item for a given (username, service).
    // If somehow more than one ended up there (e.g. via direct
    // keyring manipulation), libsecret returns them in
    // most-recently-stored order — we pick the first.
    auto* item = static_cast<SecretItem*>(items->data);

    LoadedToken out;

    SecretValue* value = secret_item_get_secret(item);
    if (value) {
        gsize len = 0;
        const gchar* raw = secret_value_get(value, &len);
        if (raw) {
            out.token = QString::fromUtf8(raw, static_cast<int>(len));
        }
        secret_value_unref(value);
    }

    // Pull the tokenType attribute. Legacy items lack it → Unknown.
    GHashTable* itemAttrs = secret_item_get_attributes(item);
    if (itemAttrs) {
        const gchar* typeStr = static_cast<const gchar*>(
            g_hash_table_lookup(itemAttrs, "tokenType"));
        if (typeStr) {
            out.type = strToTokenType(QString::fromUtf8(typeStr));
        }
        g_hash_table_unref(itemAttrs);
    }

    g_list_free_full(items, g_object_unref);

    if (out.token.isEmpty()) {
        // Item existed but secret was empty — treat as missing rather
        // than returning an "" token that would confuse callers.
        return std::nullopt;
    }
    return out;
}

std::optional<QString> SecureStorage::loadTokenString(const QString& username,
                                                       QString* errorOut)
{
    auto loaded = loadToken(username, errorOut);
    if (!loaded) return std::nullopt;
    return loaded->token;
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
