// main.cpp - application entry point.
//
// Bootstraps Qt, applies the dark stylesheet, and shows MainWindow.
// MainWindow itself decides whether to prompt the user for login by
// querying SecureStorage on construction.

#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QStyleHints>

#include "ui/MainWindow.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QApplication::setOrganizationName("github-manager");
    QApplication::setOrganizationDomain("github-manager.local");
    QApplication::setApplicationName("GitHub Manager");
    QApplication::setApplicationDisplayName("GitHub Manager");
    QApplication::setApplicationVersion(GHM_VERSION);
    QApplication::setWindowIcon(QIcon::fromTheme("system-software-update"));

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
