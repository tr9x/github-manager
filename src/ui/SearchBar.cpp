#include "ui/SearchBar.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>
#include <QLabel>
#include <QCheckBox>
#include <QKeyEvent>
#include <QEvent>
#include <QSignalBlocker>
#include <QStyle>

namespace ghm::ui {

SearchBar::SearchBar(QWidget* parent)
    : QWidget(parent)
    , input_   (new QLineEdit(this))
    , prevBtn_ (new QToolButton(this))
    , nextBtn_ (new QToolButton(this))
    , caseBtn_ (new QToolButton(this))
    , closeBtn_(new QToolButton(this))
    , counter_ (new QLabel(this))
{
    setObjectName(QStringLiteral("searchBar"));
    setStyleSheet(QStringLiteral(
        "#searchBar { background: #232830; "
        "             border-bottom: 1px solid #3c4148; padding: 4px; }"));

    input_->setPlaceholderText(tr("Find in diff  (Ctrl+F)"));
    input_->setClearButtonEnabled(true);
    input_->setToolTip(
        tr("Search the diff. Press Enter for next match, "
           "Shift+Enter for previous. Esc to clear."));

    // Glyph-only buttons. Could use QIcon::fromTheme but those are
    // theme-dependent and inconsistent across distros — text glyphs
    // are reliable on a dark stylesheet and don't need icon assets.
    auto styleBtn = [](QToolButton* btn, const QString& text, const QString& tip) {
        btn->setText(text);
        btn->setToolTip(tip);
        btn->setAutoRaise(true);
        btn->setCursor(Qt::PointingHandCursor);
    };
    styleBtn(prevBtn_,  QStringLiteral("▲"),  tr("Previous match (Shift+Enter)"));
    styleBtn(nextBtn_,  QStringLiteral("▼"),  tr("Next match (Enter)"));
    styleBtn(caseBtn_,  QStringLiteral("Aa"), tr("Match case"));
    styleBtn(closeBtn_, QStringLiteral("✕"),  tr("Clear search (Esc)"));
    caseBtn_->setCheckable(true);

    counter_->setStyleSheet(QStringLiteral("color: #9aa0a6; padding: 0 8px;"));
    counter_->setMinimumWidth(80);

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(4);
    row->addWidget(input_, 1);
    row->addWidget(counter_);
    row->addWidget(caseBtn_);
    row->addWidget(prevBtn_);
    row->addWidget(nextBtn_);
    row->addSpacing(4);
    row->addWidget(closeBtn_);

    connect(input_,   &QLineEdit::textChanged,
            this, &SearchBar::onTextChanged);
    // Don't wire returnPressed — we handle Enter/Shift+Enter ourselves
    // through eventFilter so we can distinguish "next" from "prev".
    input_->installEventFilter(this);
    connect(prevBtn_, &QToolButton::clicked,
            this, &SearchBar::findPrevRequested);
    connect(nextBtn_, &QToolButton::clicked,
            this, &SearchBar::findNextRequested);
    connect(caseBtn_, &QToolButton::toggled,
            this, &SearchBar::onCaseToggled);
    connect(closeBtn_,&QToolButton::clicked,
            this, &SearchBar::closeRequested);

    setMatches(0, 0);
    setHasMatches(true);  // neutral until first search
}

QString SearchBar::text() const          { return input_->text(); }
bool    SearchBar::caseSensitive() const { return caseBtn_->isChecked(); }

void SearchBar::activate()
{
    show();
    input_->setFocus(Qt::ShortcutFocusReason);
    input_->selectAll();
    if (!input_->text().isEmpty()) {
        // Re-fire so the host can rebuild highlights against whatever
        // document is current now. (User may have changed commits,
        // switched files, etc., between Ctrl+F invocations.)
        Q_EMIT searchChanged(input_->text(), caseBtn_->isChecked());
    }
}

void SearchBar::clearQuery()
{
    // Block the textChanged signal so the host doesn't fire a redundant
    // searchChanged("") — the close handler already wipes its own state.
    QSignalBlocker block(input_);
    input_->clear();
    counter_->clear();
    setHasMatches(true);
}

void SearchBar::setMatches(int current, int total)
{
    if (input_->text().isEmpty()) {
        counter_->clear();
        return;
    }
    if (total <= 0) {
        counter_->setText(tr("no matches"));
        return;
    }
    if (current > 0) {
        counter_->setText(tr("%1 of %2").arg(current).arg(total));
    } else {
        counter_->setText(tr("%1 matches").arg(total));
    }
}

void SearchBar::setHasMatches(bool any)
{
    // Subtly tint the background red when nothing matches — same
    // affordance Firefox / Chrome use.
    input_->setStyleSheet(any
        ? QString()
        : QStringLiteral("QLineEdit { background: #3a1f21; color: #f2a0a0; }"));
}

void SearchBar::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        Q_EMIT closeRequested();
        e->accept();
        return;
    }
    QWidget::keyPressEvent(e);
}

bool SearchBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == input_ && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        // Enter / Return: navigate matches. Shift modifier reverses
        // direction, matching the convention everyone learned from
        // browser find-bars (Firefox, Chrome, etc.).
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            if (ke->modifiers() & Qt::ShiftModifier) {
                Q_EMIT findPrevRequested();
            } else {
                Q_EMIT findNextRequested();
            }
            return true;   // consume; QLineEdit's default would do nothing useful
        }
        if (ke->key() == Qt::Key_Escape) {
            Q_EMIT closeRequested();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SearchBar::onTextChanged(const QString& s)
{
    Q_EMIT searchChanged(s, caseBtn_->isChecked());
}

void SearchBar::onCaseToggled(bool /*checked*/)
{
    // Re-fire current text under the new case mode so highlights
    // refresh immediately.
    Q_EMIT searchChanged(input_->text(), caseBtn_->isChecked());
}

} // namespace ghm::ui
