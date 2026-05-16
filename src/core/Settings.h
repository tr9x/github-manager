#pragma once

// Settings - thin wrapper around QSettings for non-secret preferences.
//
// Stores things like the last-known username (for display before secrets
// are unlocked), default clone directory, the user's git author identity
// for commits, and the list of local folders the user has added to the
// sidebar. The PAT itself NEVER goes through here — see SecureStorage.

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QPair>

namespace ghm::core {

class Settings {
public:
    Settings();

    // -- Account ----------------------------------------------------------

    QString lastUsername() const;
    void    setLastUsername(const QString& username);

    // -- Clone directory --------------------------------------------------

    QString defaultCloneDirectory() const;
    void    setDefaultCloneDirectory(const QString& path);

    // -- Git author identity (used for commits) ---------------------------
    //
    // Both must be set before the first commit; the UI prompts for them
    // lazily via IdentityDialog.

    QString authorName() const;
    void    setAuthorName(const QString& name);

    QString authorEmail() const;
    void    setAuthorEmail(const QString& email);

    bool    hasIdentity() const;

    // -- Local folder workspace -------------------------------------------
    //
    // Folders the user has explicitly added to the sidebar. Stored as
    // absolute paths. Order is preserved.

    QStringList localFolders() const;
    void        setLocalFolders(const QStringList& paths);
    bool        addLocalFolder(const QString& path);     // dedups; true = added
    void        removeLocalFolder(const QString& path);

    // -- Init defaults ----------------------------------------------------

    QString defaultInitBranch() const;                   // default "master"
    void    setDefaultInitBranch(const QString& branch);

    // -- Clone transport preference --------------------------------------
    //
    // When true, the clone-from-GitHub flow rewrites the HTTPS clone URL
    // returned by the REST API into its SCP-style SSH equivalent
    // (`git@github.com:owner/repo.git`) before handing it to libgit2.
    // SSH auth uses ssh-agent on Linux; the user must have a working
    // SSH key registered with their GitHub account.
    //
    // Defaults to false — HTTPS-with-PAT is the lower-friction path
    // for new users, and pre-existing remotes (already in .git/config)
    // are respected regardless of this setting.
    bool    clonePreferSsh() const;
    void    setClonePreferSsh(bool prefer);

    // -- Commit signing --------------------------------------------------
    //
    // Three modes:
    //   * "none"  → no signing (the default, matches git's default)
    //   * "gpg"   → sign with GPG. `signingKey()` is the GPG key ID
    //               (e.g. "0x1234ABCD") or full fingerprint
    //   * "ssh"   → sign with SSH (git 2.34+ style). `signingKey()`
    //               is a path to the SSH key file or its public-key
    //               content (per `ssh-keygen -Y sign -f <key>`)
    //
    // We deliberately don't auto-detect from git config — explicit
    // settings make failures easier to diagnose ("you said sign with
    // GPG but gpg isn't installed" rather than mysterious behavior).
    //
    // When the mode is "none", every commit goes out unsigned regardless
    // of any other setting. When set, every commit through this app is
    // signed; if signing fails, the commit fails (we don't silently
    // fall back to unsigned, that would be surprising and risky).

    enum class SigningMode {
        None,
        Gpg,
        Ssh,
    };
    static SigningMode signingModeFromString(const QString& s);
    static QString     signingModeToString  (SigningMode m);

    SigningMode signingMode() const;
    void        setSigningMode(SigningMode m);

    // Key ID (GPG) or key path (SSH). Empty when mode == None or
    // when the user hasn't configured one yet. The signing op will
    // fail with a useful error in that case rather than silently
    // signing with whatever GPG/SSH thinks the default key is —
    // explicit beats implicit.
    QString signingKey() const;
    void    setSigningKey(const QString& key);

    // -- Language --------------------------------------------------------
    //
    // Two-letter language code: "en" or "pl". When unset, defaultLanguage()
    // is returned, which respects the system locale (Polish system → "pl").

    QString language() const;
    void    setLanguage(const QString& code);

    static QString defaultLanguage();
    static QStringList supportedLanguages();   // {"en", "pl", "de", "es", "fr"}
    static QString    languageDisplayName(const QString& code);  // "English" / "Polski" / "Deutsch" / "Español" / "Français"

    // -- Trusted TLS certificate fingerprints ---------------------------
    //
    // For HTTPS connections to git servers with certs that don't
    // validate against the system trust store (self-signed, internal
    // CAs, etc.), we let the user manually approve the cert and
    // remember its SHA-256 fingerprint. Subsequent connections to the
    // same host with the same fingerprint proceed silently.
    //
    // Fingerprint is stored as lowercase hex, no separators
    // ("a1b2c3..."). One per host: re-saving overwrites the previous
    // entry. If a server rotates its cert legitimately, the prompt
    // re-fires and the user confirms again; if MITM tampering causes
    // a fingerprint change, the prompt is the warning.
    QString trustedTlsFingerprint(const QString& host) const;
    void    setTrustedTlsFingerprint(const QString& host,
                                       const QString& sha256Hex);
    void    clearTrustedTlsFingerprint(const QString& host);

    // Returns all trusted host → fingerprint pairs. Host names in
    // the returned list have been "desanitized" — slashes and
    // colons that were converted to underscores for QSettings key
    // safety stay underscored here, since we have no way to
    // reverse-map (which underscore came from what original char?).
    // For the typical case (host-only, no port suffix), the
    // returned host matches what the user saw in the approval
    // dialog. Used by TrustedServersDialog to populate its table.
    QList<QPair<QString, QString>> allTrustedTlsFingerprints() const;

    // -- Remembered SSH keys for submodules -----------------------------
    //
    // After the user explicitly picks a private key for a submodule
    // init/update via SshKeyDialog (0.29.0), we remember that
    // (parent-repo, submodule-name) → keyPath mapping so subsequent
    // operations on the same submodule reuse the same key without
    // re-prompting.
    //
    // We ONLY persist the keyPath, never the passphrase. Encrypted
    // keys still need a passphrase prompt each operation — passphrase
    // persistence would require either plaintext-on-disk (bad) or
    // an OS keyring entry per key (more work; defer).
    //
    // Mapping key format in QSettings:
    //   submoduleKeys/<sanitized_parent>/<sanitized_submodule>=<keyPath>
    QString rememberedSubmoduleKey(const QString& parentPath,
                                     const QString& submoduleName) const;
    void    setRememberedSubmoduleKey(const QString& parentPath,
                                        const QString& submoduleName,
                                        const QString& keyPath);
    void    clearRememberedSubmoduleKey(const QString& parentPath,
                                          const QString& submoduleName);

    // Used by RememberedKeysDialog to populate its table. Returns
    // tuples of (parent_path_display, submodule_name, key_path).
    // The parent path is shown as the original absolute path when
    // we can recover it; sanitized fragments otherwise (similar
    // trade-off as trusted TLS fingerprints — see those docs).
    struct RememberedKeyEntry {
        QString parentPath;
        QString submoduleName;
        QString keyPath;
    };
    QList<RememberedKeyEntry> allRememberedSubmoduleKeys() const;

    // -- Window geometry / state ------------------------------------------

    QByteArray mainWindowGeometry() const;
    void       setMainWindowGeometry(const QByteArray& geom);

    QByteArray mainWindowState() const;
    void       setMainWindowState(const QByteArray& state);
};

} // namespace ghm::core
