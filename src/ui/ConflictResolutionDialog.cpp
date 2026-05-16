#include "ui/ConflictResolutionDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QFontDatabase>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QMessageBox>
#include <QStackedWidget>

namespace ghm::ui {

namespace {

constexpr int kConflictPathRole = Qt::UserRole + 1;

QPlainTextEdit* makeMonoView(QWidget* parent, const QString& placeholder)
{
    auto* v = new QPlainTextEdit(parent);
    v->setReadOnly(true);
    v->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    v->setPlaceholderText(placeholder);
    v->setLineWrapMode(QPlainTextEdit::NoWrap);
    return v;
}

QWidget* makePaneColumn(const QString& title, QPlainTextEdit* view,
                        const QString& tooltip)
{
    auto* col = new QWidget;
    auto* lay = new QVBoxLayout(col);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);

    auto* header = new QLabel(title);
    header->setStyleSheet(QStringLiteral(
        "padding: 4px 8px; background: #2a3140; color: #c0d0e0; "
        "font-weight: bold; border-radius: 3px;"));
    header->setToolTip(tooltip);

    lay->addWidget(header);
    lay->addWidget(view, 1);
    return col;
}

} // namespace

ConflictResolutionDialog::ConflictResolutionDialog(QWidget* parent)
    : QDialog(parent)
    , list_              (new QListWidget(this))
    , oursView_          (makeMonoView(this, tr("ours (HEAD) — your branch's version")))
    , baseView_          (makeMonoView(this, tr("base — common ancestor")))
    , theirsView_        (makeMonoView(this, tr("theirs — incoming branch's version")))
    , placeholder_       (new QLabel(tr("No conflicts. The merge is ready to commit."), this))
    , panesSplit_        (new QSplitter(Qt::Horizontal, this))
    , mainSplit_         (new QSplitter(Qt::Horizontal, this))
    , openEditorBtn_     (new QPushButton(tr("Open in editor"), this))
    , showWorkingTreeBtn_(new QPushButton(tr("Show working file"), this))
    , markResolvedBtn_   (new QPushButton(tr("Mark resolved"), this))
    , abortBtn_          (new QPushButton(tr("Abort merge…"), this))
    , closeBtn_          (new QPushButton(tr("Close"), this))
{
    setWindowTitle(tr("Resolve conflicts"));
    setModal(false);
    resize(1100, 700);

    // ---- Conflict list (left) --------------------------------------
    list_->setUniformItemSizes(true);
    list_->setAlternatingRowColors(true);
    list_->setMinimumWidth(220);
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &ConflictResolutionDialog::onListSelectionChanged);

    placeholder_->setAlignment(Qt::AlignCenter);
    placeholder_->setWordWrap(true);
    placeholder_->setStyleSheet(QStringLiteral(
        "color: #707680; padding: 24px;"));

    // ---- Three side-by-side panes (right) --------------------------
    panesSplit_->addWidget(makePaneColumn(
        tr("Ours (HEAD)"), oursView_,
        tr("The version of the file on the branch you were on when "
           "the merge started.")));
    panesSplit_->addWidget(makePaneColumn(
        tr("Base"), baseView_,
        tr("The common ancestor — the version where the two branches "
           "agreed before they diverged.")));
    panesSplit_->addWidget(makePaneColumn(
        tr("Theirs"), theirsView_,
        tr("The version coming in from the branch being merged.")));
    // Equal-width default. Users can drag to favour one pane.
    panesSplit_->setSizes({400, 400, 400});

    // Vertical scroll-sync. When the user scrolls one pane the other
    // two follow. Editors that highlight matching regions across
    // panes are nicer, but value is mostly in not having to re-scroll
    // each pane individually.
    auto syncScroll = [this](QPlainTextEdit* src) {
        connect(src->verticalScrollBar(), &QScrollBar::valueChanged,
                this, [this, src](int v) {
            // Block recursive triggers — when we set value on the
            // other panes, their valueChanged fires too.
            QSignalBlocker b1(oursView_->verticalScrollBar());
            QSignalBlocker b2(baseView_->verticalScrollBar());
            QSignalBlocker b3(theirsView_->verticalScrollBar());
            if (src != oursView_)   oursView_  ->verticalScrollBar()->setValue(v);
            if (src != baseView_)   baseView_  ->verticalScrollBar()->setValue(v);
            if (src != theirsView_) theirsView_->verticalScrollBar()->setValue(v);
        });
    };
    syncScroll(oursView_);
    syncScroll(baseView_);
    syncScroll(theirsView_);

    // ---- Stack: panes vs placeholder -------------------------------
    auto* paneStack = new QStackedWidget(this);
    paneStack->addWidget(placeholder_);
    paneStack->addWidget(panesSplit_);
    paneStack->setCurrentIndex(0);
    paneStack->setObjectName(QStringLiteral("paneStack"));

    mainSplit_->addWidget(list_);
    mainSplit_->addWidget(paneStack);
    mainSplit_->setStretchFactor(0, 0);
    mainSplit_->setStretchFactor(1, 1);
    mainSplit_->setSizes({250, 800});

    // ---- Bottom buttons --------------------------------------------
    openEditorBtn_->setToolTip(
        tr("Open the working-tree version in your default editor so "
           "you can resolve the conflict markers manually. When done, "
           "save and click 'Mark resolved'."));
    showWorkingTreeBtn_->setToolTip(
        tr("Open the folder containing the conflicted file."));
    markResolvedBtn_->setToolTip(
        tr("Stage the working-tree version of this file and clear its "
           "conflict markers from git's index. Run after you've edited "
           "the file to remove the <<<<<<< / ======= / >>>>>>> markers."));
    abortBtn_->setToolTip(
        tr("Cancel the in-progress merge. The repository goes back to "
           "the state it was in before the merge started — your work "
           "in this merge is lost. Equivalent to `git merge --abort`."));

    markResolvedBtn_->setDefault(true);
    closeBtn_->setAutoDefault(false);

    connect(openEditorBtn_,      &QPushButton::clicked,
            this, &ConflictResolutionDialog::onOpenInEditorClicked);
    connect(showWorkingTreeBtn_, &QPushButton::clicked,
            this, &ConflictResolutionDialog::onShowWorkingTreeClicked);
    connect(markResolvedBtn_,    &QPushButton::clicked,
            this, &ConflictResolutionDialog::onMarkResolvedClicked);
    connect(abortBtn_,           &QPushButton::clicked,
            this, &ConflictResolutionDialog::onAbortMergeClicked);
    connect(closeBtn_,           &QPushButton::clicked,
            this, &QDialog::accept);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(openEditorBtn_);
    btnRow->addWidget(showWorkingTreeBtn_);
    btnRow->addStretch();
    btnRow->addWidget(abortBtn_);
    btnRow->addSpacing(20);
    btnRow->addWidget(markResolvedBtn_);
    btnRow->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(mainSplit_, 1);
    root->addLayout(btnRow);

    updateButtons();
}

void ConflictResolutionDialog::setEntries(
    const std::vector<ghm::git::ConflictEntry>& entries)
{
    // Preserve the user's selection across refreshes (after they
    // mark one file resolved, the list shrinks; we want the next
    // unresolved file to feel like a natural continuation).
    const QString prevSelected = currentPath();

    list_->clear();
    auto* paneStack = findChild<QStackedWidget*>(QStringLiteral("paneStack"));
    if (paneStack) paneStack->setCurrentIndex(entries.empty() ? 0 : 1);

    if (entries.empty()) {
        oursView_->clear();
        baseView_->clear();
        theirsView_->clear();
        currentlyShownPath_.clear();
        updateButtons();
        return;
    }

    int reselectRow = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        auto* item = new QListWidgetItem(e.path, list_);
        item->setData(kConflictPathRole, e.path);

        // Annotate the kind of conflict — useful at-a-glance hint
        // without opening every file.
        QString kind;
        if      (e.ancestorSha.isEmpty()) kind = tr("(add/add)");
        else if (e.oursSha.isEmpty())     kind = tr("(deleted by us)");
        else if (e.theirsSha.isEmpty())   kind = tr("(deleted by them)");
        if (!kind.isEmpty()) {
            item->setText(QStringLiteral("%1   %2").arg(e.path, kind));
        }

        if (e.path == prevSelected) reselectRow = static_cast<int>(i);
    }
    list_->setCurrentRow(reselectRow);
    updateButtons();
}

void ConflictResolutionDialog::setBlobsForEntry(
    const ghm::git::ConflictEntry& entry)
{
    // Stale results check: by the time the worker fetches blobs the
    // user might have moved on. Drop the result silently if so.
    if (entry.path != currentPath()) return;

    auto setOrPlaceholder = [](QPlainTextEdit* view, const QString& sha,
                                const QByteArray& content,
                                const QString& missingMsg) {
        if (sha.isEmpty()) {
            view->setPlainText(QStringLiteral("⟨ %1 ⟩").arg(missingMsg));
            view->setEnabled(false);
            return;
        }
        view->setEnabled(true);
        view->setPlainText(QString::fromUtf8(content));
    };

    setOrPlaceholder(oursView_,   entry.oursSha,     entry.oursContent,
                     tr("file did not exist on this side"));
    setOrPlaceholder(baseView_,   entry.ancestorSha, entry.ancestorContent,
                     tr("file is new (no common ancestor)"));
    setOrPlaceholder(theirsView_, entry.theirsSha,   entry.theirsContent,
                     tr("file did not exist on this side"));

    currentlyShownPath_ = entry.path;
}

void ConflictResolutionDialog::setBusy(bool busy)
{
    busy_ = busy;
    updateButtons();
}

void ConflictResolutionDialog::setRepoPath(const QString& localPath)
{
    repoPath_ = localPath;
}

QString ConflictResolutionDialog::currentPath() const
{
    auto* it = list_->currentItem();
    if (!it) return QString();
    return it->data(kConflictPathRole).toString();
}

void ConflictResolutionDialog::updateButtons()
{
    const bool hasSelection = list_->currentItem() != nullptr;
    const bool hasAnyConflicts = list_->count() > 0;

    openEditorBtn_     ->setEnabled(hasSelection && !busy_);
    showWorkingTreeBtn_->setEnabled(hasSelection && !busy_);
    markResolvedBtn_   ->setEnabled(hasSelection && !busy_);
    abortBtn_          ->setEnabled(hasAnyConflicts && !busy_);
}

void ConflictResolutionDialog::onListSelectionChanged()
{
    updateButtons();
    const QString p = currentPath();
    if (!p.isEmpty() && p != currentlyShownPath_) {
        // Clear panes immediately so the user sees that something is
        // about to change rather than stale content from the previous
        // selection. The host fills them via setBlobsForEntry().
        oursView_->setPlainText(tr("Loading…"));
        baseView_->setPlainText(tr("Loading…"));
        theirsView_->setPlainText(tr("Loading…"));
        Q_EMIT entrySelectionChanged(p);
    }
}

void ConflictResolutionDialog::onMarkResolvedClicked()
{
    const QString p = currentPath();
    if (p.isEmpty()) return;

    // No confirmation here — Mark resolved is recoverable (the user
    // can edit the file again and re-mark). The destructive action
    // is "Abort merge", which has its own confirmation.
    Q_EMIT markResolvedRequested(p);
}

void ConflictResolutionDialog::onAbortMergeClicked()
{
    const auto reply = QMessageBox::question(this, tr("Abort merge"),
        tr("Cancel the in-progress merge?\n\n"
           "Any progress you've made resolving conflicts will be lost. "
           "The repository will return to the state it was in before "
           "the merge started.\n\n"
           "Equivalent to: git merge --abort"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;
    Q_EMIT abortMergeRequested();
}

void ConflictResolutionDialog::onOpenInEditorClicked()
{
    const QString p = currentPath();
    if (p.isEmpty() || repoPath_.isEmpty()) return;

    // Use QDesktopServices so the OS picks the user's default editor
    // for the file's extension. On Linux this honours xdg-open's
    // associations; we don't try to second-guess the user's choice
    // by calling EDITOR or hardcoding vim/code.
    const QString abs = QDir(repoPath_).absoluteFilePath(p);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(abs))) {
        QMessageBox::warning(this, tr("Open in editor"),
            tr("Could not open the file. Make sure your desktop has a "
               "default application registered for this file type, "
               "then edit the file at:\n\n%1").arg(abs));
    }
}

void ConflictResolutionDialog::onShowWorkingTreeClicked()
{
    const QString p = currentPath();
    if (p.isEmpty() || repoPath_.isEmpty()) return;

    // Open the parent folder rather than the file itself — useful
    // when the user wants to compare against neighbouring files or
    // run their own merge tool from there.
    const QString abs = QDir(repoPath_).absoluteFilePath(p);
    const QString folder = QFileInfo(abs).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

} // namespace ghm::ui
