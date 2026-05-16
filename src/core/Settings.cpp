#include "core/Settings.h"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QLocale>

namespace ghm::core {

namespace {

QSettings makeSettings()
{
    // Default scope writes to ~/.config/github-manager/GitHub Manager.conf
    return QSettings{};
}

QString defaultClonePath()
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(QStringLiteral("github-manager"));
}

} // namespace

Settings::Settings() = default;

// -- Account ---------------------------------------------------------------

QString Settings::lastUsername() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("user/lastUsername")).toString();
}

void Settings::setLastUsername(const QString& username)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("user/lastUsername"), username);
}

// -- Clone directory -------------------------------------------------------

QString Settings::defaultCloneDirectory() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("paths/cloneDir"), defaultClonePath()).toString();
}

void Settings::setDefaultCloneDirectory(const QString& path)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("paths/cloneDir"), path);
}

// -- Author identity -------------------------------------------------------

QString Settings::authorName() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("git/authorName")).toString();
}

void Settings::setAuthorName(const QString& name)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("git/authorName"), name.trimmed());
}

QString Settings::authorEmail() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("git/authorEmail")).toString();
}

void Settings::setAuthorEmail(const QString& email)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("git/authorEmail"), email.trimmed());
}

bool Settings::hasIdentity() const
{
    return !authorName().isEmpty() && !authorEmail().isEmpty();
}

// -- Local folder workspace ------------------------------------------------

QStringList Settings::localFolders() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("workspace/localFolders")).toStringList();
}

void Settings::setLocalFolders(const QStringList& paths)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("workspace/localFolders"), paths);
}

bool Settings::addLocalFolder(const QString& path)
{
    const QString abs = QDir(path).absolutePath();
    auto folders = localFolders();
    if (folders.contains(abs)) return false;
    folders.append(abs);
    setLocalFolders(folders);
    return true;
}

void Settings::removeLocalFolder(const QString& path)
{
    const QString abs = QDir(path).absolutePath();
    auto folders = localFolders();
    folders.removeAll(abs);
    setLocalFolders(folders);
}

// -- Init defaults ---------------------------------------------------------

QString Settings::defaultInitBranch() const
{
    auto s = makeSettings();
    const QString stored = s.value(QStringLiteral("git/defaultInitBranch"),
                                   QStringLiteral("master")).toString();

    // Defensive: someone could have hand-edited the config or imported
    // a corrupted profile. Branch names making their way into git_init
    // need to be valid refs — fall back to "master" rather than letting
    // libgit2 reject the init with a cryptic error.
    if (stored.isEmpty()) return QStringLiteral("master");
    for (QChar c : stored) {
        const ushort u = c.unicode();
        if (c.isSpace() || u == '~' || u == '^' || u == ':' || u == '?' ||
            u == '*' || u == '[' || u == '\\' || u < 0x20 || u == 0x7F) {
            return QStringLiteral("master");
        }
    }
    if (stored.startsWith(QLatin1Char('-')) ||
        stored.startsWith(QLatin1Char('.')) ||
        stored.endsWith  (QLatin1Char('/')) ||
        stored.endsWith  (QLatin1Char('.')) ||
        stored.contains(QLatin1String("..")) ||
        stored.contains(QLatin1String("@{"))) {
        return QStringLiteral("master");
    }
    return stored;
}

void Settings::setDefaultInitBranch(const QString& branch)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("git/defaultInitBranch"), branch.trimmed());
}

// -- Clone transport preference --------------------------------------------

bool Settings::clonePreferSsh() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("git/clonePreferSsh"), false).toBool();
}

void Settings::setClonePreferSsh(bool prefer)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("git/clonePreferSsh"), prefer);
}

// -- Commit signing --------------------------------------------------------

Settings::SigningMode Settings::signingModeFromString(const QString& s)
{
    if (s == QLatin1String("gpg")) return SigningMode::Gpg;
    if (s == QLatin1String("ssh")) return SigningMode::Ssh;
    return SigningMode::None;
}

QString Settings::signingModeToString(SigningMode m)
{
    switch (m) {
        case SigningMode::Gpg: return QStringLiteral("gpg");
        case SigningMode::Ssh: return QStringLiteral("ssh");
        case SigningMode::None: break;
    }
    return QStringLiteral("none");
}

Settings::SigningMode Settings::signingMode() const
{
    auto s = makeSettings();
    return signingModeFromString(
        s.value(QStringLiteral("git/signingMode"),
                QStringLiteral("none")).toString());
}

void Settings::setSigningMode(SigningMode m)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("git/signingMode"), signingModeToString(m));
}

QString Settings::signingKey() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("git/signingKey")).toString();
}

void Settings::setSigningKey(const QString& key)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("git/signingKey"), key.trimmed());
}

// -- Language --------------------------------------------------------------

QString Settings::language() const
{
    auto s = makeSettings();
    const QString stored = s.value(QStringLiteral("ui/language")).toString();
    if (!stored.isEmpty() && supportedLanguages().contains(stored)) {
        return stored;
    }
    return defaultLanguage();
}

void Settings::setLanguage(const QString& code)
{
    auto s = makeSettings();
    if (supportedLanguages().contains(code)) {
        s.setValue(QStringLiteral("ui/language"), code);
    }
}

QString Settings::defaultLanguage()
{
    // Pick Polish if the user's system locale is Polish; otherwise English.
    // QLocale::system().name() returns "pl_PL", "en_US", etc.
    const QString sys = QLocale::system().name();
    if (sys.startsWith(QStringLiteral("pl"), Qt::CaseInsensitive)) {
        return QStringLiteral("pl");
    }
    return QStringLiteral("en");
}

QStringList Settings::supportedLanguages()
{
    return {
        QStringLiteral("en"),
        QStringLiteral("pl"),
        QStringLiteral("de"),
        QStringLiteral("es"),
        QStringLiteral("fr"),
    };
}

QString Settings::languageDisplayName(const QString& code)
{
    if (code == QLatin1String("pl")) return QStringLiteral("Polski");
    if (code == QLatin1String("en")) return QStringLiteral("English");
    if (code == QLatin1String("de")) return QStringLiteral("Deutsch");
    if (code == QLatin1String("es")) return QStringLiteral("Español");
    if (code == QLatin1String("fr")) return QStringLiteral("Français");
    return code;
}

// -- Trusted TLS fingerprints ----------------------------------------------

namespace {
// QSettings allows arbitrary keys but slashes are interpreted as
// group separators. Hostnames don't contain slashes but we sanitize
// defensively anyway — colons (port suffix like host:8443) become
// underscores too because some keyring backends mangle them.
QString sanitizeHostKey(const QString& host)
{
    QString s = host.trimmed().toLower();
    s.replace(QLatin1Char('/'), QLatin1Char('_'));
    s.replace(QLatin1Char(':'), QLatin1Char('_'));
    return s;
}
} // anonymous namespace

QString Settings::trustedTlsFingerprint(const QString& host) const
{
    if (host.isEmpty()) return {};
    auto s = makeSettings();
    return s.value(QStringLiteral("trustedTlsFingerprints/%1")
                     .arg(sanitizeHostKey(host)))
            .toString().toLower();
}

void Settings::setTrustedTlsFingerprint(const QString& host,
                                          const QString& sha256Hex)
{
    if (host.isEmpty() || sha256Hex.isEmpty()) return;
    auto s = makeSettings();
    s.setValue(QStringLiteral("trustedTlsFingerprints/%1")
                 .arg(sanitizeHostKey(host)),
               sha256Hex.toLower());
}

void Settings::clearTrustedTlsFingerprint(const QString& host)
{
    if (host.isEmpty()) return;
    auto s = makeSettings();
    s.remove(QStringLiteral("trustedTlsFingerprints/%1")
                .arg(sanitizeHostKey(host)));
}

QList<QPair<QString, QString>> Settings::allTrustedTlsFingerprints() const
{
    QList<QPair<QString, QString>> out;
    auto s = makeSettings();
    s.beginGroup(QStringLiteral("trustedTlsFingerprints"));
    // childKeys() returns the leaf key names (sanitized hosts) in the
    // current group. We iterate, fetch each value, and pair them.
    const QStringList keys = s.childKeys();
    for (const QString& k : keys) {
        const QString fp = s.value(k).toString().toLower();
        if (fp.isEmpty()) continue;  // defensive — shouldn't happen
        out.append({k, fp});
    }
    s.endGroup();
    // Sort alphabetically by host for stable display.
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) {
                  return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
              });
    return out;
}

// -- Submodule key memory --------------------------------------------------

namespace {
// Build a QSettings key from (parentPath, submoduleName). Both
// components can contain '/' (parentPath is an absolute filesystem
// path; submoduleName can be a nested submodule path). We use '\'
// instead of '/' (QSettings reserves '/' as group separator), and
// '\\' as the separator between the two parts. Reversible: split
// the key on the FIRST '\\', then in each half replace '\' → '/'.
QString submoduleKeyId(const QString& parentPath, const QString& submoduleName)
{
    QString p = parentPath;
    QString s = submoduleName;
    p.replace(QLatin1Char('/'), QLatin1Char('\\'));
    s.replace(QLatin1Char('/'), QLatin1Char('\\'));
    return p + QStringLiteral("\\\\") + s;
}

// Reverse of submoduleKeyId. Returns {parentPath, submoduleName}
// or {} if the key doesn't have the expected separator (corrupt
// settings; we just skip those).
QPair<QString, QString> parseSubmoduleKeyId(const QString& id)
{
    const int sepIdx = id.indexOf(QStringLiteral("\\\\"));
    if (sepIdx < 0) return {};
    QString parent = id.left(sepIdx);
    QString sub    = id.mid(sepIdx + 2);
    parent.replace(QLatin1Char('\\'), QLatin1Char('/'));
    sub.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return {parent, sub};
}
} // anonymous namespace

QString Settings::rememberedSubmoduleKey(const QString& parentPath,
                                           const QString& submoduleName) const
{
    if (parentPath.isEmpty() || submoduleName.isEmpty()) return {};
    auto s = makeSettings();
    return s.value(QStringLiteral("submoduleKeys/%1")
                     .arg(submoduleKeyId(parentPath, submoduleName)))
            .toString();
}

void Settings::setRememberedSubmoduleKey(const QString& parentPath,
                                           const QString& submoduleName,
                                           const QString& keyPath)
{
    if (parentPath.isEmpty() || submoduleName.isEmpty() || keyPath.isEmpty()) {
        return;
    }
    auto s = makeSettings();
    s.setValue(QStringLiteral("submoduleKeys/%1")
                 .arg(submoduleKeyId(parentPath, submoduleName)),
               keyPath);
}

void Settings::clearRememberedSubmoduleKey(const QString& parentPath,
                                             const QString& submoduleName)
{
    if (parentPath.isEmpty() || submoduleName.isEmpty()) return;
    auto s = makeSettings();
    s.remove(QStringLiteral("submoduleKeys/%1")
                .arg(submoduleKeyId(parentPath, submoduleName)));
}

QList<Settings::RememberedKeyEntry> Settings::allRememberedSubmoduleKeys() const
{
    QList<RememberedKeyEntry> out;
    auto s = makeSettings();
    s.beginGroup(QStringLiteral("submoduleKeys"));
    const QStringList keys = s.childKeys();
    for (const QString& k : keys) {
        const QString keyPath = s.value(k).toString();
        if (keyPath.isEmpty()) continue;
        const auto [parent, sub] = parseSubmoduleKeyId(k);
        if (parent.isEmpty() || sub.isEmpty()) continue;
        out.append({parent, sub, keyPath});
    }
    s.endGroup();
    // Sort by parent path then submodule name for stable display.
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) {
                  int cmp = a.parentPath.compare(b.parentPath, Qt::CaseInsensitive);
                  if (cmp != 0) return cmp < 0;
                  return a.submoduleName.compare(b.submoduleName, Qt::CaseInsensitive) < 0;
              });
    return out;
}

// -- Window state ----------------------------------------------------------

QByteArray Settings::mainWindowGeometry() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("window/geometry")).toByteArray();
}

void Settings::setMainWindowGeometry(const QByteArray& geom)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("window/geometry"), geom);
}

QByteArray Settings::mainWindowState() const
{
    auto s = makeSettings();
    return s.value(QStringLiteral("window/state")).toByteArray();
}

void Settings::setMainWindowState(const QByteArray& state)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("window/state"), state);
}

} // namespace ghm::core
