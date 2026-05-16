#include "ui/RepositoryListWidget.h"
#include "core/TimeFormatting.h"

#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QVariant>
#include <QFileInfo>
#include <QDir>
#include <QMenu>

namespace ghm::ui {

namespace {
constexpr int kRepoRole  = Qt::UserRole + 1;
constexpr int kPathRole  = Qt::UserRole + 1;

QLabel* makeSectionHeader(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral(
        "color: #9aa0a6; padding: 6px 4px 2px 4px; font-weight: bold; "
        "letter-spacing: 0.5px; text-transform: uppercase; font-size: 10px;"));
    return l;
}

} // namespace

RepositoryListWidget::RepositoryListWidget(QWidget* parent)
    : QWidget(parent)
    , searchEdit_(new QLineEdit(this))
    , githubHeader_(makeSectionHeader(tr("GitHub"), this))
    , githubList_(new QListWidget(this))
    , localHeader_(makeSectionHeader(tr("Local Folders"), this))
    , localList_(new QListWidget(this))
    , addLocalBtn_(new QPushButton(tr("+ Add local folder…"), this))
{
    searchEdit_->setPlaceholderText(tr("Search repositories…"));
    searchEdit_->setClearButtonEnabled(true);

    auto stylize = [](QListWidget* lw) {
        lw->setUniformItemSizes(false);
        lw->setAlternatingRowColors(true);
        lw->setSelectionMode(QAbstractItemView::SingleSelection);
    };
    stylize(githubList_);
    stylize(localList_);

    addLocalBtn_->setStyleSheet(
        QStringLiteral("text-align: left; padding: 6px;"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(2);
    root->addWidget(searchEdit_);
    root->addSpacing(4);

    root->addWidget(githubHeader_);
    root->addWidget(githubList_, /*stretch*/ 3);

    auto* localHeaderRow = new QHBoxLayout;
    localHeaderRow->setContentsMargins(0, 0, 0, 0);
    localHeaderRow->addWidget(localHeader_, 1);
    root->addLayout(localHeaderRow);

    root->addWidget(addLocalBtn_);
    root->addWidget(localList_, /*stretch*/ 2);

    connect(searchEdit_, &QLineEdit::textChanged,
            this, &RepositoryListWidget::onFilterChanged);

    connect(githubList_, &QListWidget::itemSelectionChanged,
            this, &RepositoryListWidget::onGithubSelectionChanged);
    connect(localList_, &QListWidget::itemSelectionChanged,
            this, &RepositoryListWidget::onLocalSelectionChanged);

    // Make the two lists mutually exclusive: selecting in one clears the
    // other so the detail panel only shows the active item.
    connect(githubList_, &QListWidget::itemSelectionChanged, this,
            [this] { if (githubList_->selectedItems().size())
                         localList_->clearSelection(); });
    connect(localList_, &QListWidget::itemSelectionChanged, this,
            [this] { if (localList_->selectedItems().size())
                         githubList_->clearSelection(); });

    connect(addLocalBtn_, &QPushButton::clicked,
            this, &RepositoryListWidget::addLocalFolderClicked);

    localList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(localList_, &QListWidget::customContextMenuRequested,
            this, &RepositoryListWidget::onLocalContextMenu);

    // GitHub list context menu — currently just visibility toggle.
    // Future actions (delete repo, archive, transfer, change topic…)
    // could live here too.
    githubList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(githubList_, &QListWidget::customContextMenuRequested,
            this, &RepositoryListWidget::onGithubContextMenu);
}

RepositoryListWidget::~RepositoryListWidget() = default;

// ----- GitHub section ------------------------------------------------------

void RepositoryListWidget::setRepositories(const QList<ghm::github::Repository>& repos)
{
    repos_ = repos;
    rebuildGithubItems();
}

void RepositoryListWidget::setLocalPath(const QString& fullName, const QString& localPath)
{
    for (auto& r : repos_) {
        if (r.fullName == fullName) {
            r.localPath = localPath;
            break;
        }
    }
    rebuildGithubItems();
}

ghm::github::Repository RepositoryListWidget::currentRepository() const
{
    QListWidgetItem* item = githubList_->currentItem();
    if (!item || !item->isSelected()) return {};
    return item->data(kRepoRole).value<ghm::github::Repository>();
}

void RepositoryListWidget::rebuildGithubItems()
{
    const QString filter = searchEdit_->text().trimmed().toLower();
    const QString prevFullName = currentRepository().fullName;
    githubList_->clear();

    QListWidgetItem* toReselect = nullptr;
    for (const auto& r : repos_) {
        if (!filter.isEmpty() &&
            !r.fullName.toLower().contains(filter) &&
            !r.description.toLower().contains(filter)) {
            continue;
        }
        auto* item = new QListWidgetItem(githubList_);
        styleGithubItem(item, r);
        if (r.fullName == prevFullName) toReselect = item;
    }
    if (toReselect) githubList_->setCurrentItem(toReselect);
}

void RepositoryListWidget::styleGithubItem(QListWidgetItem* item,
                                           const ghm::github::Repository& repo)
{
    const QString visibility = repo.isPrivate ? tr("private") : tr("public");
    const QString localBadge = repo.localPath.isEmpty()
        ? QString()
        : QStringLiteral(" • ▼ local");

    const QString label = QStringLiteral("%1\n  %2 • %3%4")
        .arg(repo.fullName, visibility, ghm::core::relativeTime(repo.updatedAt), localBadge);
    item->setText(label);
    item->setData(kRepoRole, QVariant::fromValue(repo));
    item->setToolTip(repo.description.isEmpty() ? repo.fullName : repo.description);
}

void RepositoryListWidget::onGithubSelectionChanged()
{
    auto repo = currentRepository();
    if (repo.isValid()) Q_EMIT repositoryActivated(repo);
}

// ----- Local section -------------------------------------------------------

void RepositoryListWidget::setLocalFolders(const QStringList& paths)
{
    localFolders_ = paths;
    rebuildLocalItems();
}

QString RepositoryListWidget::currentLocalFolder() const
{
    QListWidgetItem* item = localList_->currentItem();
    if (!item || !item->isSelected()) return {};
    return item->data(kPathRole).toString();
}

void RepositoryListWidget::selectLocalFolder(const QString& path)
{
    for (int i = 0; i < localList_->count(); ++i) {
        auto* item = localList_->item(i);
        if (item->data(kPathRole).toString() == path) {
            localList_->setCurrentItem(item);
            return;
        }
    }
}

void RepositoryListWidget::rebuildLocalItems()
{
    const QString filter = searchEdit_->text().trimmed().toLower();
    const QString prev   = currentLocalFolder();
    localList_->clear();

    QListWidgetItem* toReselect = nullptr;
    for (const auto& p : localFolders_) {
        if (!filter.isEmpty()) {
            const QString hay = QFileInfo(p).fileName().toLower();
            if (!hay.contains(filter)) continue;
        }
        auto* item = new QListWidgetItem(localList_);
        styleLocalItem(item, p);
        if (p == prev) toReselect = item;
    }
    if (toReselect) localList_->setCurrentItem(toReselect);
}

void RepositoryListWidget::styleLocalItem(QListWidgetItem* item, const QString& path)
{
    const QString name  = QFileInfo(path).fileName();
    const QString shown = QDir::toNativeSeparators(path);
    const QString label = QStringLiteral("%1\n  %2").arg(name, shown);
    item->setText(label);
    item->setData(kPathRole, path);
    item->setToolTip(shown);
}

void RepositoryListWidget::onLocalSelectionChanged()
{
    const QString path = currentLocalFolder();
    if (!path.isEmpty()) Q_EMIT localFolderActivated(path);
}

void RepositoryListWidget::onLocalContextMenu(const QPoint& pos)
{
    auto* item = localList_->itemAt(pos);
    if (!item) return;
    const QString path = item->data(kPathRole).toString();
    if (path.isEmpty()) return;

    QMenu menu(this);
    QAction* removeAct = menu.addAction(tr("Remove from sidebar"));
    if (menu.exec(localList_->viewport()->mapToGlobal(pos)) == removeAct) {
        Q_EMIT removeLocalFolderRequested(path);
    }
}

void RepositoryListWidget::onGithubContextMenu(const QPoint& pos)
{
    auto* item = githubList_->itemAt(pos);
    if (!item) return;
    const auto repo = item->data(kRepoRole).value<ghm::github::Repository>();
    if (!repo.isValid()) return;

    QMenu menu(this);
    // Toggle label tracks current state: if private show "Make
    // public", if public show "Make private". This is clearer than
    // a static "Change visibility…" entry because the action
    // outcome is right there in the verb.
    QAction* visAct = nullptr;
    if (repo.isPrivate) {
        visAct = menu.addAction(tr("Make public…"));
        visAct->setToolTip(tr(
            "Switch this repository from private to public. "
            "Anyone on the internet will be able to read it."));
    } else {
        visAct = menu.addAction(tr("Make private…"));
        visAct->setToolTip(tr(
            "Switch this repository from public to private. "
            "Stars and watchers will be erased; public forks "
            "will be detached."));
    }

    QAction* picked = menu.exec(githubList_->viewport()->mapToGlobal(pos));
    if (picked == visAct) {
        // Emit makePrivate = !current. Host handles confirmation
        // and the actual API call; we just signal intent.
        Q_EMIT changeVisibilityRequested(repo, !repo.isPrivate);
    }
}

// ----- Filter --------------------------------------------------------------

void RepositoryListWidget::onFilterChanged(const QString&)
{
    rebuildGithubItems();
    rebuildLocalItems();
}

} // namespace ghm::ui
