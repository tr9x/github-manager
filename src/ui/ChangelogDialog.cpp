#include "ui/ChangelogDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QPushButton>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QTextDocument>
#include <QTextCursor>

namespace ghm::ui {

ChangelogDialog::ChangelogDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Changelog"));
    setModal(true);
    // Roomy size — changelog has lots of detail per version and
    // version-jumping benefits from seeing multiple entries at once.
    resize(820, 640);

    viewer_ = new QTextBrowser(this);
    viewer_->setReadOnly(true);
    // External links open in the system browser. Without this, Qt
    // tries to navigate inside the QTextBrowser which doesn't make
    // sense for http URLs.
    viewer_->setOpenExternalLinks(false);
    viewer_->setOpenLinks(false);
    connect(viewer_, &QTextBrowser::anchorClicked, this,
            [](const QUrl& url) {
                if (!url.isEmpty()) QDesktopServices::openUrl(url);
            });

    // Pull CHANGELOG.md out of the Qt resource pile and render it.
    // The path is set in CMakeLists.txt's qt_add_resources call —
    // `PREFIX "/"` + `FILES CHANGELOG.md` produces `:/CHANGELOG.md`.
    QFile f(QStringLiteral(":/CHANGELOG.md"));
    QString content;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        content = QString::fromUtf8(f.readAll());
        f.close();
    } else {
        // Resource missing — shouldn't happen in a built binary but
        // we degrade gracefully rather than showing an empty dialog.
        content = tr("# Changelog\n\n"
                     "The CHANGELOG.md resource is missing from this "
                     "build. This is a packaging bug — please report it.");
    }
    viewer_->setMarkdown(content);

    // Scroll to top — setMarkdown sometimes ends with cursor at the
    // bottom of the document. We want users seeing the latest
    // version first.
    auto cursor = viewer_->textCursor();
    cursor.movePosition(QTextCursor::Start);
    viewer_->setTextCursor(cursor);

    closeBtn_ = new QPushButton(tr("Close"), this);
    closeBtn_->setDefault(true);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    root->addWidget(viewer_, 1);
    root->addLayout(btnRow);
}

} // namespace ghm::ui
