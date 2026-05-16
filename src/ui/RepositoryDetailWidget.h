#pragma once

// RepositoryDetailWidget - the right-hand panel showing one repo.
//
// When a remote-only repo is selected, this widget shows metadata,
// a "Clone…" call-to-action, and three preview tabs:
//   * README  — markdown-rendered README content from the default branch
//   * Files   — top-level directory listing via the GitHub Contents API
//   * About   — stats (stars/forks/issues), languages bar, topics
//
// When a local repo is loaded, the local file tree replaces the
// remote-files tab content and the branch/status panel becomes
// visible. The README and About tabs stay populated as long as
// the user is signed in to GitHub.

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>

#include "github/Repository.h"
#include "git/GitHandler.h"

class QLabel;
class QPushButton;
class QComboBox;
class QTreeView;
class QFileSystemModel;
class QTextBrowser;
class QListWidget;
class QListWidgetItem;
class QTabWidget;
class QHBoxLayout;

namespace ghm::ui {

class LanguagesBar;

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

    // Fed by MainWindow when GitHubClient emits the corresponding
    // detail signals. Each updates one of the preview tabs.
    void setReadme(const QString& fullName, const QString& markdown);
    void setReadmeUnavailable(const QString& fullName);
    void setRemoteContents(const QString& fullName,
                            const QString& path,
                            const QList<ghm::github::ContentEntry>& entries);
    void setLanguages(const QString& fullName,
                       const QMap<QString, qint64>& bytesByLang);

Q_SIGNALS:
    void cloneRequested      (const ghm::github::Repository& repo);
    void openLocallyRequested(const ghm::github::Repository& repo);
    void pullRequested       (const QString& localPath);
    void pushRequested       (const QString& localPath);
    void refreshRequested    (const QString& localPath);
    void switchBranchRequested(const QString& localPath, const QString& branch);

    // Detail-panel fetch requests. MainWindow listens, dispatches to
    // GitHubClient, then routes the responses back via setReadme /
    // setRemoteContents / setLanguages.
    void readmeRequested   (const QString& fullName);
    void contentsRequested (const QString& fullName, const QString& path);
    void languagesRequested(const QString& fullName);

    // Opens htmlUrl in the system browser. Emit-only; MainWindow
    // forwards through QDesktopServices so widget stays Qt-only.
    void openInBrowserRequested(const QString& url);

private Q_SLOTS:
    void onClone();
    void onOpen();
    void onPull();
    void onPush();
    void onRefresh();
    void onBranchActivated(int index);
    void onRemoteFileActivated(QListWidgetItem* item);
    void onOpenInBrowser();

private:
    void applyMode();
    void clearStatus();
    void clearDetailTabs();
    void requestDetailForCurrent();
    void renderLanguagesBar();

    ghm::github::Repository repo_;
    bool                    branchSwitchInFlight_{false};
    // Cache to avoid re-fetching when the user clicks back and forth
    // between repos in the sidebar. Keyed by fullName.
    QMap<QString, QString>                              readmeCache_;
    QMap<QString, QList<ghm::github::ContentEntry>>     contentsCache_;
    QMap<QString, QMap<QString, qint64>>                languagesCache_;

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
    QPushButton* openBrowserBtn_;

    // File tree (local mode)
    QFileSystemModel* fsModel_;
    QTreeView*        fileTree_;

    // Detail tabs
    QTabWidget*   detailTabs_;
    QTextBrowser* readmeView_;
    QLabel*       readmeEmptyHint_;
    QListWidget*  remoteFilesList_;
    QLabel*       remoteFilesEmptyHint_;
    QLabel*       statsLabel_;     // stars / forks / issues / size
    QLabel*       topicsLabel_;    // comma-separated topics
    QLabel*       defaultBranchLabel_;
    QLabel*       primaryLangLabel_;
    QWidget*      languagesBarHost_;
    QHBoxLayout*  languagesBarLayout_;
};

} // namespace ghm::ui
