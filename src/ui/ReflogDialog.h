#pragma once

// ReflogDialog - read-only view of HEAD's reflog with a recovery action.
//
// Use case: the user did something destructive (reset --hard, force
// delete branch, rebase, etc.) and wants to get back to the previous
// state. Reflog is git's local journal — every HEAD-moving operation
// adds one line, and the underlying commit objects stay in the object
// database until `git gc` reaps them (default 90 days for reachable
// commits, 30 for unreachable).
//
// UI shape:
//   Top: a notice explaining what reflog is and its limitations
//        (local-only, expires, gc reaps).
//   Middle: list of entries with old/new SHA, relative time, message.
//   Bottom: "Restore HEAD here" button — acts on the selected entry's
//           newSha via a soft reset. Working tree stays untouched.
//
// We intentionally don't expose hard reset from this dialog: a soft
// reset is recoverable (user can re-reset to wherever afterwards),
// hard reset overwrites the working tree and has no undo.

#include <QDialog>
#include <QString>
#include <vector>

#include "git/GitHandler.h"

class QListWidget;
class QPushButton;
class QLabel;

namespace ghm::ui {

class ReflogDialog : public QDialog {
    Q_OBJECT
public:
    explicit ReflogDialog(QWidget* parent = nullptr);

    void setEntries(const std::vector<ghm::git::ReflogEntry>& entries);

    // Toggle disabled state for the action button while a restore
    // operation is in flight.
    void setBusy(bool busy);

Q_SIGNALS:
    // Fired when the user picks an entry and clicks Restore.
    // `targetSha` is the entry's newSha (the SHA HEAD pointed at
    // AFTER the operation logged in that entry).
    void restoreRequested(const QString& targetSha);

    // Asked from the host's refresh button, in case the reflog
    // changed (e.g. after a restore, the dialog should refresh
    // to show the new state).
    void refreshRequested();

private Q_SLOTS:
    void onSelectionChanged();
    void onRestoreClicked();

private:
    QLabel*       notice_{nullptr};
    QListWidget*  list_{nullptr};
    QPushButton*  restoreBtn_{nullptr};
    QPushButton*  refreshBtn_{nullptr};
    QPushButton*  closeBtn_{nullptr};
};

} // namespace ghm::ui
