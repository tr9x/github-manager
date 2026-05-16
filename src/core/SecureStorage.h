#pragma once

// SecureStorage - libsecret-backed PAT vault.
//
// Tokens are stored in the user's default keyring under a fixed schema
// ("local.github-manager.Token"). On systems without an org.freedesktop
// Secret Service provider running, every method returns an error and the
// UI surfaces it. There is intentionally NO plaintext fallback.
//
// Each stored token carries a `tokenType` attribute identifying how the
// user authenticated:
//   * Pat   — personal access token (entered manually in LoginDialog)
//   * Oauth — issued by GitHub via the device flow
//   * Unknown — legacy items written before 0.27.0 didn't store the
//               attribute; we read those back as Unknown and treat
//               them optimistically as PAT for UI purposes (since
//               OAuth was added later, anything pre-existing must be
//               PAT). Treating Unknown==Pat keeps existing users from
//               seeing a misleading "OAuth" label.

#include <QString>
#include <optional>

namespace ghm::core {

struct StorageResult {
    bool    ok;
    QString error;  // empty when ok == true

    static StorageResult success() { return {true, {}}; }
    static StorageResult failure(QString msg) { return {false, std::move(msg)}; }
};

enum class TokenType {
    Unknown,  // legacy item without tokenType attribute
    Pat,      // personal access token
    Oauth,    // OAuth device-flow token
};

// Bundle returned by loadToken — the token string plus the type.
// Kept as a separate struct (rather than std::pair) for clarity at
// call sites.
struct LoadedToken {
    QString   token;
    TokenType type{TokenType::Unknown};
};

class SecureStorage {
public:
    SecureStorage();

    // Persist `token` for `username` with the given type. Replaces any
    // prior token for the same username.
    StorageResult saveToken(const QString& username,
                             const QString& token,
                             TokenType type);

    // Returns the stored token + type for `username`, or std::nullopt
    // if none. Items written before 0.27.0 (no tokenType attribute)
    // come back with TokenType::Unknown.
    std::optional<LoadedToken> loadToken(const QString& username,
                                          QString* errorOut = nullptr);

    // Convenience: just the token string. Same as loadToken() but
    // discards the type. Kept so call sites that don't care about
    // type (e.g. push/pull when they just need the credential) stay
    // short.
    std::optional<QString> loadTokenString(const QString& username,
                                            QString* errorOut = nullptr);

    // Removes the stored token for `username` (no error if it didn't exist).
    StorageResult clearToken(const QString& username);
};

} // namespace ghm::core
