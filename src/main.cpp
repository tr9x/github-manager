// main.cpp - application entry point.
//
// Bootstraps Qt, applies the dark stylesheet, installs the translator
// so the UI comes up in the user's preferred language, and shows
// MainWindow. MainWindow itself decides whether to prompt the user for
// login by querying SecureStorage on construction.

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStyleHints>

#include "ui/MainWindow.h"
#include "core/Settings.h"
#include "core/Translator.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QApplication::setOrganizationName("github-manager");
    QApplication::setOrganizationDomain("github-manager.local");
    QApplication::setApplicationName("GitHub Manager");
    QApplication::setApplicationDisplayName("GitHub Manager");
    QApplication::setApplicationVersion(GHM_VERSION);
    QApplication::setWindowIcon(QIcon::fromTheme("system-software-update"));

    // Install the language translator BEFORE constructing any widgets,
    // so labels/menu titles built in widget constructors run through
    // it. The translator stays alive for the lifetime of QApplication.
    auto* translator = new ghm::core::Translator(&app);
    {
        ghm::core::Settings settings;
        translator->setLanguage(settings.language());
    }
    QApplication::installTranslator(translator);

    // Load the bundled dark stylesheet. Users on systems with a strong
    // light preference can disable this with --no-dark-mode.
    bool wantDark = true;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromUtf8(argv[i]) == QStringLiteral("--no-dark-mode")) {
            wantDark = false;
        }
    }
    if (wantDark) {
        QFile qss(QStringLiteral(":/resources/styles/dark.qss"));
        if (qss.open(QIODevice::ReadOnly | QIODevice::Text)) {
            app.setStyleSheet(QString::fromUtf8(qss.readAll()));
        }
    }

    ghm::ui::MainWindow window;
    window.show();

    return QApplication::exec();
}
