#include "ui/DiffViewWidget.h"
#include "ui/SearchBar.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QTextEdit>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QFontDatabase>
#include <QPalette>
#include <QFileInfo>
#include <QScrollBar>
#include <QShortcut>
#include <QKeySequence>

namespace ghm::ui {

namespace {

// Colour palette tuned for the dark stylesheet. Values are deliberately
// muted so a 1000-line diff doesn't feel like a Christmas tree.
struct DiffPalette {
    QColor addBg     {0x1f, 0x36, 0x21};   // dim green wash
    QColor delBg     {0x3a, 0x1f, 0x21};   // dim red wash
    QColor addFg     {0x9c, 0xe7, 0xa6};
    QColor delFg     {0xf2, 0xa0, 0xa0};
    QColor hunkFg    {0x7a, 0xb0, 0xff};
    QColor hunkBg    {0x18, 0x22, 0x33};
    QColor lineNoFg  {0x70, 0x76, 0x80};
    QColor contextFg {0xc8, 0xcc, 0xd0};
};

// Note: this used to be called `palette()` but that shadowed
// QWidget::palette() inside member functions, leading to confusing
// "QPalette has no member addFg" errors. The new name is unambiguous.
const DiffPalette& diffPalette()
{
    static const DiffPalette p;
    return p;
}

// Format gutter line numbers as "  12 |  13 │ ", right-aligned in
// fixed-width columns. -1 renders as blanks.
QString formatGutter(int oldLine, int newLine)
{
    auto col = [](int n) -> QString {
        if (n < 0) return QStringLiteral("    ");
        return QStringLiteral("%1").arg(n, 4);
    };
    return col(oldLine) + QStringLiteral(" ") + col(newLine) + QStringLiteral(" │ ");
}

QTextCharFormat fmt(const QColor& fg, QColor bg = QColor())
{
    QTextCharFormat f;
    f.setForeground(fg);
    if (bg.isValid()) f.setBackground(bg);
    return f;
}

} // namespace

// ---------------------------------------------------------------------------

DiffViewWidget::DiffViewWidget(QWidget* parent)
    : QWidget(parent)
    , stack_(new QStackedWidget(this))
    , placeholder_(new QLabel(this))
    , header_(new QLabel(this))
    , view_(new QTextEdit(this))
    , searchBar_(new SearchBar(this))
    , findShortcut_(nullptr)
{
    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    placeholder_->setStyleSheet(
        QStringLiteral("color: #707680; padding: 24px;"));
    placeholder_->setText(tr("Select a file in the list above to see its diff."));

    header_->setStyleSheet(
        QStringLiteral("color: #c8ccd0; padding: 4px 6px; "
                       "background: #232830; border-bottom: 1px solid #3c4148;"));
    header_->setTextFormat(Qt::RichText);
    header_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    view_->setReadOnly(true);
    view_->setLineWrapMode(QTextEdit::NoWrap);
    // Diff content is always meaningful as monospace.
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setStyleHint(QFont::Monospace);
    view_->setFont(mono);
    view_->setStyleSheet(
        QStringLiteral("QTextEdit { background: #14171c; "
                       "selection-background-color: #344b6c; "
                       "border: 0; }"));

    // Search bar. Visible by default so the feature is discoverable —
    // a hidden Ctrl+F would be a hidden capability. The bar collapses
    // to a single thin row when empty, so it doesn't dominate the UI.
    // Ctrl+F still works as an accelerator: it just focuses the field
    // and selects whatever's already typed there.
    connect(searchBar_, &SearchBar::searchChanged,
            this, &DiffViewWidget::onSearchChanged);
    connect(searchBar_, &SearchBar::findNextRequested,
            this, &DiffViewWidget::onFindNext);
    connect(searchBar_, &SearchBar::findPrevRequested,
            this, &DiffViewWidget::onFindPrev);
    connect(searchBar_, &SearchBar::closeRequested,
            this, &DiffViewWidget::onSearchClosed);

    auto* diffPage = new QWidget(this);
    {
        auto* col = new QVBoxLayout(diffPage);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);
        col->addWidget(header_);
        col->addWidget(searchBar_);
        col->addWidget(view_, 1);
    }

    stack_->addWidget(placeholder_);   // index 0: empty / placeholder
    stack_->addWidget(diffPage);        // index 1: diff content
    stack_->setCurrentIndex(0);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(stack_);

    // Ctrl+F shortcut, scoped to this widget so it doesn't fire when
    // focus is in the file list or another panel. WindowShortcut
    // would clash if we ever embed multiple DiffViewWidgets in the
    // same window — and we already do (Changes tab + History tab).
    findShortcut_ = new QShortcut(QKeySequence::Find, this);
    findShortcut_->setContext(Qt::WidgetWithChildrenShortcut);
    connect(findShortcut_, &QShortcut::activated,
            this, &DiffViewWidget::onActivateSearch);
}

// ----- Public API ----------------------------------------------------------

void DiffViewWidget::clear()
{
    matches_.clear();
    current_ = -1;
    // Reset the search query — the previous file's hits don't apply
    // to the placeholder pane.
    if (searchBar_) {
        searchBar_->clearQuery();
        searchBar_->setMatches(0, 0);
        searchBar_->setHasMatches(true);
    }
    query_.clear();
    renderEmpty(tr("Select a file in the list above to see its diff."));
}

void DiffViewWidget::setLoading(const QString& path)
{
    matches_.clear();
    current_ = -1;
    renderEmpty(tr("Loading diff for %1…").arg(QFileInfo(path).fileName()));
}

void DiffViewWidget::setDiff(const ghm::git::FileDiff& diff, const QString& error)
{
    if (!error.isEmpty()) {
        matches_.clear();
        current_ = -1;
        renderEmpty(tr("Could not load diff: %1").arg(error));
        return;
    }

    if (diff.path.isEmpty()) {
        clear();
        return;
    }

    if (diff.isBinary) {
        matches_.clear();
        current_ = -1;
        renderEmpty(
            tr("%1 is a binary file — diff is not displayed.").arg(diff.path));
        return;
    }

    if (diff.hunks.empty()) {
        matches_.clear();
        current_ = -1;
        renderEmpty(tr("No changes for %1 in this scope.").arg(diff.path));
        return;
    }

    renderText(diff);

    // If the user had an active query before the diff swap (common when
    // clicking through commits in History with Ctrl+F open), re-scan
    // the new document so highlights update to whatever's now visible.
    if (!query_.isEmpty()) refreshMatches();
}

// ----- Rendering -----------------------------------------------------------

void DiffViewWidget::renderEmpty(const QString& message)
{
    placeholder_->setText(message);
    stack_->setCurrentIndex(0);
}

void DiffViewWidget::renderText(const ghm::git::FileDiff& diff)
{
    // Header — file name (with rename arrow if applicable) + add/del summary.
    QString headerHtml;
    if (!diff.oldPath.isEmpty() && diff.oldPath != diff.path) {
        headerHtml = QStringLiteral(
            "<b>%1</b>  →  <b>%2</b>  "
            "<span style='color:#9ce7a6;'>+%3</span> "
            "<span style='color:#f2a0a0;'>-%4</span>")
            .arg(diff.oldPath.toHtmlEscaped(),
                 diff.path   .toHtmlEscaped())
            .arg(diff.additions).arg(diff.deletions);
    } else {
        headerHtml = QStringLiteral(
            "<b>%1</b>  "
            "<span style='color:#9ce7a6;'>+%2</span> "
            "<span style='color:#f2a0a0;'>-%3</span>")
            .arg(diff.path.toHtmlEscaped())
            .arg(diff.additions).arg(diff.deletions);
    }
    header_->setText(headerHtml);

    // Pre-build the formats we'll reuse per line.
    const auto& p = diffPalette();
    const QTextCharFormat addFmt     = fmt(p.addFg,     p.addBg);
    const QTextCharFormat delFmt     = fmt(p.delFg,     p.delBg);
    const QTextCharFormat ctxFmt     = fmt(p.contextFg);
    const QTextCharFormat hunkFmt    = fmt(p.hunkFg,    p.hunkBg);
    const QTextCharFormat gutterFmt  = fmt(p.lineNoFg);

    // Build the document via QTextCursor. Using setPlainText would be
    // faster but loses per-line backgrounds on whitespace at the right
    // edge — for diffs we want a flat coloured band across the whole
    // visible line, hence a paragraph-level QTextBlockFormat.
    view_->clear();
    QTextCursor cur(view_->document());
    cur.beginEditBlock();

    bool firstLine = true;
    auto newLine = [&](const QColor& bg) {
        if (!firstLine) cur.insertBlock();
        firstLine = false;
        QTextBlockFormat block;
        if (bg.isValid()) block.setBackground(bg);
        cur.setBlockFormat(block);
    };

    for (const auto& hunk : diff.hunks) {
        // Hunk header line: "@@ -oldStart,oldLines +newStart,newLines @@ optional context"
        newLine(p.hunkBg);
        const QString headerLine = hunk.header.isEmpty()
            ? QStringLiteral("@@ -%1,%2 +%3,%4 @@")
                .arg(hunk.oldStart).arg(hunk.oldLines)
                .arg(hunk.newStart).arg(hunk.newLines)
            : hunk.header;
        cur.insertText(headerLine, hunkFmt);

        for (const auto& line : hunk.lines) {
            switch (line.origin) {
                case '+': newLine(p.addBg); break;
                case '-': newLine(p.delBg); break;
                case ' ': newLine(QColor()); break;
                default:  // EOF markers etc.
                    continue;
            }
            // Gutter (line numbers) — same colour as line text but dimmer.
            cur.insertText(formatGutter(line.oldLineNo, line.newLineNo), gutterFmt);

            const QTextCharFormat& bodyFmt =
                line.origin == '+' ? addFmt :
                line.origin == '-' ? delFmt : ctxFmt;
            // Sigil ('+', '-', ' ') as the first body character.
            cur.insertText(QString(QLatin1Char(line.origin)), bodyFmt);
            cur.insertText(QStringLiteral(" "), bodyFmt);
            cur.insertText(line.content, bodyFmt);
        }
    }

    cur.endEditBlock();

    // Move scroll back to top — without this, very long diffs would land
    // mid-document because of the block insertions.
    auto* vbar = view_->verticalScrollBar();
    if (vbar) vbar->setValue(0);
    auto* hbar = view_->horizontalScrollBar();
    if (hbar) hbar->setValue(0);

    stack_->setCurrentIndex(1);
}

// ----- Search --------------------------------------------------------------

void DiffViewWidget::onActivateSearch()
{
    // Ctrl+F is meaningless on the placeholder pane (no text to search).
    // Bail silently rather than focusing a useless input field.
    if (stack_->currentIndex() != 1) return;
    // Just focus + select-all. The bar is always visible now, so this
    // is a pure accelerator: get the user typing as fast as possible.
    searchBar_->activate();
}

void DiffViewWidget::onSearchClosed()
{
    // The bar stays visible — "close" here means "clear my search".
    // Wipe the query, drop highlights, hand focus back to the diff so
    // the user can keep reading.
    searchBar_->clearQuery();
    matches_.clear();
    current_ = -1;
    applyExtraSelections();
    view_->setFocus();
}

void DiffViewWidget::onSearchChanged(const QString& text, bool caseSensitive)
{
    query_ = text;
    caseSensitive_ = caseSensitive;
    refreshMatches();
}

void DiffViewWidget::onFindNext()
{
    if (matches_.isEmpty()) return;
    current_ = (current_ + 1) % matches_.size();
    scrollToMatch(current_);
    applyExtraSelections();
    searchBar_->setMatches(current_ + 1, matches_.size());
}

void DiffViewWidget::onFindPrev()
{
    if (matches_.isEmpty()) return;
    // Modulo on negative numbers in C++ is implementation-defined for
    // operand signs but defined-in-practice positive here because we
    // add `matches_.size()` first.
    current_ = (current_ - 1 + matches_.size()) % matches_.size();
    scrollToMatch(current_);
    applyExtraSelections();
    searchBar_->setMatches(current_ + 1, matches_.size());
}

void DiffViewWidget::refreshMatches()
{
    matches_.clear();
    current_ = -1;

    if (query_.isEmpty()) {
        searchBar_->setMatches(0, 0);
        searchBar_->setHasMatches(true);
        applyExtraSelections();
        return;
    }

    // Walk the document collecting every match. We use QTextDocument::find()
    // rather than rolling our own scanning so case-insensitive matching
    // and Unicode normalization are handled by Qt.
    QTextDocument::FindFlags flags;
    if (caseSensitive_) flags |= QTextDocument::FindCaseSensitively;

    auto* doc = view_->document();
    QTextCursor cur(doc);   // start at position 0
    while (true) {
        cur = doc->find(query_, cur, flags);
        if (cur.isNull()) break;
        matches_.append(cur);
        // Advance past this match. find() returns a cursor with selection
        // covering the hit; calling find() again uses the cursor's
        // *position* (post-selection end), so loop terminates naturally.
    }

    const bool any = !matches_.isEmpty();
    searchBar_->setHasMatches(any);

    if (any) {
        // Pick the first match at-or-after the user's current view, so
        // hitting Ctrl+F mid-document doesn't fling the viewport back
        // to the top. Falls back to match #0 if everything's behind us.
        const int viewportPos = view_->textCursor().position();
        int picked = 0;
        for (int i = 0; i < matches_.size(); ++i) {
            if (matches_[i].selectionStart() >= viewportPos) {
                picked = i;
                break;
            }
        }
        current_ = picked;
        scrollToMatch(current_);
        searchBar_->setMatches(current_ + 1, matches_.size());
    } else {
        searchBar_->setMatches(0, 0);
    }

    applyExtraSelections();
}

void DiffViewWidget::applyExtraSelections()
{
    QList<QTextEdit::ExtraSelection> sels;
    sels.reserve(matches_.size());

    // Two highlight styles: dimmer yellow for all matches, brighter
    // orange for the current one. Visually similar to Sublime Text's
    // search affordance.
    QTextCharFormat allFmt;
    allFmt.setBackground(QColor(0xff, 0xd3, 0x54, 0x80));   // muted yellow @ ~50%
    allFmt.setForeground(QColor(0x14, 0x14, 0x14));

    QTextCharFormat currentFmt;
    currentFmt.setBackground(QColor(0xff, 0x8c, 0x33));     // solid orange
    currentFmt.setForeground(QColor(0x14, 0x14, 0x14));

    for (int i = 0; i < matches_.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = matches_[i];
        sel.format = (i == current_) ? currentFmt : allFmt;
        sels.append(sel);
    }
    view_->setExtraSelections(sels);
}

void DiffViewWidget::scrollToMatch(int index)
{
    if (index < 0 || index >= matches_.size()) return;
    // Setting the textCursor centers the document on the cursor's line
    // automatically, then we still call ensureCursorVisible() to be safe
    // for very long single-line matches.
    view_->setTextCursor(matches_[index]);
    view_->ensureCursorVisible();
}

} // namespace ghm::ui
