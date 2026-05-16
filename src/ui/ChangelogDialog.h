#pragma once

// ChangelogDialog — renders CHANGELOG.md (embedded as a Qt resource
// at :/CHANGELOG.md) so users can see what changed across versions
// without leaving the app. Opens from the status-bar version label.
//
// Markdown is rendered via QTextBrowser::setMarkdown (CommonMark +
// GFM tables). Any links in the changelog (e.g. to issue trackers
// or commits) open in the system browser via QDesktopServices.

#include <QDialog>

class QTextBrowser;
class QPushButton;

namespace ghm::ui {

class ChangelogDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChangelogDialog(QWidget* parent = nullptr);

private:
    QTextBrowser* viewer_{nullptr};
    QPushButton*  closeBtn_{nullptr};
};

} // namespace ghm::ui
