#include "ui/LocalRepositoryWidget.h"

#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QStackedWidget>
#include <QTabWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QMenu>
#include <QFileInfo>
#include <QDir>
#include <QLocale>
#include <QDateTime>
#include <QFontDatabase>

namespace ghm::ui {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;   // QListWidget item -> repo-relative path
constexpr int kCommitRole = Qt::UserRole + 1; // history item    -> CommitInfo
constexpr int kRemoteRole = Qt::UserRole + 1; // remotes item    -> remote name

QString relativeTime(const QDateTime& when)
{
    if (!when.isValid()) return QStringLiteral("—");
    const qint64 secs = when.secsTo(QDateTime::currentDateTimeUtc().toLocalTime());
    if (secs < 60)              return QObject::tr("just now");
    if (secs < 60 * 60)         return QObject::tr("%1 min ago").arg(secs / 60);
    if (secs < 60 * 60 * 24)    return QObject::tr("%1 h ago").arg(secs / 3600);
    if (secs < 60 * 60 * 24 * 30) return QObject::tr("%1 d ago").arg(secs / 86400);
    return QLocale().toString(when.date(), QLocale::ShortFormat);
}

// Render a status entry as a compact "[I W] path (← oldPath)" string,
// where each flag is a single status letter (or space for "unchanged").
QString formatStatusItem(const ghm::git::StatusEntry& e)
{
    QString line;
    line.reserve(e.path.size() + 16);
    line += QLatin1Char('[');
    line += e.indexFlag;
    line += e.worktreeFlag;
    line += QStringLiteral("]  ");
    line += e.path;
    if (!e.oldPath.isEmpty() && e.oldPath != e.path) {
        line += QStringLiteral("  ← ");
        line += e.oldPath;
    }
    return line;
}

QString humanStatusTooltip(const ghm::git::StatusEntry& e)
{
    QStringList parts;
    if (e.isStaged) {
        switch (e.indexFlag) {
            case 'A': parts << QObject::tr("Staged: new file"); break;
            case 'M': parts << QObject::tr("Staged: modified"); break;
            case 'D': parts << QObject::tr("Staged: deleted"); break;
            case 'R': parts << QObject::tr("Staged: renamed"); break;
            case 'T': parts << QObject::tr("Staged: type changed"); break;
        }
    }
    if (e.isUnstaged) {
        switch (e.worktreeFlag) {
            case 'M': parts << QObject::tr("Unstaged: modified"); break;
            case 'D': parts << QObject::tr("Unstaged: deleted"); break;
            case 'R': parts << QObject::tr("Unstaged: renamed"); break;
            case 'T': parts << QObject::tr("Unstaged: type changed"); break;
        }
    }
    if (e.isUntracked)  parts << QObject::tr("Untracked (new file, not added)");
    if (e.isConflicted) parts << QObject::tr("⚠ Merge conflict");
    return parts.join(QStringLiteral("\n"));
}

QFrame* hRule(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

} // namespace

// ---------------------------------------------------------------------------

LocalRepositoryWidget::LocalRepositoryWidget(QWidget* parent)
    : QWidget(parent)
    , folderLabel_(nullptr)
    , pathLabel_(nullptr)
    , branchLabel_(nullptr)
    , identityLabel_(nullptr)
    , identityEditBtn_(nullptr)
    , pages_(nullptr)
    , notRepoPage_(nullptr)
    , repoPage_(nullptr)
    , initBranchEdit_(nullptr)
    , initBtn_(nullptr)
    , tabs_(nullptr)
    , publishBanner_(nullptr)
    , publishBannerLabel_(nullptr)
    , publishBannerBtn_(nullptr)
    , changesList_(nullptr)
    , stageSelectedBtn_(nullptr)
    , unstageSelectedBtn_(nullptr)
    , stageAllBtn_(nullptr)
    , refreshBtn_(nullptr)
    , commitMessageEdit_(nullptr)
    , commitBtn_(nullptr)
    , commitHintLabel_(nullptr)
    , historyList_(nullptr)
    , historyDetail_(nullptr)
    , historyRefreshBtn_(nullptr)
    , remotesList_(nullptr)
    , publishRemoteBtn_(nullptr)
    , addRemoteBtn_(nullptr)
    , removeRemoteBtn_(nullptr)
    , pushRemoteCombo_(nullptr)
    , pushBranchLabel_(nullptr)
    , pushSetUpstreamBox_(nullptr)
    , pushBtn_(nullptr)
{
    buildUi();
}

LocalRepositoryWidget::~LocalRepositoryWidget() = default;

// ----- UI construction -----------------------------------------------------

void LocalRepositoryWidget::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* header = new QWidget(this);
    buildHeader(header);
    root->addWidget(header);

    root->addWidget(hRule(this));

    pages_       = new QStackedWidget(this);
    notRepoPage_ = buildNotRepoPage();
    repoPage_    = buildRepoPage();
    pages_->addWidget(notRepoPage_);
    pages_->addWidget(repoPage_);
    root->addWidget(pages_, /*stretch*/ 1);

    pages_->setCurrentWidget(notRepoPage_);
}

void LocalRepositoryWidget::buildHeader(QWidget* container)
{
    folderLabel_ = new QLabel(container);
    QFont f = folderLabel_->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    f.setBold(true);
    folderLabel_->setFont(f);

    pathLabel_ = new QLabel(container);
    pathLabel_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    pathLabel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    branchLabel_ = new QLabel(container);
    branchLabel_->setStyleSheet(
        QStringLiteral("padding: 2px 8px; border: 1px solid #444; border-radius: 10px; "
                       "background: #2a2f36;"));

    identityLabel_   = new QLabel(container);
    identityLabel_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    identityEditBtn_ = new QToolButton(container);
    identityEditBtn_->setText(tr("Edit"));
    identityEditBtn_->setAutoRaise(true);
    connect(identityEditBtn_, &QToolButton::clicked, this,
            &LocalRepositoryWidget::editIdentityRequested);

    auto* metaRow = new QHBoxLayout;
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->addWidget(branchLabel_);
    metaRow->addSpacing(12);
    metaRow->addWidget(identityLabel_);
    metaRow->addWidget(identityEditBtn_);
    metaRow->addStretch();

    auto* col = new QVBoxLayout(container);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);
    col->addWidget(folderLabel_);
    col->addWidget(pathLabel_);
    col->addSpacing(4);
    col->addLayout(metaRow);
}

QWidget* LocalRepositoryWidget::buildNotRepoPage()
{
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(20, 20, 20, 20);

    auto* card = new QFrame(page);
    card->setObjectName(QStringLiteral("notRepoCard"));
    card->setStyleSheet(QStringLiteral(
        "#notRepoCard { background: #2a2f36; border: 1px solid #3c4148; "
        "border-radius: 8px; padding: 18px; }"));

    auto* title = new QLabel(tr("This folder is not a Git repository yet."), card);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.15);
    tf.setBold(true);
    title->setFont(tf);

    auto* hint = new QLabel(
        tr("Initializing creates a hidden <code>.git</code> directory and prepares the "
           "folder for tracking changes. Pick the name of the initial branch — "
           "<b>master</b> matches GitHub's default 'git push origin master' instructions; "
           "<b>main</b> is the modern default for new GitHub repos."), card);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);
    hint->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    initBranchEdit_ = new QLineEdit(defaultInitBranch_, card);
    initBranchEdit_->setMaximumWidth(220);
    initBranchEdit_->setPlaceholderText(QStringLiteral("master"));

    initBtn_ = new QPushButton(tr("Initialize Repository"), card);
    initBtn_->setDefault(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Initial branch:"), initBranchEdit_);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(initBtn_);
    btnRow->addStretch();

    auto* cardCol = new QVBoxLayout(card);
    cardCol->addWidget(title);
    cardCol->addSpacing(4);
    cardCol->addWidget(hint);
    cardCol->addSpacing(12);
    cardCol->addLayout(form);
    cardCol->addLayout(btnRow);

    col->addWidget(card);
    col->addStretch();

    connect(initBtn_, &QPushButton::clicked,
            this, &LocalRepositoryWidget::onInitClicked);
    return page;
}

QWidget* LocalRepositoryWidget::buildRepoPage()
{
    auto* page = new QWidget(this);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);

    tabs_ = new QTabWidget(page);
    tabs_->addTab(buildChangesTab(), tr("Changes"));
    tabs_->addTab(buildHistoryTab(), tr("History"));
    tabs_->addTab(buildRemotesTab(), tr("Remotes"));
    connect(tabs_, &QTabWidget::currentChanged,
            this, &LocalRepositoryWidget::onTabChanged);

    col->addWidget(tabs_);
    return page;
}

QWidget* LocalRepositoryWidget::buildChangesTab()
{
    auto* page = new QWidget(this);

    // ── Publish-to-GitHub banner ─────────────────────────────────────
    // Shown only when the repo has at least one commit but no `origin`
    // remote. We make it look like a primary call-to-action so the
    // happy path "init → commit → publish to GitHub" is obvious.
    publishBanner_ = new QFrame(page);
    publishBanner_->setObjectName(QStringLiteral("publishBanner"));
    publishBanner_->setStyleSheet(QStringLiteral(
        "#publishBanner { background: #1f3a5f; border: 1px solid #2c4f7c; "
        "border-radius: 6px; padding: 10px; }"));
    publishBanner_->setVisible(false);

    publishBannerLabel_ = new QLabel(publishBanner_);
    publishBannerLabel_->setText(
        tr("<b>Ready to publish?</b><br>"
           "This folder isn't connected to a GitHub repository yet. "
           "Create a new one or link to an existing repo to push your commits."));
    publishBannerLabel_->setTextFormat(Qt::RichText);
    publishBannerLabel_->setWordWrap(true);

    publishBannerBtn_ = new QPushButton(tr("Publish to GitHub…"), publishBanner_);
    publishBannerBtn_->setDefault(false);
    publishBannerBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2d6cdf; color: white; border: 0; "
        "padding: 6px 14px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover  { background: #3a7af0; } "
        "QPushButton:pressed{ background: #245bbf; }"));
    connect(publishBannerBtn_, &QPushButton::clicked,
            this, &LocalRepositoryWidget::onPublishToGitHubClicked);

    auto* bannerRow = new QHBoxLayout(publishBanner_);
    bannerRow->setContentsMargins(10, 8, 10, 8);
    bannerRow->addWidget(publishBannerLabel_, 1);
    bannerRow->addSpacing(8);
    bannerRow->addWidget(publishBannerBtn_);

    // ── Files list ──────────────────────────────────────────────────
    changesList_ = new QListWidget(page);
    changesList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    changesList_->setUniformItemSizes(true);
    changesList_->setAlternatingRowColors(true);
    changesList_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    changesList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(changesList_, &QListWidget::itemDoubleClicked,
            this, &LocalRepositoryWidget::onChangesItemDoubleClicked);
    connect(changesList_, &QListWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        if (busy_) return;
        QMenu menu(this);
        QAction* stage   = menu.addAction(tr("Stage"));
        QAction* unstage = menu.addAction(tr("Unstage"));
        const auto sel = changesList_->selectedItems();
        stage  ->setEnabled(!sel.isEmpty());
        unstage->setEnabled(!sel.isEmpty());
        QAction* picked = menu.exec(changesList_->viewport()->mapToGlobal(pos));
        if (picked == stage)   onStageSelectedClicked();
        if (picked == unstage) onUnstageSelectedClicked();
    });

    stageSelectedBtn_   = new QPushButton(tr("Stage selected"),   page);
    unstageSelectedBtn_ = new QPushButton(tr("Unstage selected"), page);
    stageAllBtn_        = new QPushButton(tr("Stage all"),        page);
    refreshBtn_         = new QPushButton(tr("Refresh"),          page);
    refreshBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));

    connect(stageSelectedBtn_,   &QPushButton::clicked, this,
            &LocalRepositoryWidget::onStageSelectedClicked);
    connect(unstageSelectedBtn_, &QPushButton::clicked, this,
            &LocalRepositoryWidget::onUnstageSelectedClicked);
    connect(stageAllBtn_,        &QPushButton::clicked, this,
            &LocalRepositoryWidget::onStageAllClicked);
    connect(refreshBtn_,         &QPushButton::clicked, this,
            &LocalRepositoryWidget::onRefreshClicked);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(stageSelectedBtn_);
    btnRow->addWidget(unstageSelectedBtn_);
    btnRow->addStretch();
    btnRow->addWidget(stageAllBtn_);
    btnRow->addWidget(refreshBtn_);

    commitMessageEdit_ = new QPlainTextEdit(page);
    commitMessageEdit_->setPlaceholderText(
        tr("Commit message — first line is the summary, blank line, then optional details."));
    commitMessageEdit_->setMaximumHeight(120);
    connect(commitMessageEdit_, &QPlainTextEdit::textChanged,
            this, &LocalRepositoryWidget::updateCommitButton);

    commitBtn_ = new QPushButton(tr("Commit"), page);
    commitBtn_->setDefault(true);
    connect(commitBtn_, &QPushButton::clicked,
            this, &LocalRepositoryWidget::onCommitClicked);

    commitHintLabel_ = new QLabel(page);
    commitHintLabel_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    commitHintLabel_->setWordWrap(true);

    auto* commitRow = new QHBoxLayout;
    commitRow->addWidget(commitHintLabel_, 1);
    commitRow->addWidget(commitBtn_);

    auto* col = new QVBoxLayout(page);
    col->addWidget(publishBanner_);
    col->addWidget(changesList_, 1);
    col->addLayout(btnRow);
    col->addWidget(hRule(page));
    col->addWidget(commitMessageEdit_);
    col->addLayout(commitRow);

    updateCommitButton();
    return page;
}

QWidget* LocalRepositoryWidget::buildHistoryTab()
{
    auto* page = new QWidget(this);

    historyList_ = new QListWidget(page);
    historyList_->setUniformItemSizes(true);
    historyList_->setAlternatingRowColors(true);
    connect(historyList_, &QListWidget::itemSelectionChanged,
            this, &LocalRepositoryWidget::onHistorySelectionChanged);

    historyDetail_ = new QPlainTextEdit(page);
    historyDetail_->setReadOnly(true);
    historyDetail_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    historyDetail_->setPlaceholderText(tr("Select a commit to see its full message."));

    auto* split = new QSplitter(Qt::Vertical, page);
    split->addWidget(historyList_);
    split->addWidget(historyDetail_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);

    historyRefreshBtn_ = new QPushButton(tr("Refresh"), page);
    historyRefreshBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(historyRefreshBtn_, &QPushButton::clicked,
            this, &LocalRepositoryWidget::onHistoryRefreshClicked);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(historyRefreshBtn_);

    auto* col = new QVBoxLayout(page);
    col->addWidget(split, 1);
    col->addLayout(btnRow);
    return page;
}

QWidget* LocalRepositoryWidget::buildRemotesTab()
{
    auto* page = new QWidget(this);

    remotesList_ = new QListWidget(page);
    remotesList_->setUniformItemSizes(true);
    remotesList_->setAlternatingRowColors(true);
    remotesList_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    connect(remotesList_, &QListWidget::itemSelectionChanged, this,
            [this] {
        const bool any = !remotesList_->selectedItems().isEmpty();
        if (removeRemoteBtn_) removeRemoteBtn_->setEnabled(any && !busy_);
    });

    addRemoteBtn_    = new QPushButton(tr("Add remote…"), page);
    removeRemoteBtn_ = new QPushButton(tr("Remove"),     page);
    publishRemoteBtn_ = new QPushButton(tr("Publish to GitHub…"), page);
    publishRemoteBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #2d6cdf; color: white; border: 0; "
        "padding: 5px 12px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover  { background: #3a7af0; } "
        "QPushButton:pressed{ background: #245bbf; } "
        "QPushButton:disabled { background: #3a3f47; color: #707b86; }"));
    removeRemoteBtn_->setEnabled(false);
    connect(addRemoteBtn_,    &QPushButton::clicked,
            this, &LocalRepositoryWidget::onAddRemoteClicked);
    connect(removeRemoteBtn_, &QPushButton::clicked,
            this, &LocalRepositoryWidget::onRemoveRemoteClicked);
    connect(publishRemoteBtn_, &QPushButton::clicked,
            this, &LocalRepositoryWidget::onPublishToGitHubClicked);

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(publishRemoteBtn_);
    topRow->addSpacing(8);
    topRow->addWidget(addRemoteBtn_);
    topRow->addWidget(removeRemoteBtn_);
    topRow->addStretch();

    // Push panel.
    pushRemoteCombo_   = new QComboBox(page);
    pushBranchLabel_   = new QLabel(page);
    pushBranchLabel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    pushSetUpstreamBox_ = new QCheckBox(tr("Set upstream (-u)"), page);
    pushSetUpstreamBox_->setToolTip(
        tr("Equivalent to 'git push -u origin <branch>'. Recommended for the first push."));
    pushSetUpstreamBox_->setChecked(true);

    pushBtn_ = new QPushButton(tr("Push"), page);
    connect(pushBtn_, &QPushButton::clicked,
            this, &LocalRepositoryWidget::onPushClicked);

    auto* form = new QFormLayout;
    form->addRow(tr("Remote:"), pushRemoteCombo_);
    form->addRow(tr("Branch:"), pushBranchLabel_);

    auto* pushRow = new QHBoxLayout;
    pushRow->addWidget(pushSetUpstreamBox_);
    pushRow->addStretch();
    pushRow->addWidget(pushBtn_);

    auto* pushBox = new QFrame(page);
    pushBox->setObjectName(QStringLiteral("pushBox"));
    pushBox->setStyleSheet(QStringLiteral(
        "#pushBox { background: #232830; border: 1px solid #3c4148; "
        "border-radius: 6px; padding: 8px; }"));
    auto* pushCol = new QVBoxLayout(pushBox);
    auto* pushTitle = new QLabel(tr("Push"), pushBox);
    QFont pf = pushTitle->font(); pf.setBold(true); pushTitle->setFont(pf);
    pushCol->addWidget(pushTitle);
    pushCol->addLayout(form);
    pushCol->addLayout(pushRow);

    auto* col = new QVBoxLayout(page);
    col->addLayout(topRow);
    col->addWidget(remotesList_, 1);
    col->addWidget(hRule(page));
    col->addWidget(pushBox);
    return page;
}

// ----- Public API ----------------------------------------------------------

void LocalRepositoryWidget::setFolder(const QString& path)
{
    path_ = path;
    isRepository_ = false;
    historyLoaded_ = false;
    branch_.clear();
    remotes_.clear();

    folderLabel_->setText(QFileInfo(path).fileName());
    pathLabel_->setText(QDir::toNativeSeparators(path));

    branchLabel_->setText(QStringLiteral("—"));
    if (changesList_)  changesList_->clear();
    if (historyList_)  historyList_->clear();
    if (historyDetail_) historyDetail_->clear();
    if (remotesList_)  remotesList_->clear();
    if (pushRemoteCombo_) pushRemoteCombo_->clear();
    if (commitMessageEdit_) commitMessageEdit_->clear();
    if (publishBanner_) publishBanner_->setVisible(false);

    pages_->setCurrentWidget(notRepoPage_);
    if (initBranchEdit_) initBranchEdit_->setText(defaultInitBranch_);
    updateIdentityBar();
}

void LocalRepositoryWidget::setLocalState(
    bool isRepository,
    const QString& branch,
    const std::vector<ghm::git::StatusEntry>& entries,
    const std::vector<ghm::git::RemoteInfo>&  remotes)
{
    isRepository_ = isRepository;
    branch_       = branch;
    remotes_      = remotes;

    if (!isRepository) {
        pages_->setCurrentWidget(notRepoPage_);
        branchLabel_->setText(QStringLiteral("—"));
        return;
    }

    pages_->setCurrentWidget(repoPage_);
    branchLabel_->setText(branch.isEmpty() ? QStringLiteral("(unborn)") : branch);

    rebuildChangesList(entries);
    rebuildRemotesList(remotes);
    updateCommitButton();
    updatePushPanel();

    // Decide whether the "Publish to GitHub" call-to-action makes sense.
    //   * Need at least one commit (otherwise nothing to push)
    //   * Need no `origin` remote (otherwise the user already wired it)
    bool hasOrigin = false;
    for (const auto& r : remotes) {
        if (r.name == QLatin1String("origin")) { hasOrigin = true; break; }
    }
    const bool unborn = branch.isEmpty() || branch.startsWith(QLatin1Char('('));
    const bool wantsPublish = !unborn && !hasOrigin;

    if (publishBanner_) publishBanner_->setVisible(wantsPublish);
    if (publishRemoteBtn_) {
        publishRemoteBtn_->setVisible(!hasOrigin);
        // Even if origin exists, we still allow re-publishing if the user
        // wants to (e.g. they removed origin); just don't push it.
    }
}

void LocalRepositoryWidget::setHistory(const std::vector<ghm::git::CommitInfo>& commits)
{
    historyLoaded_ = true;
    historyList_->clear();
    historyDetail_->clear();

    if (commits.empty()) {
        auto* item = new QListWidgetItem(
            tr("No commits yet — make your first commit in the Changes tab."),
            historyList_);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        return;
    }

    for (const auto& c : commits) {
        const QString line = QStringLiteral("%1  %2  —  %3, %4")
            .arg(c.shortId, c.summary, c.authorName, relativeTime(c.when));
        auto* item = new QListWidgetItem(line, historyList_);
        item->setData(kCommitRole, QVariant::fromValue(c.id));
        // Stash extra fields in tooltip; the details panel renders the full message.
        item->setToolTip(QStringLiteral("%1\n%2 <%3>")
                         .arg(c.id, c.authorName, c.authorEmail));
        // Stash full info as user data on the item via dynamic properties:
        item->setData(Qt::UserRole + 2, c.message);
        item->setData(Qt::UserRole + 3, c.authorName);
        item->setData(Qt::UserRole + 4, c.authorEmail);
        item->setData(Qt::UserRole + 5, c.when);
    }
}

void LocalRepositoryWidget::setIdentity(const QString& name, const QString& email)
{
    identityName_  = name;
    identityEmail_ = email;
    updateIdentityBar();
    updateCommitButton();
}

void LocalRepositoryWidget::setBusy(bool busy)
{
    busy_ = busy;
    const bool en = !busy;
    if (initBtn_)            initBtn_->setEnabled(en);
    if (stageSelectedBtn_)   stageSelectedBtn_->setEnabled(en);
    if (unstageSelectedBtn_) unstageSelectedBtn_->setEnabled(en);
    if (stageAllBtn_)        stageAllBtn_->setEnabled(en);
    if (refreshBtn_)         refreshBtn_->setEnabled(en);
    if (commitBtn_)          commitBtn_->setEnabled(en);
    if (historyRefreshBtn_)  historyRefreshBtn_->setEnabled(en);
    if (addRemoteBtn_)       addRemoteBtn_->setEnabled(en);
    if (removeRemoteBtn_)    removeRemoteBtn_->setEnabled(
        en && remotesList_ && !remotesList_->selectedItems().isEmpty());
    if (pushBtn_)            pushBtn_->setEnabled(
        en && pushRemoteCombo_ && pushRemoteCombo_->count() > 0);
    updateCommitButton();
}

void LocalRepositoryWidget::setDefaultInitBranch(const QString& branch)
{
    defaultInitBranch_ = branch.isEmpty() ? QStringLiteral("master") : branch;
    if (initBranchEdit_ && !isRepository_) {
        initBranchEdit_->setText(defaultInitBranch_);
    }
}

// ----- Slot handlers -------------------------------------------------------

void LocalRepositoryWidget::onInitClicked()
{
    if (path_.isEmpty()) return;
    QString branch = initBranchEdit_->text().trimmed();
    if (branch.isEmpty()) branch = QStringLiteral("master");
    Q_EMIT initRequested(path_, branch);
}

void LocalRepositoryWidget::onStageSelectedClicked()
{
    if (path_.isEmpty()) return;
    const auto paths = selectedChangedPaths(/*stagedOnly*/ false, /*unstagedOnly*/ false);
    if (paths.isEmpty()) return;
    Q_EMIT stagePathsRequested(path_, paths);
}

void LocalRepositoryWidget::onUnstageSelectedClicked()
{
    if (path_.isEmpty()) return;
    const auto paths = selectedChangedPaths(/*stagedOnly*/ false, /*unstagedOnly*/ false);
    if (paths.isEmpty()) return;
    Q_EMIT unstagePathsRequested(path_, paths);
}

void LocalRepositoryWidget::onStageAllClicked()
{
    if (path_.isEmpty()) return;
    Q_EMIT stageAllRequested(path_);
}

void LocalRepositoryWidget::onRefreshClicked()
{
    if (path_.isEmpty()) return;
    Q_EMIT refreshRequested(path_);
}

void LocalRepositoryWidget::onCommitClicked()
{
    if (path_.isEmpty()) return;
    const QString msg = commitMessageEdit_->toPlainText().trimmed();
    if (msg.isEmpty()) return;
    Q_EMIT commitRequested(path_, msg);
}

void LocalRepositoryWidget::onChangesItemDoubleClicked(QListWidgetItem* item)
{
    if (!item || busy_) return;
    const QString path = item->data(kPathRole).toString();
    if (path.isEmpty()) return;

    // Toggle: if any index flag set -> unstage; else -> stage.
    // We don't have the flag directly; cheapest is to look at the prefix.
    const QString text = item->text();
    bool isStaged = false;
    if (text.size() >= 4 && text[0] == QLatin1Char('[')) {
        const QChar idx = text[1];
        isStaged = (idx != QLatin1Char(' '));
    }
    if (isStaged) Q_EMIT unstagePathsRequested(path_, { path });
    else          Q_EMIT stagePathsRequested  (path_, { path });
}

void LocalRepositoryWidget::onTabChanged(int index)
{
    // Tab 1 = History. Lazy-load on first activation; Refresh button
    // forces a reload afterwards.
    if (!isRepository_ || path_.isEmpty()) return;
    if (index == 1 && !historyLoaded_) {
        Q_EMIT historyRequested(path_);
    }
}

void LocalRepositoryWidget::onHistoryRefreshClicked()
{
    if (path_.isEmpty()) return;
    Q_EMIT historyRequested(path_);
}

void LocalRepositoryWidget::onHistorySelectionChanged()
{
    auto items = historyList_->selectedItems();
    if (items.isEmpty()) { historyDetail_->clear(); return; }
    auto* it = items.first();
    const QString id      = it->data(kCommitRole).toString();
    const QString message = it->data(Qt::UserRole + 2).toString();
    const QString name    = it->data(Qt::UserRole + 3).toString();
    const QString email   = it->data(Qt::UserRole + 4).toString();
    const QDateTime when  = it->data(Qt::UserRole + 5).toDateTime();

    QString text;
    text += QStringLiteral("commit %1\n").arg(id);
    text += QStringLiteral("Author: %1 <%2>\n").arg(name, email);
    text += QStringLiteral("Date:   %1\n\n")
              .arg(QLocale().toString(when, QLocale::LongFormat));
    text += message;
    historyDetail_->setPlainText(text);
}

void LocalRepositoryWidget::onAddRemoteClicked()
{
    if (path_.isEmpty()) return;
    Q_EMIT addRemoteRequested(path_);   // MainWindow opens the dialog
}

void LocalRepositoryWidget::onRemoveRemoteClicked()
{
    if (path_.isEmpty()) return;
    auto items = remotesList_->selectedItems();
    if (items.isEmpty()) return;
    const QString name = items.first()->data(kRemoteRole).toString();
    if (!name.isEmpty()) Q_EMIT removeRemoteRequested(path_, name);
}

void LocalRepositoryWidget::onPushClicked()
{
    if (path_.isEmpty() || branch_.isEmpty() ||
        branch_.startsWith(QLatin1Char('('))) {
        return;
    }
    const QString remote = selectedRemote();
    if (remote.isEmpty()) return;
    Q_EMIT pushLocalRequested(path_, remote, branch_,
                              pushSetUpstreamBox_->isChecked());
}

void LocalRepositoryWidget::onPublishToGitHubClicked()
{
    if (path_.isEmpty()) return;
    Q_EMIT publishToGitHubRequested(path_);
}

// ----- Helpers -------------------------------------------------------------

QString LocalRepositoryWidget::selectedRemote() const
{
    if (!pushRemoteCombo_ || pushRemoteCombo_->count() == 0) return {};
    return pushRemoteCombo_->currentText();
}

QStringList LocalRepositoryWidget::selectedChangedPaths(bool /*stagedOnly*/,
                                                        bool /*unstagedOnly*/) const
{
    QStringList out;
    const auto items = changesList_->selectedItems();
    for (auto* it : items) {
        const QString p = it->data(kPathRole).toString();
        if (!p.isEmpty()) out << p;
    }
    return out;
}

void LocalRepositoryWidget::rebuildChangesList(
    const std::vector<ghm::git::StatusEntry>& entries)
{
    changesList_->clear();
    if (entries.empty()) {
        auto* item = new QListWidgetItem(
            tr("✓ Working tree is clean — nothing to commit."), changesList_);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        return;
    }
    // Order: staged first, then unstaged, then untracked. Within each
    // group, alphabetical by path.
    std::vector<const ghm::git::StatusEntry*> staged, unstaged, untracked, conflicted;
    for (const auto& e : entries) {
        if (e.isConflicted)       conflicted.push_back(&e);
        else if (e.isUntracked)   untracked.push_back(&e);
        else if (e.isStaged)      staged.push_back(&e);
        else                      unstaged.push_back(&e);
    }
    auto byPath = [](const ghm::git::StatusEntry* a, const ghm::git::StatusEntry* b) {
        return a->path < b->path;
    };
    std::sort(staged.begin(),     staged.end(),     byPath);
    std::sort(unstaged.begin(),   unstaged.end(),   byPath);
    std::sort(untracked.begin(),  untracked.end(),  byPath);
    std::sort(conflicted.begin(), conflicted.end(), byPath);

    auto addAll = [&](const auto& vec) {
        for (const auto* e : vec) {
            auto* item = new QListWidgetItem(formatStatusItem(*e), changesList_);
            item->setData(kPathRole, e->path);
            item->setToolTip(humanStatusTooltip(*e));
        }
    };
    addAll(conflicted);
    addAll(staged);
    addAll(unstaged);
    addAll(untracked);
}

void LocalRepositoryWidget::rebuildRemotesList(
    const std::vector<ghm::git::RemoteInfo>& remotes)
{
    remotesList_->clear();
    pushRemoteCombo_->clear();
    if (remotes.empty()) {
        auto* hint = new QListWidgetItem(
            tr("No remotes — click 'Add remote…' and paste your "
               "'git remote add origin …' command."), remotesList_);
        hint->setFlags(hint->flags() & ~Qt::ItemIsSelectable);
    } else {
        for (const auto& r : remotes) {
            QString line = QStringLiteral("%1  →  %2").arg(r.name, r.url);
            if (!r.pushUrl.isEmpty() && r.pushUrl != r.url) {
                line += QStringLiteral("    (push: %1)").arg(r.pushUrl);
            }
            auto* item = new QListWidgetItem(line, remotesList_);
            item->setData(kRemoteRole, r.name);
            item->setToolTip(r.url);
            pushRemoteCombo_->addItem(r.name);
        }
        // Default-select 'origin' if present.
        const int idx = pushRemoteCombo_->findText(QStringLiteral("origin"));
        if (idx >= 0) pushRemoteCombo_->setCurrentIndex(idx);
    }
    if (removeRemoteBtn_) {
        removeRemoteBtn_->setEnabled(false);
    }
}

void LocalRepositoryWidget::updateCommitButton()
{
    if (!commitBtn_) return;
    const bool hasMessage  = !commitMessageEdit_->toPlainText().trimmed().isEmpty();
    const bool hasIdentity = !identityName_.isEmpty() && !identityEmail_.isEmpty();
    commitBtn_->setEnabled(!busy_ && hasMessage);

    if (!hasIdentity) {
        commitHintLabel_->setText(
            tr("Author identity not set — you'll be asked for your name and email "
               "when you commit."));
    } else {
        commitHintLabel_->setText(
            tr("Will be committed as <b>%1</b> &lt;%2&gt;.")
                .arg(identityName_, identityEmail_));
        commitHintLabel_->setTextFormat(Qt::RichText);
    }
}

void LocalRepositoryWidget::updatePushPanel()
{
    if (pushBranchLabel_) {
        pushBranchLabel_->setText(branch_.isEmpty() ? QStringLiteral("—") : branch_);
    }
    if (pushBtn_) {
        const bool can = !busy_
                      && pushRemoteCombo_ && pushRemoteCombo_->count() > 0
                      && !branch_.isEmpty()
                      && !branch_.startsWith(QLatin1Char('('));
        pushBtn_->setEnabled(can);
    }
}

void LocalRepositoryWidget::updateIdentityBar()
{
    if (!identityLabel_) return;
    if (identityName_.isEmpty() || identityEmail_.isEmpty()) {
        identityLabel_->setText(tr("Author: not configured"));
    } else {
        identityLabel_->setText(
            tr("Author: %1 <%2>").arg(identityName_, identityEmail_));
    }
}

} // namespace ghm::ui
