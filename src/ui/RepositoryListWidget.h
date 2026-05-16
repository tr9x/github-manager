#pragma once

// RepositoryListWidget - sidebar with two sections:
//
//   1. "GitHub" - repos auto-fetched from the user's account.
//      Activating an item emits repositoryActivated(Repository).
//
//   2. "Local Folders" - paths the user has added via Settings.
//      Activating an item emits localFolderActivated(QString path).
//
// The same QLineEdit search filters both sections by name.

#include <QWidget>
#include <QList>
#include <QStringList>

#include "github/Repository.h"

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QPushButton;
class QLabel;

namespace ghm::ui {

class RepositoryListWidget : public QWidget {
    Q_OBJECT
public:
    explicit RepositoryListWidget(QWidget* parent = nullptr);
    ~RepositoryListWidget() override;

    // GitHub section.
    void setRepositories(const QList<ghm::github::Repository>& repos);
    void setLocalPath(const QString& fullName, const QString& localPath);
    ghm::github::Repository currentRepository() const;

    // Local-folders section.
    void setLocalFolders(const QStringList& paths);
    QString currentLocalFolder() const;

    // Imperative selection helpers (used after add/remove).
    void selectLocalFolder(const QString& path);

Q_SIGNALS:
    void repositoryActivated(const ghm::github::Repository& repo);
    void localFolderActivated(const QString& path);
    void addLocalFolderClicked();
    void removeLocalFolderRequested(const QString& path);

    // Emitted from the GitHub list's context menu when the user
    // picks "Make public" / "Make private". The host catches this,
    // confirms intent, and dispatches to GitHubClient. We carry
    // the full Repository so the host can show its name in the
    // confirmation dialog without a second lookup.
    void changeVisibilityRequested(const ghm::github::Repository& repo,
                                      bool makePrivate);

private Q_SLOTS:
    void onGithubSelectionChanged();
    void onLocalSelectionChanged();
    void onFilterChanged(const QString& text);
    void onLocalContextMenu(const QPoint& pos);
    void onGithubContextMenu(const QPoint& pos);

private:
    void rebuildGithubItems();
    void rebuildLocalItems();
    void styleGithubItem(QListWidgetItem* item, const ghm::github::Repository& repo);
    void styleLocalItem (QListWidgetItem* item, const QString& path);

    QLineEdit*   searchEdit_;

    QLabel*      githubHeader_;
    QListWidget* githubList_;

    QLabel*      localHeader_;
    QListWidget* localList_;
    QPushButton* addLocalBtn_;

    QList<ghm::github::Repository> repos_;
    QStringList                    localFolders_;
};

} // namespace ghm::ui
