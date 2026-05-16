#include "core/Translator.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace ghm::core {

Translator::Translator(QObject* parent)
    : QTranslator(parent)
    , lang_(QStringLiteral("en"))
{
}

namespace {

// Walks the Qt resource tree under `:/i18n` and `:/translations`
// and prints what's actually there. Diagnostic only — runs at most
// once per setLanguage failure to figure out where qt_add_translations
// actually deposited the .qm files. Different Qt versions and CMake
// flavours of qt_add_translations have put them under different
// prefixes (RESOURCE_PREFIX, the project name, or just "/").
void dumpResourceTree()
{
    static bool dumped = false;
    if (dumped) return;
    dumped = true;

    qWarning() << "Translator: dumping Qt resource tree to help locate .qm files…";
    const QStringList roots = {
        QStringLiteral(":/"),
        QStringLiteral(":/i18n"),
        QStringLiteral(":/translations"),
        QStringLiteral(":/i18n/translations"),
        QStringLiteral(":/qt-project.org"),
    };
    for (const QString& root : roots) {
        QDir d(root);
        if (!d.exists()) {
            qWarning().noquote() << "  " << root << "(does not exist)";
            continue;
        }
        const auto entries = d.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        qWarning().noquote() << "  " << root << ":" << entries.join(QStringLiteral(", "));
    }
}

} // namespace

bool Translator::setLanguage(const QString& code)
{
    // "en" or any unrecognised value → no translator, Qt uses source.
    // We still record what the user picked so the menu's checkmark
    // can reflect it, and so a later call to setLanguage("en") after
    // "pl" cleanly returns to English without leaving the previous
    // .qm loaded in our QTranslator instance.
    lang_ = code.isEmpty() ? QStringLiteral("en") : code;

    if (lang_ == QLatin1String("en")) {
        // Empty load — clears any previously-loaded data. QTranslator
        // doesn't have a dedicated clear(); loading an empty filename
        // achieves the same. Result is the same as not installing a
        // translator at all.
        return load(QString());
    }

    // Different Qt versions / different CMake flavours of
    // qt_add_translations have put the compiled .qm files at
    // different resource paths. Try the most likely locations in
    // order; first hit wins. If none of them work we dump the
    // resource tree to stderr so we can see what's actually there.
    //
    // Naming convention is consistent: github-manager_<langcode>.qm
    // — that comes from the .ts filename in CMakeLists.txt, which
    // lrelease preserves.
    const QString filename =
        QStringLiteral("github-manager_%1").arg(lang_);

    const QStringList prefixes = {
        QStringLiteral(":/i18n/"),               // our RESOURCE_PREFIX in CMake
        QStringLiteral(":/i18n/translations/"),  // Qt 6.5+ sometimes nests
        QStringLiteral(":/translations/"),       // alternative
        QStringLiteral(":/"),                    // fallback: root
    };

    for (const QString& prefix : prefixes) {
        if (load(filename, prefix)) {
            qDebug() << "Translator: loaded" << prefix + filename + ".qm";
            return true;
        }
    }

    qWarning() << "Translator: could not load github-manager_" + lang_ + ".qm"
               << "from any expected location — UI will stay in English.";
    dumpResourceTree();
    (void)load(QString());
    return false;
}

} // namespace ghm::core
