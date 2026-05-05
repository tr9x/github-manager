#pragma once

// DiffViewWidget - read-only unified-diff renderer.
//
// Lives in the bottom pane of the Changes tab. Receives a FileDiff
// from GitWorker and renders it like `git diff --color`: green
// additions, red deletions, blue hunk headers, dim context. Also
// supports binary-file and "no changes" placeholders.
//
// Implementation note: we use a QTextEdit with rich text and apply
// QTextCharFormat per line via QTextCursor. That gives us free
// selection/copy/find at the cost of memory ~3× plain text. For
// typical diff sizes (< 10k lines) it's fine.

#include <QWidget>
#include <QString>
#include <QList>
#include <QTextCursor>

#include "git/GitHandler.h"

class QTextEdit;
class QLabel;
class QToolButton;
class QStackedWidget;
class QShortcut;

namespace ghm::ui {

class SearchBar;

class DiffViewWidget : public QWidget {
    Q_OBJECT
public:
    explicit DiffViewWidget(QWidget* parent = nullptr);

    // Clears the panel back to "no file selected" placeholder.
    void clear();

    // Show a "diff is loading" spinner-style message for `path`.
    void setLoading(const QString& path);

    // Render the diff. If `error` is non-empty, an error placeholder
    // is shown instead.
    void setDiff(const ghm::git::FileDiff& diff, const QString& error = {});

private Q_SLOTS:
    // Search wiring: SearchBar emits these.
    void onSearchChanged(const QString& text, bool caseSensitive);
    void onFindNext();
    void onFindPrev();
    void onSearchClosed();
    // Ctrl+F shortcut.
    void onActivateSearch();

private:
    void renderEmpty(const QString& message);
    void renderText(const ghm::git::FileDiff& diff);

    // Re-scans the current document for the active query and rebuilds
    // matches_/extraSelections. Called from onSearchChanged and after
    // every renderText() so highlights survive a diff swap.
    void refreshMatches();
    void applyExtraSelections();
    void scrollToMatch(int index);

    QStackedWidget* stack_;
    QLabel*         placeholder_;
    QLabel*         header_;
    QTextEdit*      view_;

    SearchBar*      searchBar_;
    QShortcut*      findShortcut_;

    // Current search state. `query_` is empty when nothing's been
    // entered yet. matches_ holds positions in document order; current_
    // is an index into it (-1 = none focused).
    QString             query_;
    bool                caseSensitive_{false};
    QList<QTextCursor>  matches_;
    int                 current_{-1};
};

} // namespace ghm::ui
