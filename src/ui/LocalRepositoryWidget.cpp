#include "ui/LocalRepositoryWidget.h"
#include "ui/DiffViewWidget.h"
#include "core/TimeFormatting.h"
#include "core/Settings.h"

#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QShortcut>
#include <QGuiApplication>
#include <QClipboard>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
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
#include <QFileSystemWatcher>
#include <QTimer>
#include <QCursor>

namespace ghm::ui {

namespace {

constexpr int kPathRole = Qt::UserRole + 1;   // QListWidget item -> repo-relative path
constexpr int kCommitRole   = Qt::UserRole + 1; // history item    -> CommitInfo
constexpr int kLoadMoreRole = Qt::UserRole + 10; // sentinel row marker
                                                  // (bool — true == sentinel)
constexpr int kRemoteRole = Qt::UserRole + 1; // remotes item    -> remote name

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
    , branchButton_(nullptr)
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
    , changesSplitter_(nullptr)
    , changesList_(nullptr)
    , stageSelectedBtn_(nullptr)
    , unstageSelectedBtn_(nullptr)
    , stageAllBtn_(nullptr)
    , refreshBtn_(nullptr)
    , commitMessageEdit_(nullptr)
    , commitBtn_(nullptr)
    , commitHintLabel_(nullptr)
    , diffView_(nullptr)
    , historyList_(nullptr)
    , commitFilter_(nullptr)
    , historyDetail_(nullptr)
    , historyRefreshBtn_(nullptr)
    , commitFilesList_(nullptr)
    , commitDiffView_(nullptr)
    , remotesList_(nullptr)
    , publishRemoteBtn_(nullptr)
    , addRemoteBtn_(nullptr)
    , removeRemoteBtn_(nullptr)
    , pushRemoteCombo_(nullptr)
    , pushBranchLabel_(nullptr)
    , pushSetUpstreamBox_(nullptr)
    , pushBtn_(nullptr)
    , watcher_(new QFileSystemWatcher(this))
    , autoRefreshTimer_(new QTimer(this))
{
    autoRefreshTimer_->setSingleShot(true);
    // 300ms feels responsive without thrashing during a `git add .` from
    // the CLI that touches the index a couple of times in quick succession.
    autoRefreshTimer_->setInterval(300);
    connect(autoRefreshTimer_, &QTimer::timeout,
            this, &LocalRepositoryWidget::onAutoRefreshTimeout);
    connect(watcher_, &QFileSystemWatcher::fileChanged,
            this, &LocalRepositoryWidget::onWatchedPathChanged);
    connect(watcher_, &QFileSystemWatcher::directoryChanged,
            this, &LocalRepositoryWidget::onWatchedPathChanged);

    // Debounce verify dispatch on scroll. 150ms quiet period is a
    // compromise: short enough that the verify badges appear "soon
    // after I stop scrolling" but long enough to absorb a flick or
    // a Page Down keystroke that scrolls past many rows quickly.
    verifyDebounce_ = new QTimer(this);
    verifyDebounce_->setSingleShot(true);
    verifyDebounce_->setInterval(150);
    connect(verifyDebounce_, &QTimer::timeout,
            this, &LocalRepositoryWidget::dispatchVerifyForVisibleRows);

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

    branchButton_ = new QToolButton(container);
    branchButton_->setText(QStringLiteral("—"));
    branchButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    branchButton_->setPopupMode(QToolButton::InstantPopup);
    branchButton_->setCursor(Qt::PointingHandCursor);
    branchButton_->setStyleSheet(QStringLiteral(
        "QToolButton { padding: 3px 10px; border: 1px solid #444; "
        "border-radius: 10px; background: #2a2f36; color: #d8dde2; } "
        "QToolButton:hover { background: #343a44; border-color: #5a6070; } "
        "QToolButton::menu-indicator { image: none; } " // we draw our own ▾
    ));
    // Ensure the click opens our popup rather than nothing — we handle
    // it ourselves so the popup can be rebuilt fresh from branchInfos_
    // on each open (catches branches that materialised externally).
    connect(branchButton_, &QToolButton::clicked,
            this, &LocalRepositoryWidget::onBranchButtonClicked);

    identityLabel_   = new QLabel(container);
    identityLabel_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    identityEditBtn_ = new QToolButton(container);
    identityEditBtn_->setText(tr("Edit"));
    identityEditBtn_->setAutoRaise(true);
    connect(identityEditBtn_, &QToolButton::clicked, this,
            &LocalRepositoryWidget::editIdentityRequested);

    auto* metaRow = new QHBoxLayout;
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->addWidget(branchButton_);
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
    tabs_->addTab(buildChangesTab(),    tr("Changes"));
    tabs_->addTab(buildHistoryTab(),    tr("History"));
    tabs_->addTab(buildRemotesTab(),    tr("Remotes"));
    tabs_->addTab(buildSubmodulesTab(), tr("Submodules"));
    connect(tabs_, &QTabWidget::currentChanged,
            this, &LocalRepositoryWidget::onTabChanged);

    // F5 shortcut, widget-wide. Dispatch depends on which tab is
    // currently active:
    //   * Changes  → working-tree status refresh
    //   * History  → re-fetch commit log
    //   * Remotes / Submodules → working-tree refresh (fallback;
    //     these tabs share the localState payload anyway)
    // We use one shortcut + tab check rather than per-tab shortcuts
    // because Qt::WidgetWithChildrenShortcut scopes overlap across
    // sibling tabs (they all live inside `tabs_`) and would conflict.
    {
        auto* sc = new QShortcut(QKeySequence(Qt::Key_F5), this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        connect(sc, &QShortcut::activated, this, [this] {
            if (!tabs_ || path_.isEmpty() || !isRepository_) return;
            if (tabs_->currentIndex() == 1 && historyRefreshBtn_) {
                historyRefreshBtn_->click();
            } else if (refreshBtn_) {
                refreshBtn_->click();
            }
        });
    }

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

    // ── Conflict banner ────────────────────────────────────────────
    // Higher-priority than publish: if there's a merge in progress
    // with conflicts, that's the user's most pressing concern. Red
    // colour scheme distinguishes it from the (informational) blue
    // publish banner.
    conflictBanner_ = new QFrame(page);
    conflictBanner_->setObjectName(QStringLiteral("conflictBanner"));
    conflictBanner_->setStyleSheet(QStringLiteral(
        "#conflictBanner { background: #4a1f1f; border: 1px solid #7c2c2c; "
        "border-radius: 6px; padding: 10px; }"));
    conflictBanner_->setVisible(false);

    conflictBannerLabel_ = new QLabel(conflictBanner_);
    conflictBannerLabel_->setTextFormat(Qt::RichText);
    conflictBannerLabel_->setWordWrap(true);

    conflictBannerBtn_ = new QPushButton(tr("Resolve conflicts…"),
                                         conflictBanner_);
    conflictBannerBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #c0392b; color: white; border: 0; "
        "padding: 6px 14px; border-radius: 4px; font-weight: bold; } "
        "QPushButton:hover  { background: #d04432; } "
        "QPushButton:pressed{ background: #a02f23; }"));
    connect(conflictBannerBtn_, &QPushButton::clicked, this, [this] {
        if (!path_.isEmpty()) Q_EMIT resolveConflictsRequested(path_);
    });

    auto* conflictRow = new QHBoxLayout(conflictBanner_);
    conflictRow->setContentsMargins(10, 8, 10, 8);
    conflictRow->addWidget(conflictBannerLabel_, 1);
    conflictRow->addSpacing(8);
    conflictRow->addWidget(conflictBannerBtn_);

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
    refreshBtn_->setToolTip(tr("Refresh status (Ctrl+R or F5)"));

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

    // ── Diff panel ─────────────────────────────────────────────────
    diffView_ = new DiffViewWidget(page);
    connect(changesList_, &QListWidget::itemSelectionChanged,
            this, &LocalRepositoryWidget::onChangesSelectionChanged);

    // Top half: file list + buttons + commit form. Bottom half: diff.
    // QSplitter handles vertical resizing, sticky positions on user drag.
    auto* topPane = new QWidget(page);
    auto* topCol  = new QVBoxLayout(topPane);
    topCol->setContentsMargins(0, 0, 0, 0);
    topCol->addWidget(conflictBanner_);   // higher priority — show first
    topCol->addWidget(publishBanner_);
    topCol->addWidget(changesList_, 1);
    topCol->addLayout(btnRow);
    topCol->addWidget(hRule(topPane));
    topCol->addWidget(commitMessageEdit_);
    topCol->addLayout(commitRow);

    changesSplitter_ = new QSplitter(Qt::Vertical, page);
    changesSplitter_->addWidget(topPane);
    changesSplitter_->addWidget(diffView_);
    changesSplitter_->setStretchFactor(0, 3);
    changesSplitter_->setStretchFactor(1, 4);
    changesSplitter_->setChildrenCollapsible(false);

    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->addWidget(changesSplitter_, 1);

    updateCommitButton();
    return page;
}

QWidget* LocalRepositoryWidget::buildHistoryTab()
{
    auto* page = new QWidget(this);

    historyList_ = new QListWidget(page);
    historyList_->setUniformItemSizes(true);
    historyList_->setAlternatingRowColors(true);
    // Allow Ctrl+click to pick a second commit for compare. Single-
    // click clears the previous selection — we don't try to manage
    // two independent "current" markers, just show whichever commit
    // (or pair) is currently selected.
    historyList_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(historyList_, &QListWidget::itemSelectionChanged,
            this, &LocalRepositoryWidget::onHistorySelectionChanged);

    // Context menu for power-user actions on a commit row. Currently
    // just "Copy SHA" — handy when filing bug reports or pasting into
    // issue trackers. Extra actions (cherry-pick, revert, tag here…)
    // could land in this menu as the app grows.
    historyList_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(historyList_, &QListWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        QListWidgetItem* item = historyList_->itemAt(pos);
        if (!item) return;
        // Skip sentinel — Copy SHA on "Load more…" is nonsense.
        if (item->data(kLoadMoreRole).toBool()) return;
        const QString sha = item->data(kCommitRole).toString();
        if (sha.isEmpty()) return;

        QMenu menu(this);
        QAction* copyFull = menu.addAction(
            tr("Copy SHA"));
        QAction* copyShort = menu.addAction(
            tr("Copy short SHA (%1)").arg(sha.left(7)));

        QAction* picked = menu.exec(
            historyList_->viewport()->mapToGlobal(pos));
        if (picked == copyFull) {
            QGuiApplication::clipboard()->setText(sha);
        } else if (picked == copyShort) {
            QGuiApplication::clipboard()->setText(sha.left(7));
        }
    });

    // Scroll-driven verify dispatch. Every scroll pixel fires
    // valueChanged; we route through onHistoryScrolled which kicks
    // a debounce timer. Net effect: one dispatch per ~150ms scroll
    // pause regardless of how much intermediate scrolling happened.
    connect(historyList_->verticalScrollBar(), &QAbstractSlider::valueChanged,
            this, &LocalRepositoryWidget::onHistoryScrolled);

    // Commit filter: a thin search bar above the list. Live-filters by
    // matching against summary + short SHA + author (case-insensitive).
    // Implemented as a plain QLineEdit rather than reusing SearchBar
    // because we don't want highlight/navigation semantics here — just
    // hide rows that don't match.
    commitFilter_ = new QLineEdit(page);
    commitFilter_->setPlaceholderText(
        tr("Filter commits by summary, SHA, or author…"));
    commitFilter_->setClearButtonEnabled(true);
    connect(commitFilter_, &QLineEdit::textChanged,
            this, &LocalRepositoryWidget::onHistoryFilterChanged);

    // Match counter — shows "N of M" while a filter is active so the
    // user knows how many rows survived without scrolling to count.
    // Empty when no filter; muted styling so it doesn't compete with
    // the actual content for attention.
    historyFilterCounter_ = new QLabel(page);
    historyFilterCounter_->setStyleSheet(
        QStringLiteral("color: #9aa0a6; padding: 0 8px;"));
    historyFilterCounter_->setMinimumWidth(90);
    historyFilterCounter_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* filterRow = new QHBoxLayout;
    filterRow->setContentsMargins(0, 0, 0, 0);
    filterRow->setSpacing(4);
    filterRow->addWidget(commitFilter_, 1);
    filterRow->addWidget(historyFilterCounter_);

    auto* listPane = new QWidget(page);
    auto* listCol  = new QVBoxLayout(listPane);
    listCol->setContentsMargins(0, 0, 0, 0);
    listCol->setSpacing(2);
    listCol->addLayout(filterRow);
    listCol->addWidget(historyList_, 1);

    historyDetail_ = new QPlainTextEdit(page);
    historyDetail_->setReadOnly(true);
    historyDetail_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    historyDetail_->setPlaceholderText(tr("Select a commit to see its full message."));
    historyDetail_->setMaximumHeight(180);

    // Files changed by the selected commit. Tight list — paths only,
    // colour-coded prefix to indicate add/modify/delete (same letters
    // as the Changes tab so users learn one vocabulary).
    commitFilesList_ = new QListWidget(page);
    commitFilesList_->setUniformItemSizes(true);
    commitFilesList_->setAlternatingRowColors(true);
    commitFilesList_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    connect(commitFilesList_, &QListWidget::itemSelectionChanged,
            this, &LocalRepositoryWidget::onCommitFileSelectionChanged);

    commitDiffView_ = new DiffViewWidget(page);

    // Three-level vertical layout: commits → message+filelist → diff.
    // The middle row is itself a horizontal splitter so the user can
    // give files more room when the message is short, or the message
    // more room when there are lots of files.
    auto* midRow = new QSplitter(Qt::Horizontal, page);
    midRow->addWidget(historyDetail_);
    midRow->addWidget(commitFilesList_);
    midRow->setStretchFactor(0, 3);
    midRow->setStretchFactor(1, 2);
    midRow->setChildrenCollapsible(false);

    auto* split = new QSplitter(Qt::Vertical, page);
    split->addWidget(listPane);
    split->addWidget(midRow);
    split->addWidget(commitDiffView_);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 5);
    split->setChildrenCollapsible(false);

    historyRefreshBtn_ = new QPushButton(tr("Refresh"), page);
    historyRefreshBtn_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    historyRefreshBtn_->setToolTip(tr("Refresh history (Ctrl+R or F5)"));
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
    currentDiffPath_.clear();

    folderLabel_->setText(QFileInfo(path).fileName());
    pathLabel_->setText(QDir::toNativeSeparators(path));

    branchButton_->setText(QStringLiteral("—"));
    if (changesList_)  changesList_->clear();
    if (historyList_)  historyList_->clear();
    if (historyDetail_) historyDetail_->clear();
    if (remotesList_)  remotesList_->clear();
    if (pushRemoteCombo_) pushRemoteCombo_->clear();
    if (commitMessageEdit_) commitMessageEdit_->clear();
    if (publishBanner_) publishBanner_->setVisible(false);
    if (conflictBanner_) conflictBanner_->setVisible(false);
    if (diffView_) diffView_->clear();
    if (commitFilter_) commitFilter_->clear();
    currentCommitSha_.clear();
    currentCommitFiles_.clear();
    if (commitFilesList_) commitFilesList_->clear();
    if (commitDiffView_)  commitDiffView_->clear();

    pages_->setCurrentWidget(notRepoPage_);
    if (initBranchEdit_) initBranchEdit_->setText(defaultInitBranch_);
    updateIdentityBar();

    // Re-point the watcher at the new folder. It's harmless to do this
    // even when the folder isn't a git repo yet — setupWatcher() guards
    // against missing paths.
    setupWatcher();
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
        branchButton_->setText(QStringLiteral("—"));
        return;
    }

    pages_->setCurrentWidget(repoPage_);
    branchButton_->setText(branch.isEmpty() ? QStringLiteral("(unborn)")
                                            : branch + QStringLiteral("  ▾"));

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

    // .git just got created (init flow) — re-watch. Idempotent for the
    // already-watched case.
    setupWatcher();

    // If a file is currently selected in the changes list, refresh its
    // diff so the panel stays in sync with the new state. Ask the host
    // for it; if the file is no longer in the list, the request will
    // simply be filtered out by the freshness check in setFileDiff.
    requestDiffForSelection();
}

// Append one CommitInfo as a list row. Extracted so setHistory and
// appendHistory share rendering without duplicating the line-format
// or the per-item user data.
void LocalRepositoryWidget::appendCommitRow(const ghm::git::CommitInfo& c)
{
    // Compose the diff-stats suffix when we have it. log() leaves
    // the three fields at -1 when computeStats was off; in that
    // case we just skip the suffix.
    QString statsText;
    if (c.filesChanged >= 0) {
        statsText = QStringLiteral("(+%1 −%2 / %3)")
            .arg(c.insertions)
            .arg(c.deletions)
            .arg(c.filesChanged == 1
                    ? tr("1 file")
                    : tr("%1 files").arg(c.filesChanged));
    }

    // Signature badge. The verify result might not be in the cache
    // yet (lazy verification runs after the row is added), in which
    // case we show a "signed but not verified" prefix for commits
    // with a gpgsig header, and nothing for unsigned commits.
    // Updates flow through onSignatureVerified() below — the row's
    // text gets rewritten when verify completes.
    QString sigBadge;
    if (c.hasSignature) {
        const auto it = sigCache_.constFind(c.id);
        if (it == sigCache_.cend()) {
            sigBadge = QStringLiteral("· ");  // pending
        } else {
            switch (it.value().status) {
                case ghm::git::VerifyResult::Status::Verified:
                    sigBadge = QStringLiteral("✓ "); break;
                case ghm::git::VerifyResult::Status::Signed:
                    sigBadge = QStringLiteral("◐ "); break;
                case ghm::git::VerifyResult::Status::Invalid:
                    sigBadge = QStringLiteral("✗ "); break;
                case ghm::git::VerifyResult::Status::Unsigned:
                    sigBadge = QString();        break;
            }
        }
    }

    QString line;
    if (statsText.isEmpty()) {
        line = QStringLiteral("%1%2  %3  —  %4, %5")
            .arg(sigBadge, c.shortId, c.summary, c.authorName,
                 ghm::core::relativeTime(c.when));
    } else {
        line = QStringLiteral("%1%2  %3  %4  —  %5, %6")
            .arg(sigBadge, c.shortId, c.summary, statsText, c.authorName,
                 ghm::core::relativeTime(c.when));
    }

    auto* item = new QListWidgetItem(line, historyList_);
    item->setData(kCommitRole, QVariant::fromValue(c.id));
    // Tooltip: identity + signature detail when verified.
    QString tooltip = QStringLiteral("%1\n%2 <%3>")
                       .arg(c.id, c.authorName, c.authorEmail);
    if (c.hasSignature) {
        const auto it = sigCache_.constFind(c.id);
        if (it != sigCache_.cend()) {
            const auto& vr = it.value();
            switch (vr.status) {
                case ghm::git::VerifyResult::Status::Verified:
                    tooltip += tr("\n\nSignature verified");
                    if (!vr.signer.isEmpty()) {
                        tooltip += QStringLiteral(": %1").arg(vr.signer);
                    }
                    break;
                case ghm::git::VerifyResult::Status::Signed:
                    tooltip += tr("\n\nSigned, but key trust unknown");
                    break;
                case ghm::git::VerifyResult::Status::Invalid:
                    tooltip += tr("\n\nSignature invalid: %1")
                                .arg(vr.error);
                    break;
                case ghm::git::VerifyResult::Status::Unsigned:
                    break;
            }
        } else {
            tooltip += tr("\n\nSignature pending verification…");
        }
    }
    item->setToolTip(tooltip);
    item->setData(Qt::UserRole + 2, c.message);
    item->setData(Qt::UserRole + 3, c.authorName);
    item->setData(Qt::UserRole + 4, c.authorEmail);
    item->setData(Qt::UserRole + 5, c.when);
    // Stash hasSignature so onSignatureVerified can do quick lookups
    // without scanning the displayed text.
    item->setData(Qt::UserRole + 6, c.hasSignature);
}

// Add a sentinel "Load more older commits…" row at the bottom of the
// list. Marked with kLoadMoreRole = true so the click handler can
// distinguish it from a real commit and fire loadMoreRequested
// instead of opening the diff view.
void LocalRepositoryWidget::appendLoadMoreSentinel()
{
    auto* item = new QListWidgetItem(
        tr("📂 Load more older commits…"), historyList_);
    item->setData(kCommitRole,   QString());  // empty SHA → not a commit
    item->setData(kLoadMoreRole, true);
    // Distinguish visually so it doesn't read like a real commit.
    QFont f = item->font();
    f.setItalic(true);
    item->setFont(f);
    item->setForeground(QBrush(QColor("#8aa3c2")));
    item->setToolTip(tr("Click to fetch the next batch of older commits."));
}

void LocalRepositoryWidget::setHistory(const std::vector<ghm::git::CommitInfo>& commits)
{
    historyLoaded_ = true;
    historyList_->clear();
    historyDetail_->clear();
    currentCommitSha_.clear();
    currentCommitFiles_.clear();
    if (commitFilesList_) commitFilesList_->clear();
    if (commitDiffView_)  commitDiffView_->clear();

    // Reset pagination state — this is the initial fetch, so we know
    // about exactly `commits.size()` rows and the oldest is the
    // basis for the next "load more".
    oldestLoadedSha_.clear();
    isLoadingMore_   = false;

    if (commits.empty()) {
        auto* item = new QListWidgetItem(
            tr("No commits yet — make your first commit in the Changes tab."),
            historyList_);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        return;
    }

    for (const auto& c : commits) appendCommitRow(c);

    oldestLoadedSha_ = commits.back().id;

    // If we got the full page, there might be more — show the
    // sentinel. If we got fewer than the requested cap, we hit the
    // end of history and the sentinel doesn't appear.
    if (static_cast<int>(commits.size()) >= kHistoryPageSize) {
        appendLoadMoreSentinel();
    }

    // Re-apply any active filter — if the user typed "fix" before
    // pressing Refresh, we want the new list to honour that filter
    // immediately rather than dumping every commit on screen.
    if (commitFilter_ && !commitFilter_->text().isEmpty()) {
        onHistoryFilterChanged(commitFilter_->text());
    }

    // Dispatch lazy signature verification — but only for commits
    // currently visible in the viewport. Scrolling fires another
    // dispatch via the verticalScrollBar valueChanged hook. This
    // keeps cold start cheap: a 1000-commit history of signed
    // commits triggers ~15-30 verifies (one viewport-worth) instead
    // of 1000 gpg subprocesses queued in the worker.
    dispatchVerifyForVisibleRows();
}

void LocalRepositoryWidget::appendHistory(
        const std::vector<ghm::git::CommitInfo>& commits)
{
    // Find and remove the existing "Load more…" sentinel (if any) so
    // we can append the new rows above where it used to be. We'll
    // re-add it at the bottom afterwards unless this is the final page.
    for (int i = historyList_->count() - 1; i >= 0; --i) {
        if (historyList_->item(i)->data(kLoadMoreRole).toBool()) {
            delete historyList_->takeItem(i);
            break;
        }
    }

    for (const auto& c : commits) appendCommitRow(c);

    if (!commits.empty()) {
        oldestLoadedSha_ = commits.back().id;
    }

    // Got a full page → more probably available, show sentinel again.
    // Got fewer → this was the last page, stop.
    if (static_cast<int>(commits.size()) >= kHistoryPageSize) {
        appendLoadMoreSentinel();
    }
    isLoadingMore_ = false;

    // Viewport-only verify dispatch — same as setHistory. The newly-
    // appended rows are typically below the viewport (user pressed
    // "Load more" at the bottom) so most won't trigger verify until
    // the user scrolls down. That's intentional: don't burn cycles
    // on rows the user might never look at.
    dispatchVerifyForVisibleRows();
}

void LocalRepositoryWidget::onHistoryScrolled()
{
    // Restart the debounce timer. If another scroll event arrives
    // within 150ms, the timer restarts and the dispatch is deferred
    // again — only when the user pauses do we actually walk the
    // list. Single-shot semantics + start() = stop-and-restart.
    if (verifyDebounce_) verifyDebounce_->start();
}

void LocalRepositoryWidget::dispatchVerifyForVisibleRows()
{
    if (!historyList_ || path_.isEmpty()) return;

    // Viewport rect in the list's coordinate system. Rows whose
    // visualItemRect intersects this are at least partially
    // visible — those are the ones the user can actually see.
    const QRect viewportRect = historyList_->viewport()->rect();

    int dispatched = 0;
    const int rowCount = historyList_->count();
    for (int i = 0; i < rowCount; ++i) {
        QListWidgetItem* item = historyList_->item(i);
        if (!item) continue;
        // Skip the "Load more…" sentinel — it carries no SHA and
        // wouldn't have a signature anyway. Its kLoadMoreRole is
        // the canonical marker.
        if (item->data(kLoadMoreRole).toBool()) continue;
        if (item->isHidden()) continue;  // filtered out

        // Cheap visibility check: visualItemRect returns the row's
        // pixel rect within the viewport. If it doesn't intersect
        // the viewport rect (or has zero size = not laid out yet),
        // the row is offscreen.
        const QRect rowRect = historyList_->visualItemRect(item);
        if (rowRect.isEmpty() ||
            !viewportRect.intersects(rowRect)) {
            continue;
        }

        // hasSignature stashed in UserRole+6 by appendCommitRow.
        // sigCache_ check skips commits we already verified.
        const bool hasSig = item->data(Qt::UserRole + 6).toBool();
        if (!hasSig) continue;
        const QString sha = item->data(kCommitRole).toString();
        if (sha.isEmpty()) continue;
        if (sigCache_.contains(sha)) continue;

        Q_EMIT verifyCommitSignatureRequested(path_, sha);
        ++dispatched;
    }
    // `dispatched` is currently unused but a useful breakpoint /
    // log target if we ever want to instrument hit rates.
    (void)dispatched;
}

void LocalRepositoryWidget::setSignatureVerifyResult(
    const QString& sha, const ghm::git::VerifyResult& result)
{
    sigCache_.insert(sha, result);

    // Find the corresponding row and rewrite it in place. Linear
    // scan of historyList_; for typical history sizes (200-1000
    // rows) this is fast enough. We could index by SHA → row but
    // that'd require maintaining a parallel hash and updating it
    // on every list mutation; not worth it for this hot path.
    for (int i = 0; i < historyList_->count(); ++i) {
        auto* item = historyList_->item(i);
        if (item->data(kCommitRole).toString() != sha) continue;
        // The row's data fields (UserRole + 2,3,4,5,6) are already
        // populated from the original appendCommitRow call; the
        // easiest way to refresh is to rebuild a CommitInfo from
        // them and call appendCommitRow on a fresh row, then take
        // the new row's text/tooltip.
        //
        // Simpler still: just rewrite the row's text and tooltip
        // by reading what we stashed. The format string is shared
        // with appendCommitRow but reconstructing it here drifts
        // out of sync. Instead: build a CommitInfo from the row
        // and call a render helper. To avoid an even bigger
        // refactor, we accept the minor duplication.

        // Pull stored fields.
        ghm::git::CommitInfo c;
        c.id           = sha;
        c.shortId      = sha.left(7);
        c.summary      = item->text();  // contains badge+rest, but ok for now
        c.message      = item->data(Qt::UserRole + 2).toString();
        c.authorName   = item->data(Qt::UserRole + 3).toString();
        c.authorEmail  = item->data(Qt::UserRole + 4).toString();
        c.when         = item->data(Qt::UserRole + 5).toDateTime();
        c.hasSignature = item->data(Qt::UserRole + 6).toBool();

        // The summary in c is currently "badge + shortId + summary + …"
        // — we lost the original summary text. To rebuild cleanly,
        // we just remove the row and re-add via appendCommitRow.
        // But then the row's *position* changes (goes to end). We
        // need to preserve position.
        //
        // Simpler approach: stash the unformatted summary in another
        // role. But that's another retrofit. For now, accept that
        // the row text gets rewritten but the row stays in place by
        // editing the existing item in place: rebuild the badge
        // prefix and prepend it after stripping any previous one.
        QString text = item->text();
        // Strip leading badge if any. Possible prefixes: "✓ ", "◐ ",
        // "✗ ", "· ". Each is 2 chars (incl. trailing space).
        static const QStringList prefixes = {
            QStringLiteral("✓ "), QStringLiteral("◐ "),
            QStringLiteral("✗ "), QStringLiteral("· "),
        };
        for (const QString& p : prefixes) {
            if (text.startsWith(p)) {
                text.remove(0, p.length());
                break;
            }
        }
        QString badge;
        switch (result.status) {
            case ghm::git::VerifyResult::Status::Verified: badge = QStringLiteral("✓ "); break;
            case ghm::git::VerifyResult::Status::Signed:   badge = QStringLiteral("◐ "); break;
            case ghm::git::VerifyResult::Status::Invalid:  badge = QStringLiteral("✗ "); break;
            case ghm::git::VerifyResult::Status::Unsigned: badge = QString(); break;
        }
        item->setText(badge + text);

        // Tooltip: replace pending/old verdict block. Tooltip body
        // is "<sha>\n<name> <email>\n\n<verdict line>". Strip
        // anything after the second \n\n marker, then re-append.
        QString tooltip = item->toolTip();
        const int doubleNl = tooltip.indexOf(QStringLiteral("\n\n"));
        if (doubleNl >= 0) tooltip.truncate(doubleNl);

        switch (result.status) {
            case ghm::git::VerifyResult::Status::Verified:
                tooltip += tr("\n\nSignature verified");
                if (!result.signer.isEmpty()) {
                    tooltip += QStringLiteral(": %1").arg(result.signer);
                }
                break;
            case ghm::git::VerifyResult::Status::Signed:
                tooltip += tr("\n\nSigned, but key trust unknown");
                break;
            case ghm::git::VerifyResult::Status::Invalid:
                tooltip += tr("\n\nSignature invalid: %1").arg(result.error);
                break;
            case ghm::git::VerifyResult::Status::Unsigned:
                break;
        }
        item->setToolTip(tooltip);
        break;  // found the row, done
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

void LocalRepositoryWidget::setConflictCount(int count)
{
    if (!conflictBanner_ || !conflictBannerLabel_) return;
    if (count <= 0) {
        conflictBanner_->setVisible(false);
        return;
    }
    conflictBannerLabel_->setText(
        tr("<b>Merge in progress — %1 conflicted file%2.</b><br>"
           "Resolve them in your editor, then click 'Resolve conflicts…' "
           "to mark each file as done.")
            .arg(count)
            .arg(count == 1 ? QString() : QStringLiteral("s")));
    conflictBanner_->setVisible(true);
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

void LocalRepositoryWidget::onHistoryFilterChanged(const QString& filter)
{
    // Match against the row's display text (which already contains
    // shortId, summary, and authorName) so we don't have to plumb
    // extra fields through. Empty filter shows everything.
    //
    // The "Load more…" sentinel is always visible (regardless of
    // filter) — hiding it would strand the user with no way to fetch
    // more rows that might actually match their filter.
    const QString f = filter.trimmed();
    int total   = 0;  // commit rows (excluding sentinel)
    int matched = 0;  // commits surviving the filter
    for (int i = 0; i < historyList_->count(); ++i) {
        QListWidgetItem* item = historyList_->item(i);
        if (item->data(kLoadMoreRole).toBool()) {
            item->setHidden(false);
            continue;
        }
        ++total;
        const bool visible = f.isEmpty() ||
            item->text().contains(f, Qt::CaseInsensitive) ||
            item->data(kCommitRole).toString().contains(f, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible) ++matched;
    }
    // If the currently-selected item got hidden, the lower panes still
    // show that commit's diff/message — leave it alone. The user can
    // see the filter is active and clear it; we don't second-guess.

    // Update the "N of M" counter. Three states:
    //   * filter empty           → blank (no clutter)
    //   * matches == 0           → "no matches" (red-tinted? just text for now)
    //   * matches > 0            → "N of M"
    if (!historyFilterCounter_) return;
    if (f.isEmpty()) {
        historyFilterCounter_->clear();
    } else if (matched == 0) {
        historyFilterCounter_->setText(tr("no matches"));
    } else if (matched == total) {
        // All rows match — show the total alone, "N of N" is noise.
        historyFilterCounter_->setText(tr("%1 matches").arg(matched));
    } else {
        historyFilterCounter_->setText(tr("%1 of %2").arg(matched).arg(total));
    }

    // Filter changes alter which rows are visible. Re-dispatch
    // verify so newly-visible signed commits get badge'd. Already-
    // verified ones short-circuit via the sigCache_.contains()
    // check in the helper, so this is cheap when the user is
    // typing a filter.
    if (verifyDebounce_) verifyDebounce_->start();
}

void LocalRepositoryWidget::onHistorySelectionChanged()
{
    auto items = historyList_->selectedItems();
    if (items.isEmpty()) {
        historyDetail_->clear();
        currentCommitSha_.clear();
        currentCommitFiles_.clear();
        if (commitFilesList_) commitFilesList_->clear();
        if (commitDiffView_)  commitDiffView_->clear();
        return;
    }

    // Sentinel "Load more older commits…" row was selected (single).
    // Fire the request and DESELECT so a second click on the same
    // sentinel after the new batch arrives triggers another fetch.
    if (items.size() == 1 && items.front()->data(kLoadMoreRole).toBool()) {
        if (isLoadingMore_ || oldestLoadedSha_.isEmpty() || path_.isEmpty()) {
            historyList_->clearSelection();
            return;
        }
        isLoadingMore_ = true;
        // Re-label so the user gets feedback. We'll remove the row
        // entirely when the new batch arrives.
        items.front()->setText(tr("⋯ Loading older commits…"));
        Q_EMIT loadMoreHistoryRequested(path_, oldestLoadedSha_);
        historyList_->clearSelection();
        return;
    }

    // Two selected commits → compare them. The user got here by Ctrl+
    // clicking a second commit; we treat the older one (lower in the
    // list, i.e. higher row index, since git log is reverse-chrono)
    // as the base, the newer as the target — what `git diff old new`
    // would show.
    if (items.size() == 2) {
        // historyList_->row() gives the visual index; older commits
        // have higher row numbers in our list.
        QListWidgetItem* a = items[0];
        QListWidgetItem* b = items[1];
        if (historyList_->row(a) < historyList_->row(b)) std::swap(a, b);
        const QString shaOld = a->data(kCommitRole).toString();
        const QString shaNew = b->data(kCommitRole).toString();
        if (shaOld.isEmpty() || shaNew.isEmpty()) return;

        const QString shortOld = a->data(kCommitRole).toString().left(7);
        const QString shortNew = b->data(kCommitRole).toString().left(7);
        historyDetail_->setPlainText(
            tr("Comparing %1..%2\n\n"
               "Older base on top, newer target below.\n"
               "Files changed are listed on the right; pick one to see its diff.")
                .arg(shortOld, shortNew));

        // Synthetic SHA matches what the worker will emit so setCommitDiff
        // accepts the result instead of filtering it as stale.
        const QString synthSha = shaOld + QStringLiteral("..") + shaNew;
        if (synthSha != currentCommitSha_) {
            currentCommitSha_ = synthSha;
            currentCommitFiles_.clear();
            if (commitFilesList_) commitFilesList_->clear();
            if (commitDiffView_)  commitDiffView_->setLoading(synthSha);
            if (!path_.isEmpty()) {
                Q_EMIT commitCompareRequested(path_, shaOld, shaNew);
            }
        }
        return;
    }

    // More than two — that's selection rubber-banding or Shift+click;
    // we don't have a meaningful UI for "diff three commits". Treat
    // it as if the most recently picked commit is selected alone.
    auto* it = items.first();
    if (items.size() > 2) {
        // currentItem() tracks the focus marker, which is what the
        // user just clicked — better default than items.first().
        if (auto* c = historyList_->currentItem()) it = c;
    }
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

    // Kick off the diff request for this commit. The result lands in
    // setCommitDiff(), which populates the file list and the diff view.
    if (id != currentCommitSha_) {
        currentCommitSha_ = id;
        currentCommitFiles_.clear();
        if (commitFilesList_) commitFilesList_->clear();
        if (commitDiffView_)  commitDiffView_->setLoading(id);
        if (!path_.isEmpty()) Q_EMIT commitDiffRequested(path_, id);
    }
}

void LocalRepositoryWidget::onCommitFileSelectionChanged()
{
    if (!commitDiffView_ || !commitFilesList_) return;
    const auto sel = commitFilesList_->selectedItems();
    if (sel.isEmpty()) {
        commitDiffView_->clear();
        return;
    }
    // Each item carries the index into currentCommitFiles_ — fast lookup
    // and avoids a name search.
    const int idx = sel.first()->data(Qt::UserRole + 1).toInt();
    if (idx < 0 || idx >= static_cast<int>(currentCommitFiles_.size())) {
        commitDiffView_->clear();
        return;
    }
    commitDiffView_->setDiff(currentCommitFiles_[idx]);
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

void LocalRepositoryWidget::onChangesSelectionChanged()
{
    requestDiffForSelection();
}

void LocalRepositoryWidget::onWatchedPathChanged()
{
    // Re-arm the debouncer. Multiple rapid hits (e.g. saving 50 files
    // from an editor) collapse into a single refresh after the timer
    // settles — see autoRefreshTimer_'s 300ms interval.
    autoRefreshTimer_->start();
}

void LocalRepositoryWidget::onAutoRefreshTimeout()
{
    if (path_.isEmpty()) return;
    // The watcher unwatches deleted files automatically (Linux quirk
    // when an editor saves with rename-replace). Re-add them so we
    // keep getting events.
    setupWatcher();
    Q_EMIT refreshRequested(path_);
}

// ----- Branch picker -------------------------------------------------------

void LocalRepositoryWidget::setBranches(
    const std::vector<ghm::git::BranchInfo>& branches)
{
    branchInfos_ = branches;

    // Refresh the button caption: branch name + ahead/behind arrows
    // for the current branch (if known). Format mirrors how shells like
    // starship/p10k display this info.
    QString caption = branch_.isEmpty() ? QStringLiteral("(unborn)") : branch_;
    if (branch_.startsWith(QLatin1Char('('))) {
        // unborn / detached — no branch info to enrich with
        branchButton_->setText(caption);
        return;
    }
    for (const auto& b : branches) {
        if (b.isCurrent) {
            QStringList parts;
            parts << caption;
            if (b.hasUpstream) {
                if (b.ahead  > 0) parts << QStringLiteral("↑%1").arg(b.ahead);
                if (b.behind > 0) parts << QStringLiteral("↓%1").arg(b.behind);
            } else {
                parts << QStringLiteral("(no upstream)");
            }
            caption = parts.join(QStringLiteral(" "));
            break;
        }
    }
    branchButton_->setText(caption + QStringLiteral("  ▾"));
}

void LocalRepositoryWidget::onBranchButtonClicked()
{
    // Build a fresh popup every time so external branch changes
    // (e.g. user ran `git branch foo` from a terminal) show up
    // without requiring a separate Refresh action.
    if (path_.isEmpty() || !isRepository_) return;

    QMenu menu(this);
    menu.setSeparatorsCollapsible(false);

    if (branchInfos_.empty()) {
        auto* none = menu.addAction(tr("(no branches yet — make a commit first)"));
        none->setEnabled(false);
    } else {
        // Branches are pre-sorted (current first, then alphabetical).
        for (const auto& b : branchInfos_) {
            QString label = b.name;
            if (b.hasUpstream) {
                QStringList annotations;
                if (b.ahead  > 0) annotations << QStringLiteral("↑%1").arg(b.ahead);
                if (b.behind > 0) annotations << QStringLiteral("↓%1").arg(b.behind);
                if (!annotations.isEmpty()) {
                    label += QStringLiteral("    ") + annotations.join(QLatin1Char(' '));
                }
            }
            QAction* act = menu.addAction(label);
            act->setCheckable(true);
            act->setChecked(b.isCurrent);
            act->setData(b.name);
            // The current branch can't be checked-out into itself, but
            // we still show it in the menu for orientation.
            if (b.isCurrent) {
                act->setEnabled(false);
                QFont f = act->font();
                f.setBold(true);
                act->setFont(f);
            }
        }
    }

    menu.addSeparator();
    QAction* createAct = menu.addAction(tr("Create new branch…"));
    QAction* renameAct = menu.addAction(tr("Rename current branch…"));
    QAction* deleteAct = menu.addAction(tr("Delete branch…"));

    // Disabling delete when there's nothing to delete (only the current
    // branch exists, or the repo is unborn) keeps the menu honest.
    bool hasDeletable = false;
    QString currentName;
    for (const auto& b : branchInfos_) {
        if (b.isCurrent) currentName = b.name;
        else             hasDeletable = true;
    }
    deleteAct->setEnabled(hasDeletable);
    // Renaming an unborn HEAD doesn't make sense — there's no branch yet.
    renameAct->setEnabled(!currentName.isEmpty());

    QAction* picked = menu.exec(branchButton_->mapToGlobal(
        QPoint(0, branchButton_->height())));
    if (!picked) return;

    if (picked == createAct) {
        Q_EMIT branchCreateRequested(path_);
        return;
    }
    if (picked == renameAct) {
        if (!currentName.isEmpty()) {
            Q_EMIT branchRenameRequested(path_, currentName);
        }
        return;
    }
    if (picked == deleteAct) {
        // Build a quick chooser over the deletable branches. If only
        // one is deletable, skip straight to it.
        QStringList options;
        for (const auto& b : branchInfos_) {
            if (!b.isCurrent) options << b.name;
        }
        if (options.isEmpty()) return;

        QString chosen;
        if (options.size() == 1) {
            chosen = options.first();
        } else {
            // Sub-menu UX: show a second QMenu of deletable branches.
            QMenu sub(this);
            for (const auto& n : options) sub.addAction(n);
            QAction* p2 = sub.exec(QCursor::pos());
            if (!p2) return;
            chosen = p2->text();
        }
        Q_EMIT branchDeleteRequested(path_, chosen);
        return;
    }

    // Otherwise it's a branch-switch action.
    const QString target = picked->data().toString();
    if (!target.isEmpty()) {
        Q_EMIT branchSwitchRequested(path_, target);
    }
}

// ----- Diff plumbing -------------------------------------------------------

void LocalRepositoryWidget::setFileDiff(const QString& repoRelPath,
                                        const ghm::git::FileDiff& diff,
                                        const QString& error)
{
    if (!diffView_) return;
    // Filter stale results: if the user already moved on to another
    // file, the previous request's reply would otherwise overwrite the
    // newer diff with old content.
    if (repoRelPath != currentDiffPath_) return;

    if (!error.isEmpty()) {
        diffView_->setDiff({}, error);
        return;
    }
    diffView_->setDiff(diff);
}

void LocalRepositoryWidget::setCommitDiff(
    const QString&                          sha,
    const std::vector<ghm::git::FileDiff>&  files,
    const QString&                          error)
{
    // Filter stale: user moved on to a different commit before this
    // one came back. Drop the result silently.
    if (sha != currentCommitSha_) return;
    if (!commitFilesList_ || !commitDiffView_) return;

    commitFilesList_->clear();
    currentCommitFiles_ = files;

    if (!error.isEmpty()) {
        commitDiffView_->setDiff({}, error);
        return;
    }
    if (files.empty()) {
        // Empty commit (rare but legal — `git commit --allow-empty`)
        // or a merge whose first parent is identical.
        commitDiffView_->setDiff({});  // shows the "no changes" placeholder
        return;
    }

    // Build the file list — same prefix vocabulary as the Changes tab
    // ([A] added, [M] modified, [D] deleted, [R] renamed, ...) for
    // visual consistency between the two contexts.
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& fd = files[i];
        QString line;
        line.reserve(fd.path.size() + 16);
        line += QLatin1Char('[');
        line += fd.status;
        line += QStringLiteral("]  ");
        line += fd.path;
        if (!fd.oldPath.isEmpty() && fd.oldPath != fd.path) {
            line += QStringLiteral("  ← ");
            line += fd.oldPath;
        }
        QStringList summary;
        if (fd.additions > 0) summary << QStringLiteral("+%1").arg(fd.additions);
        if (fd.deletions > 0) summary << QStringLiteral("-%1").arg(fd.deletions);
        if (!summary.isEmpty()) {
            line += QStringLiteral("    ") + summary.join(QLatin1Char(' '));
        }

        auto* item = new QListWidgetItem(line, commitFilesList_);
        item->setData(Qt::UserRole + 1, static_cast<int>(i));
    }

    // Auto-select the first file so the diff pane shows something
    // immediately rather than the placeholder. Saves a click.
    if (commitFilesList_->count() > 0) {
        commitFilesList_->setCurrentRow(0);
    }
}

void LocalRepositoryWidget::requestDiffForSelection()
{
    if (!diffView_ || !changesList_) return;
    if (path_.isEmpty() || !isRepository_) {
        diffView_->clear();
        currentDiffPath_.clear();
        return;
    }
    // Only render the diff for a single-selection. Multi-select is for
    // bulk stage/unstage operations; showing the diff of "one of them"
    // would be misleading. We show the placeholder instead.
    const auto sel = changesList_->selectedItems();
    if (sel.size() != 1) {
        diffView_->clear();
        currentDiffPath_.clear();
        return;
    }

    const QString repoRelPath = sel.first()->data(Qt::UserRole + 1).toString();
    if (repoRelPath.isEmpty()) {
        diffView_->clear();
        currentDiffPath_.clear();
        return;
    }

    currentDiffPath_ = repoRelPath;
    diffView_->setLoading(repoRelPath);
    // HeadToWorkdir gives users the most natural "what will my next
    // commit look like, plus what's still loose in the worktree" view.
    Q_EMIT diffRequested(path_, repoRelPath, ghm::git::DiffScope::HeadToWorkdir);
}

// ----- Auto-refresh watcher ------------------------------------------------

void LocalRepositoryWidget::setupWatcher()
{
    teardownWatcher();
    if (path_.isEmpty()) return;

    QStringList toWatch;
    // Working-tree root — catches new files, dir-level renames, etc.
    if (QDir(path_).exists()) toWatch << path_;

    // .git internals: HEAD changes on branch switch / commit; index
    // changes on stage/unstage. Tags and refs aren't crucial for the
    // Changes view — skipping them keeps the watch list short on
    // platforms where it has limits (Linux inotify default is 8192).
    const QString gitDir = QDir(path_).filePath(QStringLiteral(".git"));
    if (QFileInfo(gitDir).isDir()) {
        const QString headPath  = QDir(gitDir).filePath(QStringLiteral("HEAD"));
        const QString indexPath = QDir(gitDir).filePath(QStringLiteral("index"));
        if (QFileInfo::exists(headPath))  toWatch << headPath;
        if (QFileInfo::exists(indexPath)) toWatch << indexPath;
    }
    // QFileSystemWatcher::addPaths returns the list it failed to add;
    // ignore — non-existent paths just aren't watched.
    if (!toWatch.isEmpty()) watcher_->addPaths(toWatch);
}

void LocalRepositoryWidget::teardownWatcher()
{
    const auto files = watcher_->files();
    if (!files.isEmpty()) watcher_->removePaths(files);
    const auto dirs  = watcher_->directories();
    if (!dirs.isEmpty())  watcher_->removePaths(dirs);
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
    // Remember which file was selected so we can re-apply the selection
    // after the rebuild — otherwise an auto-refresh would scrub the
    // diff panel back to the placeholder every time.
    QString prevSelected;
    if (auto sel = changesList_->selectedItems(); sel.size() == 1) {
        prevSelected = sel.first()->data(kPathRole).toString();
    }

    QSignalBlocker blocker(changesList_);
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

    QListWidgetItem* toReselect = nullptr;
    auto addAll = [&](const auto& vec) {
        for (const auto* e : vec) {
            auto* item = new QListWidgetItem(formatStatusItem(*e), changesList_);
            item->setData(kPathRole, e->path);
            item->setToolTip(humanStatusTooltip(*e));
            if (!prevSelected.isEmpty() && e->path == prevSelected) {
                toReselect = item;
            }
        }
    };
    addAll(conflicted);
    addAll(staged);
    addAll(unstaged);
    addAll(untracked);

    if (toReselect) {
        changesList_->setCurrentItem(toReselect);
        toReselect->setSelected(true);
    }
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

// ----- Submodules tab ------------------------------------------------------

QWidget* LocalRepositoryWidget::buildSubmodulesTab()
{
    auto* page = new QWidget(this);
    auto* col  = new QVBoxLayout(page);

    auto* header = new QHBoxLayout;
    auto* heading = new QLabel(tr("Submodules"), page);
    heading->setStyleSheet(QStringLiteral("font-weight: bold;"));
    submoduleAddBtn_       = new QPushButton(tr("Add submodule…"), page);
    submoduleUpdateAllBtn_ = new QPushButton(tr("Update all"),     page);
    submoduleRefreshBtn_   = new QPushButton(tr("Refresh"),        page);
    submoduleAddBtn_->setToolTip(tr(
        "Add a new submodule by URL. Opens a dialog for the URL and "
        "the path within this repo."));
    submoduleUpdateAllBtn_->setToolTip(tr(
        "Run init+update on every submodule listed in .gitmodules. "
        "Network operations use ssh-agent for SSH URLs."));

    header->addWidget(heading);
    header->addStretch();
    header->addWidget(submoduleAddBtn_);
    header->addWidget(submoduleRefreshBtn_);
    header->addWidget(submoduleUpdateAllBtn_);

    submoduleTable_ = new QTableWidget(page);
    submoduleTable_->setColumnCount(5);
    submoduleTable_->setHorizontalHeaderLabels({
        tr("Path"), tr("URL"), tr("Status"),
        tr("Recorded"), tr("Current")
    });
    submoduleTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Interactive);
    submoduleTable_->horizontalHeader()->setStretchLastSection(false);
    // Path and URL get most of the width; status/SHA columns stay narrow.
    submoduleTable_->setColumnWidth(0, 180);
    submoduleTable_->setColumnWidth(1, 300);
    submoduleTable_->setColumnWidth(2, 110);
    submoduleTable_->setColumnWidth(3, 80);
    submoduleTable_->setColumnWidth(4, 80);
    submoduleTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    submoduleTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    submoduleTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    submoduleTable_->verticalHeader()->setVisible(false);

    // Context menu for power-user "init/update with explicit SSH key"
    // flows. The plain Init/Update buttons below use ssh-agent for SSH
    // URLs — most users want that. But submodules with deploy keys
    // need a specific key file, and that path goes through this menu.
    submoduleTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(submoduleTable_, &QTableWidget::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        const QString name = selectedSubmoduleName();
        if (name.isEmpty()) return;

        // Check if we have a remembered key for this submodule.
        // If yes, the menu offers reuse + forget actions;
        // if no, the menu offers initial-pick actions.
        const QString rememberedKey = settings_
            ? settings_->rememberedSubmoduleKey(path_, name)
            : QString();
        const bool hasRemembered = !rememberedKey.isEmpty();

        QMenu menu(this);
        QAction* initStd     = menu.addAction(tr("Init && Update"));
        QAction* updateStd   = menu.addAction(tr("Update"));
        menu.addSeparator();

        QAction* initExplicit   = nullptr;
        QAction* updateExplicit = nullptr;
        QAction* forgetKey      = nullptr;

        if (hasRemembered) {
            // Show the remembered key path in the action label so
            // the user knows what's about to be used. Ellipsize
            // long paths in display by trimming to last 2 components.
            QFileInfo fi(rememberedKey);
            const QString shortKey = fi.fileName();
            initExplicit = menu.addAction(
                tr("Init && Update with remembered key (%1)")
                    .arg(shortKey));
            updateExplicit = menu.addAction(
                tr("Update with remembered key (%1)").arg(shortKey));
            initExplicit->setToolTip(tr(
                "Use the previously-saved key %1 without prompting. "
                "Use 'Forget remembered key' below to clear the saved "
                "mapping and pick a new one.").arg(rememberedKey));
            updateExplicit->setToolTip(initExplicit->toolTip());

            menu.addSeparator();
            forgetKey = menu.addAction(tr("Forget remembered key"));
            forgetKey->setToolTip(tr(
                "Clear the saved (submodule → key file) mapping. "
                "Next 'explicit key' init/update will prompt for a "
                "key file again."));
        } else {
            initExplicit = menu.addAction(
                tr("Init && Update with explicit SSH key…"));
            updateExplicit = menu.addAction(
                tr("Update with explicit SSH key…"));
            initExplicit->setToolTip(tr(
                "Use a specific SSH private key file instead of "
                "ssh-agent. Needed for submodules that use deploy "
                "keys scoped to that one repository. The choice is "
                "remembered for future operations on this submodule."));
            updateExplicit->setToolTip(initExplicit->toolTip());
        }

        QAction* picked = menu.exec(
            submoduleTable_->viewport()->mapToGlobal(pos));
        if (picked == initStd)
            Q_EMIT submoduleInitRequested(path_, name);
        else if (picked == updateStd)
            Q_EMIT submoduleUpdateRequested(path_, name);
        else if (picked == initExplicit)
            Q_EMIT submoduleInitWithExplicitKeyRequested(path_, name);
        else if (picked == updateExplicit)
            Q_EMIT submoduleUpdateWithExplicitKeyRequested(path_, name);
        else if (picked == forgetKey) {
            Q_EMIT submoduleForgetRememberedKeyRequested(path_, name);
        }
    });

    // Per-row action buttons sit below the table. They operate on the
    // currently-selected submodule; the user picks the row, then
    // clicks the appropriate action. We could put buttons in each
    // row but that wastes horizontal space and clutters the table.
    auto* rowActions = new QHBoxLayout;
    submoduleInitBtn_   = new QPushButton(tr("Init && Update"), page);
    submoduleUpdateBtn_ = new QPushButton(tr("Update"),         page);
    submoduleSyncBtn_   = new QPushButton(tr("Sync URL"),       page);
    submoduleRemoveBtn_ = new QPushButton(tr("Remove…"),        page);
    submoduleInitBtn_->setToolTip(tr(
        "Clone the selected submodule (if not yet initialized) and "
        "check out the SHA recorded in the parent commit."));
    submoduleUpdateBtn_->setToolTip(tr(
        "Fetch and check out the SHA recorded in the parent commit. "
        "Use this when the parent has bumped the submodule to a new "
        "version."));
    submoduleSyncBtn_->setToolTip(tr(
        "Copy the URL from .gitmodules into .git/config. Run this "
        "when the upstream URL changes (Status column shows 'URL mismatch')."));
    submoduleRemoveBtn_->setToolTip(tr(
        "Remove this submodule. This deletes the workdir copy and "
        "stages the .gitmodules + gitlink removal — you still need "
        "to commit afterwards. A confirmation prompt appears first."));
    // Style Remove differently — destructive action gets a muted red
    // tint so it doesn't blend with the harmless ones.
    submoduleRemoveBtn_->setStyleSheet(QStringLiteral(
        "QPushButton:enabled { color: #e07a5f; }"));
    // Disabled until a row is selected.
    submoduleInitBtn_->setEnabled(false);
    submoduleUpdateBtn_->setEnabled(false);
    submoduleSyncBtn_->setEnabled(false);
    submoduleRemoveBtn_->setEnabled(false);

    rowActions->addStretch();
    rowActions->addWidget(submoduleInitBtn_);
    rowActions->addWidget(submoduleUpdateBtn_);
    rowActions->addWidget(submoduleSyncBtn_);
    rowActions->addWidget(submoduleRemoveBtn_);

    submoduleEmptyLabel_ = new QLabel(
        tr("No submodules in this repository."), page);
    submoduleEmptyLabel_->setAlignment(Qt::AlignCenter);
    submoduleEmptyLabel_->setStyleSheet(QStringLiteral("color: #8aa3c2; padding: 24px;"));
    submoduleEmptyLabel_->setVisible(false);

    col->addLayout(header);
    col->addWidget(submoduleTable_, 1);
    col->addWidget(submoduleEmptyLabel_);
    col->addLayout(rowActions);

    connect(submoduleAddBtn_, &QPushButton::clicked, this,
            [this] { Q_EMIT submoduleAddRequested(path_); });
    connect(submoduleRefreshBtn_, &QPushButton::clicked, this,
            [this] { Q_EMIT submodulesListRequested(path_); });
    connect(submoduleUpdateAllBtn_, &QPushButton::clicked, this,
            &LocalRepositoryWidget::onSubmoduleUpdateAllClicked);
    connect(submoduleInitBtn_, &QPushButton::clicked, this,
            &LocalRepositoryWidget::onSubmoduleInitClicked);
    connect(submoduleUpdateBtn_, &QPushButton::clicked, this,
            &LocalRepositoryWidget::onSubmoduleUpdateClicked);
    connect(submoduleSyncBtn_, &QPushButton::clicked, this,
            &LocalRepositoryWidget::onSubmoduleSyncClicked);
    connect(submoduleRemoveBtn_, &QPushButton::clicked, this,
            &LocalRepositoryWidget::onSubmoduleRemoveClicked);
    connect(submoduleTable_, &QTableWidget::itemSelectionChanged, this,
            &LocalRepositoryWidget::onSubmoduleSelectionChanged);

    return page;
}

void LocalRepositoryWidget::setSubmodules(
    const std::vector<ghm::git::SubmoduleInfo>& subs)
{
    submodules_ = subs;

    submoduleTable_->setRowCount(0);
    if (subs.empty()) {
        submoduleTable_->setVisible(false);
        submoduleEmptyLabel_->setVisible(true);
        submoduleUpdateAllBtn_->setEnabled(false);
        submoduleInitBtn_->setEnabled(false);
        submoduleUpdateBtn_->setEnabled(false);
        submoduleSyncBtn_->setEnabled(false);
        submoduleRemoveBtn_->setEnabled(false);
        // The Add button stays enabled even when there are no
        // submodules yet — that's actually the most common state
        // from which a user clicks "Add submodule…" for the first time.
        return;
    }
    submoduleTable_->setVisible(true);
    submoduleEmptyLabel_->setVisible(false);
    submoduleUpdateAllBtn_->setEnabled(true);

    submoduleTable_->setRowCount(static_cast<int>(subs.size()));
    for (size_t i = 0; i < subs.size(); ++i) {
        const auto& s = subs[i];
        const int row = static_cast<int>(i);

        // Store the submodule name (the .gitmodules key) on the Path
        // cell as user-data so the click handlers can dispatch by name
        // without re-looking-up by row index.
        auto* pathItem = new QTableWidgetItem(s.path);
        pathItem->setData(Qt::UserRole, s.name);
        submoduleTable_->setItem(row, 0, pathItem);

        submoduleTable_->setItem(row, 1, new QTableWidgetItem(s.url));

        QString statusText;
        QColor  statusColor;
        switch (s.status) {
            case ghm::git::SubmoduleInfo::Status::NotInitialized:
                statusText = tr("Not initialized");  statusColor = QColor("#8aa3c2"); break;
            case ghm::git::SubmoduleInfo::Status::UpToDate:
                statusText = tr("Up to date");       statusColor = QColor("#7ed99d"); break;
            case ghm::git::SubmoduleInfo::Status::Modified:
                statusText = tr("Modified");        statusColor = QColor("#e0b85a"); break;
            case ghm::git::SubmoduleInfo::Status::UrlMismatch:
                statusText = tr("URL mismatch");    statusColor = QColor("#e07a5f"); break;
            case ghm::git::SubmoduleInfo::Status::Missing:
                statusText = tr("Missing");         statusColor = QColor("#e07a5f"); break;
            case ghm::git::SubmoduleInfo::Status::Unknown:
                statusText = tr("Unknown");         statusColor = QColor("#8aa3c2"); break;
        }
        auto* statusItem = new QTableWidgetItem(statusText);
        statusItem->setForeground(QBrush(statusColor));
        submoduleTable_->setItem(row, 2, statusItem);

        // Show short (7-char) SHAs. Empty cells when SHA isn't known.
        const QString recordedShort = s.recordedSha.left(7);
        const QString workdirShort  = s.workdirSha.left(7);
        submoduleTable_->setItem(row, 3, new QTableWidgetItem(recordedShort));
        submoduleTable_->setItem(row, 4, new QTableWidgetItem(workdirShort));
    }
}

void LocalRepositoryWidget::onSubmoduleSelectionChanged()
{
    const QString name = selectedSubmoduleName();
    const bool hasSelection = !name.isEmpty();
    submoduleInitBtn_->setEnabled(hasSelection);
    submoduleUpdateBtn_->setEnabled(hasSelection);
    submoduleSyncBtn_->setEnabled(hasSelection);
    submoduleRemoveBtn_->setEnabled(hasSelection);
}

QString LocalRepositoryWidget::selectedSubmoduleName() const
{
    const auto rows = submoduleTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return {};
    auto* item = submoduleTable_->item(rows.first().row(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void LocalRepositoryWidget::onSubmoduleInitClicked()
{
    const QString name = selectedSubmoduleName();
    if (name.isEmpty() || path_.isEmpty()) return;
    Q_EMIT submoduleInitRequested(path_, name);
}

void LocalRepositoryWidget::onSubmoduleUpdateClicked()
{
    const QString name = selectedSubmoduleName();
    if (name.isEmpty() || path_.isEmpty()) return;
    Q_EMIT submoduleUpdateRequested(path_, name);
}

void LocalRepositoryWidget::onSubmoduleSyncClicked()
{
    const QString name = selectedSubmoduleName();
    if (name.isEmpty() || path_.isEmpty()) return;
    Q_EMIT submoduleSyncRequested(path_, name);
}

void LocalRepositoryWidget::onSubmoduleRemoveClicked()
{
    const QString name = selectedSubmoduleName();
    if (name.isEmpty() || path_.isEmpty()) return;
    // We don't pop the confirmation dialog from inside this widget —
    // the host (MainWindow) does, because MainWindow has the parent
    // window for proper modality and styling. The widget just
    // signals intent.
    Q_EMIT submoduleRemoveRequested(path_, name);
}

void LocalRepositoryWidget::onSubmoduleUpdateAllClicked()
{
    if (path_.isEmpty() || submodules_.empty()) return;
    // Emit one signal per submodule. MainWindow serialises them on
    // the worker (the worker has a queue), and the user sees each
    // op's progress as it completes. We could batch into a single
    // worker task but per-submodule reporting is friendlier UI.
    for (const auto& s : submodules_) {
        Q_EMIT submoduleInitRequested(path_, s.name);
    }
}

} // namespace ghm::ui
