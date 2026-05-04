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

    // -- Window geometry / state ------------------------------------------

    QByteArray mainWindowGeometry() const;
    void       setMainWindowGeometry(const QByteArray& geom);

    QByteArray mainWindowState() const;
    void       setMainWindowState(const QByteArray& state);
};

} // namespace ghm::core
