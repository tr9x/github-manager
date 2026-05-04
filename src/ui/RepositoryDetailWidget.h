#pragma once

// RepositoryDetailWidget - the right-hand panel showing one repo.
//
// When a remote-only repo is selected, this widget shows metadata and a
// "Clone…" call-to-action. When a local repo is loaded, it shows branch
// state, a file tree, and pull/push controls.

#include <QWidget>
#include <QString>
#include <QStringList>

#include "github/Repository.h"
#include "git/GitHandler.h"

class QLabel;
class QPushButton;
class QComboBox;
class QTreeView;
class QFileSystemModel;

namespace ghm::ui {

class RepositoryDetailWidget : public QWidget {
    Q_OBJECT
public:
    explicit RepositoryDetailWidget(QWidget* parent = nullptr);
    ~RepositoryDetailWidget() override;

    // Show repo metadata. If `localPath` is empty the panel goes into
    // "remote only — clone to start" mode.
    void showRepository(const ghm::github::Repository& repo);

    // Updates the branch + status block. Called from MainWindow after a
    // GitWorker::statusReady.
    void updateStatus(const QString& currentBranch,
                      const ghm::git::StatusSummary& status);

    void setBranches(const QStringList& branches, const QString& current);

Q_SIGNALS:
    void cloneRequested      (const ghm::github::Repository& repo);
    void openLocallyRequested(const ghm::github::Repository& repo);
    void pullRequested       (const QString& localPath);
    void pushRequested       (const QString& localPath);
    void refreshRequested    (const QString& localPath);
    void switchBranchRequested(const QString& localPath, const QString& branch);

private Q_SLOTS:
    void onClone();
    void onOpen();
    void onPull();
    void onPush();
    void onRefresh();
    void onBranchActivated(int index);

private:
    void applyMode();
    void clearStatus();

    ghm::github::Repository repo_;
    bool                    branchSwitchInFlight_{false};

    // Metadata
    QLabel* nameLabel_;
    QLabel* visibilityLabel_;
    QLabel* descriptionLabel_;
    QLabel* updatedLabel_;
    QLabel* localPathLabel_;

    // Local-mode controls
    QComboBox*   branchCombo_;
    QLabel*      statusLabel_;
    QPushButton* cloneBtn_;
    QPushButton* openBtn_;
    QPushButton* pullBtn_;
    QPushButton* pushBtn_;
    QPushButton* refreshBtn_;

    // File tree
    QFileSystemModel* fsModel_;
    QTreeView*        fileTree_;
};

} // namespace ghm::ui
