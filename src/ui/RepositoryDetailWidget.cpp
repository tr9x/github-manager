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
#include <QTabWidget>
#include <QTextBrowser>
#include <QListWidget>
#include <QStyle>
#include <QApplication>

namespace ghm::ui {

namespace {

// Stable color palette for the languages bar. Cycles for languages
// beyond the table. Matches GitHub's broad-strokes palette: not
// pixel-exact but recognisable (greens, blues, yellows, reds…).
const QStringList& kLangColors()
{
    static const QStringList c = {
        QStringLiteral("#3572A5"),  // Python blue
        QStringLiteral("#f1e05a"),  // JavaScript yellow
        QStringLiteral("#dea584"),  // Rust orange
        QStringLiteral("#00ADD8"),  // Go teal
        QStringLiteral("#b07219"),  // Java brown
        QStringLiteral("#178600"),  // C# green
        QStringLiteral("#f34b7d"),  // C++ pink
        QStringLiteral("#555555"),  // C grey
        QStringLiteral("#701516"),  // Ruby red
        QStringLiteral("#41b883"),  // Vue green
        QStringLiteral("#A97BFF"),  // Kotlin purple
        QStringLiteral("#ffac45"),  // Swift orange
        QStringLiteral("#2b7489"),  // TypeScript blue
    };
    return c;
}

// Format a byte count human-readably. Used for sizes in the files
// list and the languages bar tooltips. Defensive against negative
// inputs since GitHub occasionally surprises.
QString humanBytes(qint64 b)
{
    if (b < 0) return QStringLiteral("?");
    if (b < 1024)            return QStringLiteral("%1 B").arg(b);
    if (b < 1024 * 1024)     return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    if (b < 1024LL * 1024 * 1024)
                              return QStringLiteral("%1 MB").arg(b / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(b / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

} // anonymous namespace

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
    , openBrowserBtn_(new QPushButton(tr("Open on GitHub"), this))
    , fsModel_(new QFileSystemModel(this))
    , fileTree_(new QTreeView(this))
    , detailTabs_(new QTabWidget(this))
    , readmeView_(new QTextBrowser(this))
    , readmeEmptyHint_(new QLabel(this))
    , remoteFilesList_(new QListWidget(this))
    , remoteFilesEmptyHint_(new QLabel(this))
    , statsLabel_(new QLabel(this))
    , topicsLabel_(new QLabel(this))
    , defaultBranchLabel_(new QLabel(this))
    , primaryLangLabel_(new QLabel(this))
    , languagesBarHost_(new QWidget(this))
    , languagesBarLayout_(new QHBoxLayout(languagesBarHost_))
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
    btnRow->addWidget(openBrowserBtn_);
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

    // --- File tree (local mode) ---
    fsModel_->setRootPath(QDir::homePath());     // placeholder root
    fsModel_->setReadOnly(true);
    fileTree_->setModel(fsModel_);
    fileTree_->setRootIndex(QModelIndex());      // hidden until repo loaded
    fileTree_->setHeaderHidden(false);
    fileTree_->setColumnHidden(2, true);          // type
    fileTree_->setColumnHidden(3, true);          // date
    fileTree_->hide();

    // --- Detail tabs (README / Files / About) ---
    // README tab — QTextBrowser handles markdown via setMarkdown
    // since Qt 5.14. Read-only, link-clicking opens in browser via
    // the openInBrowserRequested signal.
    readmeView_->setOpenExternalLinks(false);
    readmeView_->setReadOnly(true);
    connect(readmeView_, &QTextBrowser::anchorClicked, this,
            [this](const QUrl& url) {
                Q_EMIT openInBrowserRequested(url.toString());
            });

    readmeEmptyHint_->setAlignment(Qt::AlignCenter);
    readmeEmptyHint_->setStyleSheet(QStringLiteral("color: #8a8a8a; padding: 24px;"));
    readmeEmptyHint_->setText(tr("Loading README…"));

    auto* readmePage = new QWidget(this);
    {
        auto* col = new QVBoxLayout(readmePage);
        col->setContentsMargins(0, 0, 0, 0);
        col->addWidget(readmeEmptyHint_);
        col->addWidget(readmeView_, 1);
        readmeView_->hide();  // start hidden, shown when content arrives
    }

    // Files tab — flat list of root-level entries from contents API.
    // Future enhancement: nested expansion. For now, single level is
    // already a huge improvement over "no preview at all".
    remoteFilesList_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(remoteFilesList_, &QListWidget::itemActivated,
            this, &RepositoryDetailWidget::onRemoteFileActivated);

    remoteFilesEmptyHint_->setAlignment(Qt::AlignCenter);
    remoteFilesEmptyHint_->setStyleSheet(QStringLiteral("color: #8a8a8a; padding: 24px;"));
    remoteFilesEmptyHint_->setText(tr("Loading files…"));

    auto* filesPage = new QWidget(this);
    {
        auto* col = new QVBoxLayout(filesPage);
        col->setContentsMargins(0, 0, 0, 0);
        col->addWidget(remoteFilesEmptyHint_);
        col->addWidget(remoteFilesList_, 1);
        remoteFilesList_->hide();
    }

    // About tab — stats grid, topics, languages bar.
    statsLabel_->setTextFormat(Qt::RichText);
    statsLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    topicsLabel_->setWordWrap(true);
    topicsLabel_->setTextFormat(Qt::RichText);
    defaultBranchLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    primaryLangLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    languagesBarLayout_->setContentsMargins(0, 0, 0, 0);
    languagesBarLayout_->setSpacing(1);  // tight, joined-bar look
    languagesBarHost_->setFixedHeight(12);

    auto* aboutPage = new QWidget(this);
    {
        auto* col = new QVBoxLayout(aboutPage);
        col->setContentsMargins(12, 12, 12, 12);
        col->addWidget(statsLabel_);

        col->addSpacing(8);
        auto* form = new QFormLayout;
        form->addRow(tr("Default branch:"), defaultBranchLabel_);
        form->addRow(tr("Primary language:"), primaryLangLabel_);
        form->addRow(tr("Topics:"), topicsLabel_);
        col->addLayout(form);

        col->addSpacing(8);
        auto* langTitle = new QLabel(tr("<b>Languages</b>"), aboutPage);
        langTitle->setTextFormat(Qt::RichText);
        col->addWidget(langTitle);
        col->addWidget(languagesBarHost_);

        col->addStretch();
    }

    detailTabs_->addTab(readmePage, tr("README"));
    detailTabs_->addTab(filesPage,  tr("Files"));
    detailTabs_->addTab(aboutPage,  tr("About"));

    auto* root = new QVBoxLayout(this);
    root->addWidget(metaBox);
    root->addLayout(btnRow);
    root->addWidget(localBox);
    root->addWidget(fileTree_, 0);   // hidden by default in remote mode
    root->addWidget(detailTabs_, 1);  // takes the rest of the vertical space

    connect(cloneBtn_,   &QPushButton::clicked, this, &RepositoryDetailWidget::onClone);
    connect(openBtn_,    &QPushButton::clicked, this, &RepositoryDetailWidget::onOpen);
    connect(pullBtn_,    &QPushButton::clicked, this, &RepositoryDetailWidget::onPull);
    connect(pushBtn_,    &QPushButton::clicked, this, &RepositoryDetailWidget::onPush);
    connect(refreshBtn_, &QPushButton::clicked, this, &RepositoryDetailWidget::onRefresh);
    connect(openBrowserBtn_, &QPushButton::clicked,
            this, &RepositoryDetailWidget::onOpenInBrowser);
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
        clearDetailTabs();
        detailTabs_->setVisible(false);
        openBrowserBtn_->setVisible(false);
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

    detailTabs_->setVisible(true);
    openBrowserBtn_->setVisible(!repo.htmlUrl.isEmpty());

    // Populate stats (always — fast, just rendering the data we
    // already have in `repo`). README/Files/Languages get a real
    // fetch.
    statsLabel_->setText(tr(
        "<span style='color:#e8b517;'>★</span> %1 &nbsp;&nbsp; "
        "<span style='color:#9aa0a6;'>⑂</span> %2 forks &nbsp;&nbsp; "
        "<span style='color:#9aa0a6;'>●</span> %3 open issues &nbsp;&nbsp; "
        "<span style='color:#9aa0a6;'>📦</span> %4")
        .arg(repo.stargazers)
        .arg(repo.forks)
        .arg(repo.openIssues)
        .arg(humanBytes(repo.sizeKb * 1024)));
    defaultBranchLabel_->setText(repo.defaultBranch.isEmpty()
        ? tr("(unknown)") : repo.defaultBranch);
    primaryLangLabel_->setText(repo.primaryLanguage.isEmpty()
        ? tr("(none detected)") : repo.primaryLanguage);
    if (repo.topics.isEmpty()) {
        topicsLabel_->setText(tr("<i>(none)</i>"));
    } else {
        // Render topics as pseudo-chip pills via inline CSS. Simple
        // padded backgrounds — not perfectly pill-shaped, but
        // recognisable as tags.
        QStringList chips;
        for (const QString& t : repo.topics) {
            chips << QStringLiteral(
                "<span style='background:#1f6feb22; color:#79c0ff; "
                "padding:2px 8px; border-radius:10px; "
                "margin-right:4px;'>%1</span>").arg(t.toHtmlEscaped());
        }
        topicsLabel_->setText(chips.join(QStringLiteral(" ")));
    }

    // Trigger detail fetches (uses cache when available).
    requestDetailForCurrent();
    applyMode();
}

void RepositoryDetailWidget::requestDetailForCurrent()
{
    if (!repo_.isValid()) return;
    const QString fn = repo_.fullName;

    // README: cache hit → render immediately; miss → request.
    if (readmeCache_.contains(fn)) {
        setReadme(fn, readmeCache_.value(fn));
    } else {
        readmeView_->hide();
        readmeEmptyHint_->setText(tr("Loading README…"));
        readmeEmptyHint_->show();
        Q_EMIT readmeRequested(fn);
    }

    // Contents.
    if (contentsCache_.contains(fn)) {
        setRemoteContents(fn, QString(), contentsCache_.value(fn));
    } else {
        remoteFilesList_->hide();
        remoteFilesEmptyHint_->setText(tr("Loading files…"));
        remoteFilesEmptyHint_->show();
        Q_EMIT contentsRequested(fn, QString());
    }

    // Languages.
    if (languagesCache_.contains(fn)) {
        setLanguages(fn, languagesCache_.value(fn));
    } else {
        Q_EMIT languagesRequested(fn);
    }
}

void RepositoryDetailWidget::clearDetailTabs()
{
    readmeView_->clear();
    readmeView_->hide();
    readmeEmptyHint_->setText(tr("Select a repository to see its README."));
    readmeEmptyHint_->show();

    remoteFilesList_->clear();
    remoteFilesList_->hide();
    remoteFilesEmptyHint_->setText(tr("Select a repository to see its files."));
    remoteFilesEmptyHint_->show();

    statsLabel_->clear();
    defaultBranchLabel_->clear();
    primaryLangLabel_->clear();
    topicsLabel_->clear();

    // Clear language bars
    while (auto* child = languagesBarLayout_->takeAt(0)) {
        if (auto* w = child->widget()) w->deleteLater();
        delete child;
    }
}

void RepositoryDetailWidget::setReadme(const QString& fullName,
                                        const QString& markdown)
{
    readmeCache_.insert(fullName, markdown);
    if (fullName != repo_.fullName) return;  // user switched repos while fetching

    if (markdown.isEmpty()) {
        readmeView_->hide();
        readmeEmptyHint_->setText(tr("README is empty."));
        readmeEmptyHint_->show();
        return;
    }
    // QTextBrowser::setMarkdown handles CommonMark + GFM tables.
    // Inline images won't render (we'd need an HTTP loader hooked
    // into QTextDocument's resource loader), but text+links work.
    readmeView_->setMarkdown(markdown);
    readmeEmptyHint_->hide();
    readmeView_->show();
}

void RepositoryDetailWidget::setReadmeUnavailable(const QString& fullName)
{
    // Cache the absence too so we don't re-request next time.
    readmeCache_.insert(fullName, QString());
    if (fullName != repo_.fullName) return;
    readmeView_->hide();
    readmeEmptyHint_->setText(tr(
        "This repository doesn't have a README in its default branch."));
    readmeEmptyHint_->show();
}

void RepositoryDetailWidget::setRemoteContents(
    const QString& fullName, const QString& path,
    const QList<ghm::github::ContentEntry>& entries)
{
    // Only cache root listings — nested paths aren't (yet) explored
    // by the UI. Conservative bound for what to remember.
    if (path.isEmpty()) contentsCache_.insert(fullName, entries);
    if (fullName != repo_.fullName) return;

    remoteFilesList_->clear();
    if (entries.isEmpty()) {
        remoteFilesList_->hide();
        remoteFilesEmptyHint_->setText(tr(
            "This repository appears to be empty."));
        remoteFilesEmptyHint_->show();
        return;
    }

    auto style = QApplication::style();
    for (const auto& e : entries) {
        auto* item = new QListWidgetItem(e.name, remoteFilesList_);
        const bool isDir = e.type == QLatin1String("dir");
        item->setIcon(isDir
            ? style->standardIcon(QStyle::SP_DirIcon)
            : style->standardIcon(QStyle::SP_FileIcon));
        // Size only meaningful for files
        if (!isDir && e.size > 0) {
            item->setToolTip(tr("%1 — %2").arg(e.name, humanBytes(e.size)));
        } else {
            item->setToolTip(e.name);
        }
        // Stash htmlUrl so onRemoteFileActivated can route to browser
        item->setData(Qt::UserRole, e.htmlUrl);
        item->setData(Qt::UserRole + 1, e.type);
    }
    remoteFilesEmptyHint_->hide();
    remoteFilesList_->show();
}

void RepositoryDetailWidget::setLanguages(
    const QString& fullName, const QMap<QString, qint64>& bytesByLang)
{
    languagesCache_.insert(fullName, bytesByLang);
    if (fullName != repo_.fullName) return;
    renderLanguagesBar();
}

void RepositoryDetailWidget::renderLanguagesBar()
{
    // Clear existing
    while (auto* child = languagesBarLayout_->takeAt(0)) {
        if (auto* w = child->widget()) w->deleteLater();
        delete child;
    }

    const auto langs = languagesCache_.value(repo_.fullName);
    if (langs.isEmpty()) {
        auto* empty = new QLabel(tr("<i>No language data.</i>"), languagesBarHost_);
        empty->setStyleSheet(QStringLiteral("color: #8a8a8a;"));
        languagesBarLayout_->addWidget(empty);
        return;
    }

    qint64 total = 0;
    for (auto v : langs) total += v;
    if (total <= 0) return;

    // Sort by byte count desc. QMap iterates by key by default so
    // we copy into a vector to sort by value.
    QList<QPair<QString, qint64>> pairs;
    for (auto it = langs.begin(); it != langs.end(); ++it) {
        pairs.append({it.key(), it.value()});
    }
    std::sort(pairs.begin(), pairs.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Render as a row of fixed-height colored frames whose widths
    // are proportional to their byte share. Tooltip on each shows
    // the actual percentage.
    const auto& colors = kLangColors();
    int idx = 0;
    for (const auto& p : pairs) {
        const double pct = double(p.second) * 100.0 / double(total);
        if (pct < 0.1) continue;  // skip noise
        auto* seg = new QFrame(languagesBarHost_);
        seg->setStyleSheet(QStringLiteral(
            "background-color: %1; border: none;")
            .arg(colors.at(idx % colors.size())));
        seg->setToolTip(QStringLiteral("%1 — %2%")
                          .arg(p.first).arg(pct, 0, 'f', 1));
        // Convert percentage to integer stretch factor (×100 so small
        // languages still get visible width)
        languagesBarLayout_->addWidget(seg, qMax(1, int(pct * 100)));
        ++idx;
    }
}

void RepositoryDetailWidget::onRemoteFileActivated(QListWidgetItem* item)
{
    if (!item) return;
    const QString url = item->data(Qt::UserRole).toString();
    if (url.isEmpty()) return;
    // Both files and dirs: open on github.com. We could descend into
    // dirs inline but that requires extra UI (back button, breadcrumb)
    // and the browser handles it perfectly.
    Q_EMIT openInBrowserRequested(url);
}

void RepositoryDetailWidget::onOpenInBrowser()
{
    if (repo_.htmlUrl.isEmpty()) return;
    Q_EMIT openInBrowserRequested(repo_.htmlUrl);
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
        // Local mode: show file tree, hide the remote-files tab
        // since the user has the real thing on disk. README and
        // About still useful — they describe the GitHub-side state
        // (stars/forks/topics) which differs from local.
        fsModel_->setRootPath(repo_.localPath);
        fileTree_->setRootIndex(fsModel_->index(repo_.localPath));
        fileTree_->show();
        // Hide the Files tab; user has fileTree_ for local view
        if (detailTabs_->count() >= 2) detailTabs_->setTabVisible(1, false);
        Q_EMIT refreshRequested(repo_.localPath);
    } else {
        fileTree_->hide();
        if (detailTabs_->count() >= 2) detailTabs_->setTabVisible(1, true);
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
