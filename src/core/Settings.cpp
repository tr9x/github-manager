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
    return s.value(QStringLiteral("git/defaultInitBranch"),
                   QStringLiteral("master")).toString();
}

void Settings::setDefaultInitBranch(const QString& branch)
{
    auto s = makeSettings();
    s.setValue(QStringLiteral("git/defaultInitBranch"), branch.trimmed());
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
    return { QStringLiteral("en"), QStringLiteral("pl") };
}

QString Settings::languageDisplayName(const QString& code)
{
    if (code == QLatin1String("pl")) return QStringLiteral("Polski");
    if (code == QLatin1String("en")) return QStringLiteral("English");
    return code;
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
