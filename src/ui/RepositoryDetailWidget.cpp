#include "ui/RepositoryDetailWidget.h"

#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QTreeView>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFrame>
#include <QFileInfo>
#include <QDir>
#include <QLocale>

namespace ghm::ui {

RepositoryDetailWidget::RepositoryDetailWidget(QWidget* parent)
    : QWidget(parent)
    , nameLabel_(new QLabel(this))
    , visibilityLabel_(new QLabel(this))
    , descriptionLabel_(new QLabel(this))
    , updatedLabel_(new QLabel(this))
    , localPathLabel_(new QLabel(this))
    , branchCombo_(new QComboBox(this))
    , statusLabel_(new QLabel(this))
    , cloneBtn_(new QPushButton(tr("Clone…"), this))
    , openBtn_(new QPushButton(tr("Open Local…"), this))
    , pullBtn_(new QPushButton(tr("Pull"), this))
    , pushBtn_(new QPushButton(tr("Push"), this))
    , refreshBtn_(new QPushButton(tr("Refresh"), this))
    , fsModel_(new QFileSystemModel(this))
    , fileTree_(new QTreeView(this))
{
    nameLabel_->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 600;"));
    nameLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    visibilityLabel_->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    descriptionLabel_->setWordWrap(true);
    descriptionLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    updatedLabel_->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
    localPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    localPathLabel_->setStyleSheet(QStringLiteral("color: #8a8a8a;"));

    // --- Metadata block ---
    auto* metaBox = new QGroupBox(tr("Repository"), this);
    auto* metaLay = new QVBoxLayout(metaBox);
    metaLay->addWidget(nameLabel_);
    metaLay->addWidget(visibilityLabel_);
    metaLay->addWidget(descriptionLabel_);
    metaLay->addWidget(updatedLabel_);
    metaLay->addWidget(localPathLabel_);

    // --- Action buttons ---
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(cloneBtn_);
    btnRow->addWidget(openBtn_);
    btnRow->addStretch();
    btnRow->addWidget(refreshBtn_);
    btnRow->addWidget(pullBtn_);
    btnRow->addWidget(pushBtn_);

    // --- Branch / status block ---
    auto* localBox  = new QGroupBox(tr("Working copy"), this);
    auto* localForm = new QFormLayout(localBox);
    localForm->addRow(tr("Branch:"), branchCombo_);
    localForm->addRow(tr("Status:"), statusLabel_);

    branchCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    // --- File tree ---
    fsModel_->setRootPath(QDir::homePath());     // placeholder root
    fsModel_->setReadOnly(true);
    fileTree_->setModel(fsModel_);
    fileTree_->setRootIndex(QModelIndex());      // hidden until repo loaded
    fileTree_->setHeaderHidden(false);
    fileTree_->setColumnHidden(2, true);          // type
    fileTree_->setColumnHidden(3, true);          // date
    fileTree_->hide();

    auto* root = new QVBoxLayout(this);
    root->addWidget(metaBox);
    root->addLayout(btnRow);
    root->addWidget(localBox);
    root->addWidget(fileTree_, 1);

    connect(cloneBtn_,   &QPushButton::clicked, this, &RepositoryDetailWidget::onClone);
    connect(openBtn_,    &QPushButton::clicked, this, &RepositoryDetailWidget::onOpen);
    connect(pullBtn_,    &QPushButton::clicked, this, &RepositoryDetailWidget::onPull);
    connect(pushBtn_,    &QPushButton::clicked, this, &RepositoryDetailWidget::onPush);
    connect(refreshBtn_, &QPushButton::clicked, this, &RepositoryDetailWidget::onRefresh);
    connect(branchCombo_, QOverload<int>::of(&QComboBox::activated),
            this, &RepositoryDetailWidget::onBranchActivated);

    showRepository({});  // empty state
}

RepositoryDetailWidget::~RepositoryDetailWidget() = default;

void RepositoryDetailWidget::showRepository(const ghm::github::Repository& repo)
{
    repo_ = repo;
    if (!repo.isValid()) {
        nameLabel_->setText(tr("No repository selected"));
        visibilityLabel_->clear();
        descriptionLabel_->clear();
        updatedLabel_->clear();
        localPathLabel_->clear();
        applyMode();
        return;
    }
    nameLabel_->setText(repo.fullName);
    visibilityLabel_->setText(repo.isPrivate ? tr("Private repository")
                                             : tr("Public repository"));
    descriptionLabel_->setText(repo.description.isEmpty()
        ? tr("(no description)") : repo.description);
    updatedLabel_->setText(tr("Last updated: %1").arg(
        repo.updatedAt.isValid()
            ? QLocale().toString(repo.updatedAt.toLocalTime(), QLocale::ShortFormat)
            : tr("unknown")));

    if (repo.localPath.isEmpty()) {
        localPathLabel_->setText(tr("Not cloned locally."));
    } else {
        localPathLabel_->setText(tr("Local path: %1").arg(repo.localPath));
    }
    applyMode();
}

void RepositoryDetailWidget::applyMode()
{
    const bool hasRepo  = repo_.isValid();
    const bool hasLocal = hasRepo && !repo_.localPath.isEmpty();

    cloneBtn_->setVisible (hasRepo && !hasLocal);
    openBtn_->setVisible  (hasRepo && !hasLocal);
    pullBtn_->setVisible  (hasLocal);
    pushBtn_->setVisible  (hasLocal);
    refreshBtn_->setVisible(hasLocal);
    branchCombo_->setEnabled(hasLocal);
    statusLabel_->setEnabled(hasLocal);

    if (hasLocal) {
        fsModel_->setRootPath(repo_.localPath);
        fileTree_->setRootIndex(fsModel_->index(repo_.localPath));
        fileTree_->show();
        Q_EMIT refreshRequested(repo_.localPath);
    } else {
        fileTree_->hide();
        clearStatus();
    }
}

void RepositoryDetailWidget::clearStatus()
{
    branchCombo_->blockSignals(true);
    branchCombo_->clear();
    branchCombo_->blockSignals(false);
    statusLabel_->setText(QStringLiteral("—"));
}

void RepositoryDetailWidget::updateStatus(const QString& currentBranch,
                                          const ghm::git::StatusSummary& s)
{
    QStringList parts;
    if (s.isClean()) {
        parts << tr("clean");
    } else {
        if (s.modified)   parts << tr("%1 modified").arg(s.modified);
        if (s.added)      parts << tr("%1 added").arg(s.added);
        if (s.deleted)    parts << tr("%1 deleted").arg(s.deleted);
        if (s.untracked)  parts << tr("%1 untracked").arg(s.untracked);
        if (s.conflicted) parts << tr("%1 conflicted").arg(s.conflicted);
    }
    if (s.ahead || s.behind) {
        parts << tr("%1 ahead, %2 behind").arg(s.ahead).arg(s.behind);
    }
    statusLabel_->setText(parts.join(QStringLiteral(", ")));

    // Reflect current branch even if it's not in the combo yet
    if (!currentBranch.isEmpty() &&
        branchCombo_->findText(currentBranch) < 0)
    {
        branchCombo_->blockSignals(true);
        branchCombo_->insertItem(0, currentBranch);
        branchCombo_->setCurrentIndex(0);
        branchCombo_->blockSignals(false);
    }
}

void RepositoryDetailWidget::setBranches(const QStringList& branches,
                                         const QString& current)
{
    branchCombo_->blockSignals(true);
    branchCombo_->clear();
    branchCombo_->addItems(branches);
    const int idx = branchCombo_->findText(current);
    if (idx >= 0) branchCombo_->setCurrentIndex(idx);
    branchCombo_->blockSignals(false);
}

void RepositoryDetailWidget::onClone()   { Q_EMIT cloneRequested(repo_); }
void RepositoryDetailWidget::onOpen()    { Q_EMIT openLocallyRequested(repo_); }
void RepositoryDetailWidget::onPull()    { Q_EMIT pullRequested(repo_.localPath); }
void RepositoryDetailWidget::onPush()    { Q_EMIT pushRequested(repo_.localPath); }
void RepositoryDetailWidget::onRefresh() { Q_EMIT refreshRequested(repo_.localPath); }

void RepositoryDetailWidget::onBranchActivated(int index)
{
    if (index < 0 || repo_.localPath.isEmpty()) return;
    const QString target = branchCombo_->itemText(index);
    if (target.isEmpty()) return;
    Q_EMIT switchBranchRequested(repo_.localPath, target);
}

} // namespace ghm::ui
