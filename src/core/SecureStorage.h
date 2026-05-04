#pragma once

// SecureStorage - libsecret-backed PAT vault.
//
// Tokens are stored in the user's default keyring under a fixed schema
// ("local.github-manager.Token"). On systems without an org.freedesktop
// Secret Service provider running, every method returns an error and the
// UI surfaces it. There is intentionally NO plaintext fallback.

#include <QString>
#include <optional>

namespace ghm::core {

struct StorageResult {
    bool    ok;
    QString error;  // empty when ok == true

    static StorageResult success() { return {true, {}}; }
    static StorageResult failure(QString msg) { return {false, std::move(msg)}; }
};

class SecureStorage {
public:
    SecureStorage();

    // Persist `token` for `username`. Replaces any prior token for the
    // same username.
    StorageResult saveToken(const QString& username, const QString& token);

    // Returns the stored token for `username`, or std::nullopt if none.
    // The returned string is held in a QString that callers should clear
    // when no longer needed.
    std::optional<QString> loadToken(const QString& username, QString* errorOut = nullptr);

    // Removes the stored token for `username` (no error if it didn't exist).
    StorageResult clearToken(const QString& username);
};

} // namespace ghm::core
