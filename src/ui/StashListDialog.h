#pragma once

// StashListDialog - shows the stash stack and lets the user apply,
// pop, or drop entries.
//
// Modal but non-blocking in the sense that the caller (MainWindow)
// does the actual git operations through the worker; the dialog just
// emits intent signals and refreshes its list when told to.

#include <QDialog>
#include <QString>
#include <vector>

#include "git/GitHandler.h"

class QListWidget;
class QPushButton;
class QLabel;

namespace ghm::ui {

class StashListDialog : public QDialog {
    Q_OBJECT
public:
    explicit StashListDialog(QWidget* parent = nullptr);

    // Replaces the displayed list. Pass an empty vector to show the
    // "no stashes" placeholder.
    void setEntries(const std::vector<ghm::git::StashEntry>& entries);

    // While a stash op is in flight (apply/pop/drop), action buttons
    // are disabled to prevent double-fire.
    void setBusy(bool busy);

Q_SIGNALS:
    void applyRequested(int index);
    void popRequested  (int index);
    void dropRequested (int index);

private Q_SLOTS:
    void onSelectionChanged();
    void onApplyClicked();
    void onPopClicked();
    void onDropClicked();

private:
    int currentIndex() const;

    QListWidget* list_;
    QLabel*      placeholder_;
    QPushButton* applyBtn_;
    QPushButton* popBtn_;
    QPushButton* dropBtn_;
    QPushButton* closeBtn_;
};

} // namespace ghm::ui
