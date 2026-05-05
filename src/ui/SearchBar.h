#pragma once

// SearchBar - small inline search affordance that mimics the Ctrl+F
// experience users know from editors and browsers.
//
// Owns no document of its own — it just emits signals (search text
// changed, prev/next navigation, close requested). The host widget
// drives the actual searching against whatever document it owns.
//
// Visibility is controlled by the host: call show()/hide() and bind
// Ctrl+F / Esc to toggle. Auto-focuses the input on show, restores
// focus to the host on hide.

#include <QWidget>
#include <QString>

class QLineEdit;
class QToolButton;
class QLabel;
class QCheckBox;

namespace ghm::ui {

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget* parent = nullptr);

    QString text() const;
    bool    caseSensitive() const;

    // Show + focus the field. If the field already has text, the host
    // re-fires its search to refresh highlights for the new context.
    void activate();

    // Wipe the query without firing searchChanged signals. Used when
    // the host wants to "close" the find session without hiding the
    // bar (because we display it permanently).
    void clearQuery();

    // Updates the "X of Y" counter. Pass `total = 0` to show "no matches"
    // styling; `current = 0` to suppress the X-of for inputs that don't
    // track current match yet (host can call setMatches(N) only).
    void setMatches(int current, int total);
    void setMatches(int total) { setMatches(0, total); }

    // Styles the input field as red-tinted when the user's query has
    // zero matches. Call after searching.
    void setHasMatches(bool any);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    // Fires whenever the search text or case-sensitivity option changes.
    // Hosts run a fresh search from the cursor position.
    void searchChanged(const QString& text, bool caseSensitive);

    // Fires on Enter (next) / Shift+Enter (prev), or the toolbar arrows.
    void findNextRequested();
    void findPrevRequested();

    // Fires on Esc or the close button. Host typically hide()s the bar
    // and restores focus to the document view.
    void closeRequested();

private Q_SLOTS:
    void onTextChanged(const QString& s);
    void onCaseToggled(bool checked);

private:
    QLineEdit*   input_;
    QToolButton* prevBtn_;
    QToolButton* nextBtn_;
    QToolButton* caseBtn_;
    QToolButton* closeBtn_;
    QLabel*      counter_;
};

} // namespace ghm::ui
