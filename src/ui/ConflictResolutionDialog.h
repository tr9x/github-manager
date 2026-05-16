#pragma once

// ConflictResolutionDialog - shows the three sides of each conflicted
// file (base / ours / theirs) and lets the user mark files as
// resolved after editing them externally.
//
// Read-only viewer by design. The actual conflict resolution happens
// in the user's preferred editor (vim, vscode, kdiff3, …) — this
// dialog provides context (what the three sides look like) and
// state-machine actions (mark resolved, abort merge). It does NOT
// embed a full mergetool; that's a much larger feature.
//
// Modeless — the dialog stays on screen while the user alt-tabs to
// their editor and back. Host (MainWindow) keeps a pointer and feeds
// it fresh data via setEntries() / setBlobsForEntry() as the worker
// callbacks land.

#include <QDialog>
#include <QString>
#include <vector>

#include "git/GitHandler.h"

class QListWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QSplitter;

namespace ghm::ui {

class ConflictResolutionDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConflictResolutionDialog(QWidget* parent = nullptr);

    // Replace the entire list (e.g. after listConflicts returns).
    // Empty vector ⇒ "no conflicts" placeholder is shown and Mark
    // Resolved becomes disabled.
    void setEntries(const std::vector<ghm::git::ConflictEntry>& entries);

    // Push blob contents for the selected entry into the three view
    // panes. Called by host after loadConflictBlobs returns; we only
    // accept it if the path matches what the user has selected (the
    // user might have moved on to a different file by then).
    void setBlobsForEntry(const ghm::git::ConflictEntry& entry);

    // Disable action buttons while a worker op is in flight.
    void setBusy(bool busy);

    // Path to the local repo, used to compose absolute paths for
    // "Open in editor" via QDesktopServices.
    void setRepoPath(const QString& localPath);

Q_SIGNALS:
    // The user wants to inspect a different file from the list.
    void entrySelectionChanged(const QString& path);
    // The user clicked "Mark resolved" for the currently-selected file.
    void markResolvedRequested(const QString& path);
    // The user clicked "Abort merge".
    void abortMergeRequested();

private Q_SLOTS:
    void onListSelectionChanged();
    void onMarkResolvedClicked();
    void onAbortMergeClicked();
    void onOpenInEditorClicked();
    void onShowWorkingTreeClicked();

private:
    QString currentPath() const;
    void    updateButtons();

    QListWidget*    list_;
    QPlainTextEdit* oursView_;
    QPlainTextEdit* baseView_;
    QPlainTextEdit* theirsView_;
    QLabel*         placeholder_;   // shown when no conflicts
    QSplitter*      panesSplit_;
    QSplitter*      mainSplit_;

    QPushButton* openEditorBtn_;
    QPushButton* showWorkingTreeBtn_;
    QPushButton* markResolvedBtn_;
    QPushButton* abortBtn_;
    QPushButton* closeBtn_;

    QString repoPath_;
    QString currentlyShownPath_;   // path whose blobs are visible in the panes
    bool    busy_{false};
};

} // namespace ghm::ui
