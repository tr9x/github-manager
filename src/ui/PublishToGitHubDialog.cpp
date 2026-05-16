#include "ui/PublishToGitHubDialog.h"
#include "ui/RepoNameSuggester.h"
#include "core/TimeFormatting.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QComboBox>
#include <QStackedWidget>
#include <QListWidget>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QLocale>

namespace ghm::ui {

namespace {

constexpr int kRepoRole = Qt::UserRole + 1;

// suggestRepoName moved to ui/RepoNameSuggester.h so unit tests can
// exercise it without spinning up Qt widgets.

} // namespace

PublishToGitHubDialog::PublishToGitHubDialog(
    const QString&                          folderName,
    const QString&                          suggestedRepoName,
    const QString&                          accountLogin,
    const QList<ghm::github::Repository>&   knownRepos,
    QWidget*                                parent)
    : QDialog(parent)
    , accountLogin_(accountLogin)
    , knownRepos_(knownRepos)
    , headerLabel_(new QLabel(this))
    , createRadio_(new QRadioButton(tr("Create a new GitHub repository"), this))
    , linkRadio_  (new QRadioButton(tr("Link to an existing repository"),  this))
    , pages_(new QStackedWidget(this))
    , nameEdit_(new QLineEdit(this))
    , descEdit_(new QLineEdit(this))
    , publicRadio_ (new QRadioButton(tr("Public"),  this))
    , privateRadio_(new QRadioButton(tr("Private"), this))
    , licenseCombo_(new QComboBox(this))
    , gitignoreCombo_(new QComboBox(this))
    , createPreviewLabel_(new QLabel(this))
    , createWarningLabel_(new QLabel(this))
    , searchEdit_(new QLineEdit(this))
    , existingList_(new QListWidget(this))
    , linkHintLabel_(new QLabel(this))
    , pushBox_(new QCheckBox(tr("Push my commits after publishing"), this))
    , buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("Publish to GitHub"));
    setModal(true);
    resize(580, 540);

    // --- Header --------------------------------------------------------
    QFont hf = headerLabel_->font();
    hf.setPointSizeF(hf.pointSizeF() * 1.15);
    hf.setBold(true);
    headerLabel_->setFont(hf);
    headerLabel_->setText(tr("Publish \"%1\" to GitHub").arg(folderName));

    auto* subtitle = new QLabel(
        tr("Sets up the <code>origin</code> remote on this folder and, if you want, "
           "pushes your commits in one go."), this);
    subtitle->setTextFormat(Qt::RichText);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    // --- Mode radios ---------------------------------------------------
    createRadio_->setChecked(true);
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(createRadio_, 0);
    modeGroup->addButton(linkRadio_,   1);
    connect(createRadio_, &QRadioButton::toggled,
            this, &PublishToGitHubDialog::onModeChanged);
    connect(linkRadio_,   &QRadioButton::toggled,
            this, &PublishToGitHubDialog::onModeChanged);

    // --- Create-new page -----------------------------------------------
    auto* createPage = new QWidget(this);
    {
        nameEdit_->setText(suggestedRepoName.isEmpty()
                            ? suggestRepoName(folderName)
                            : suggestedRepoName);
        nameEdit_->setPlaceholderText(QStringLiteral("my-cool-project"));

        descEdit_->setPlaceholderText(tr("Optional description shown on GitHub"));

        publicRadio_->setChecked(true);
        auto* visGroup = new QButtonGroup(createPage);
        visGroup->addButton(publicRadio_);
        visGroup->addButton(privateRadio_);

        auto* visRow = new QHBoxLayout;
        visRow->addWidget(publicRadio_);
        visRow->addSpacing(12);
        visRow->addWidget(privateRadio_);
        visRow->addStretch();

        // License combobox. The items are (display, github-key) pairs.
        // We hardcode the most common ~10 — these match GitHub's
        // "Choose a license" featured/common list. Fetching from
        // /licenses dynamically would add a network call before the
        // dialog could open; not worth it for a list that hasn't
        // changed in years. Empty "key" means "no license" (default).
        struct LicenseChoice { const char* display; const char* key; };
        static const LicenseChoice kLicenses[] = {
            { "(none)",                                            "" },
            { "MIT License",                                       "mit" },
            { "Apache License 2.0",                                "apache-2.0" },
            { "GNU General Public License v3.0",                   "gpl-3.0" },
            { "GNU General Public License v2.0",                   "gpl-2.0" },
            { "GNU Lesser General Public License v3.0",            "lgpl-3.0" },
            { "GNU Affero General Public License v3.0",            "agpl-3.0" },
            { "BSD 2-Clause \"Simplified\" License",               "bsd-2-clause" },
            { "BSD 3-Clause \"New\" or \"Revised\" License",       "bsd-3-clause" },
            { "Mozilla Public License 2.0",                        "mpl-2.0" },
            { "Boost Software License 1.0",                        "bsl-1.0" },
            { "The Unlicense",                                     "unlicense" },
        };
        for (const auto& l : kLicenses) {
            licenseCombo_->addItem(tr(l.display), QString::fromLatin1(l.key));
        }
        licenseCombo_->setToolTip(tr(
            "Adds a LICENSE file to the initial commit on GitHub. "
            "Required for repos you intend to share publicly. If your "
            "local folder already has commits, GitHub will create its "
            "own initial commit with LICENSE — you'll need to pull "
            "before pushing (or accept that LICENSE comes from "
            "GitHub's side only)."));

        // Gitignore combobox. Same shape as licenses; values come
        // from GitHub's gitignore template list. We carry a curated
        // common subset — Python, Node, C++, etc. Empty = no
        // .gitignore added.
        struct GitignoreChoice { const char* display; const char* key; };
        static const GitignoreChoice kGitignores[] = {
            { "(none)",      "" },
            { "C",           "C" },
            { "C++",         "C++" },
            { "Go",          "Go" },
            { "Java",        "Java" },
            { "JavaScript",  "Node" },        // GitHub key is "Node"
            { "Python",      "Python" },
            { "Ruby",        "Ruby" },
            { "Rust",        "Rust" },
            { "Swift",       "Swift" },
            { "Kotlin",      "Java" },        // closest match
            { "TypeScript",  "Node" },
            { "Visual Studio (.NET)", "VisualStudio" },
            { "Unity",       "Unity" },
            { "Qt",          "Qt" },
            { "CMake",       "CMake" },
        };
        for (const auto& g : kGitignores) {
            gitignoreCombo_->addItem(tr(g.display), QString::fromLatin1(g.key));
        }
        gitignoreCombo_->setToolTip(tr(
            "Adds a .gitignore file to the initial commit on GitHub. "
            "Same caveat as the license: if your local folder already "
            "has commits, you'll need to pull GitHub's initial commit "
            "before pushing."));

        createPreviewLabel_->setStyleSheet(QStringLiteral("color: #8ab4f8;"));
        createPreviewLabel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        createPreviewLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);

        createWarningLabel_->setWordWrap(true);
        createWarningLabel_->setStyleSheet(
            QStringLiteral("color: #f0b400; padding: 6px; "
                           "border: 1px solid #4a3a00; border-radius: 4px; "
                           "background: #2a2110;"));
        createWarningLabel_->setVisible(false);

        auto* form = new QFormLayout;
        form->addRow(tr("Owner:"),       new QLabel(accountLogin.isEmpty()
                                                        ? tr("(not signed in)")
                                                        : accountLogin, createPage));
        form->addRow(tr("Name:"),        nameEdit_);
        form->addRow(tr("Description:"), descEdit_);
        form->addRow(tr("Visibility:"),  visRow);
        form->addRow(tr("License:"),     licenseCombo_);
        form->addRow(tr(".gitignore:"),  gitignoreCombo_);

        auto* col = new QVBoxLayout(createPage);
        col->setContentsMargins(0, 0, 0, 0);
        col->addLayout(form);
        col->addSpacing(4);
        col->addWidget(createPreviewLabel_);
        col->addSpacing(4);
        col->addWidget(createWarningLabel_);
        col->addStretch();

        auto refreshPreview = [this] {
            const QString n = nameEdit_->text().trimmed();
            if (n.isEmpty() || accountLogin_.isEmpty()) {
                createPreviewLabel_->clear();
            } else {
                createPreviewLabel_->setText(
                    QStringLiteral("→ https://github.com/%1/%2.git")
                        .arg(accountLogin_, n));
            }

            // Validate against known repos (case-insensitive collision).
            bool collides = false;
            for (const auto& r : knownRepos_) {
                if (r.fullName.compare(QStringLiteral("%1/%2")
                                       .arg(accountLogin_, n),
                                       Qt::CaseInsensitive) == 0) {
                    collides = true;
                    break;
                }
            }
            if (collides) {
                createWarningLabel_->setText(
                    tr("⚠ You already have a repository called \"%1\". "
                       "GitHub will reject this name — choose a different one, "
                       "or switch to \"Link to an existing repository\".").arg(n));
                createWarningLabel_->setVisible(true);
            } else if (!n.isEmpty() && !isValidGithubName(n)) {
                createWarningLabel_->setText(
                    tr("⚠ GitHub repository names may only contain letters, "
                       "digits, hyphens, underscores, and dots."));
                createWarningLabel_->setVisible(true);
            } else if (licenseCombo_->currentIndex() > 0
                       || gitignoreCombo_->currentIndex() > 0) {
                // License OR gitignore picked → GitHub will auto_init
                // the repo. Local commits won't push cleanly until the
                // user pulls GitHub's initial commit. Warn them so the
                // workflow doesn't surprise them.
                createWarningLabel_->setText(
                    tr("ℹ Adding a LICENSE or .gitignore will make GitHub "
                       "create the repository with an initial commit. If "
                       "your local folder already has commits, you'll "
                       "need to run <code>git pull --rebase</code> before "
                       "pushing (or uncheck \"Push my commits after "
                       "publishing\" and resolve the merge yourself)."));
                createWarningLabel_->setTextFormat(Qt::RichText);
                createWarningLabel_->setVisible(true);
            } else {
                createWarningLabel_->setVisible(false);
            }
            updateOkState();
        };
        connect(nameEdit_, &QLineEdit::textChanged, this, [refreshPreview](const QString&) {
            refreshPreview();
        });
        // License / gitignore combo changes should also re-evaluate
        // the warning state — picking a license triggers the
        // auto_init notice; picking (none) clears it.
        connect(licenseCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [refreshPreview](int) { refreshPreview(); });
        connect(gitignoreCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [refreshPreview](int) { refreshPreview(); });
        refreshPreview();
    }

    // --- Link-existing page --------------------------------------------
    auto* linkPage = new QWidget(this);
    {
        searchEdit_->setPlaceholderText(tr("Search your repositories…"));
        searchEdit_->setClearButtonEnabled(true);
        connect(searchEdit_, &QLineEdit::textChanged,
                this, &PublishToGitHubDialog::onSearchChanged);

        existingList_->setUniformItemSizes(false);
        existingList_->setAlternatingRowColors(true);
        existingList_->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(existingList_, &QListWidget::itemSelectionChanged,
                this, &PublishToGitHubDialog::onExistingSelectionChanged);

        linkHintLabel_->setWordWrap(true);
        linkHintLabel_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
        linkHintLabel_->setText(
            tr("Pick the empty GitHub repository you just created. "
               "If your list is out of date, close this dialog and click "
               "Refresh in the toolbar."));

        auto* col = new QVBoxLayout(linkPage);
        col->setContentsMargins(0, 0, 0, 0);
        col->addWidget(searchEdit_);
        col->addWidget(existingList_, 1);
        col->addWidget(linkHintLabel_);
    }

    pages_->addWidget(createPage);
    pages_->addWidget(linkPage);

    // --- Common footer -------------------------------------------------
    pushBox_->setChecked(true);
    pushBox_->setToolTip(
        tr("Equivalent to running 'git push -u origin <branch>' immediately after "
           "wiring up the remote. Recommended for an empty GitHub repository."));

    auto* footerHint = new QLabel(
        tr("Uses your saved GitHub token over HTTPS. The token is never written "
           "to <code>.git/config</code>."), this);
    footerHint->setTextFormat(Qt::RichText);
    footerHint->setWordWrap(true);
    footerHint->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    connect(buttons_, &QDialogButtonBox::accepted,
            this, &PublishToGitHubDialog::onAccept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // --- Layout --------------------------------------------------------
    auto* root = new QVBoxLayout(this);
    root->addWidget(headerLabel_);
    root->addWidget(subtitle);
    root->addSpacing(8);

    auto* radioRow = new QHBoxLayout;
    radioRow->addWidget(createRadio_);
    radioRow->addSpacing(16);
    radioRow->addWidget(linkRadio_);
    radioRow->addStretch();
    root->addLayout(radioRow);

    root->addWidget(pages_, 1);
    root->addSpacing(4);
    root->addWidget(pushBox_);
    root->addWidget(footerHint);
    root->addWidget(buttons_);

    rebuildExistingList();
    onModeChanged();
}

// ----- Public accessors ----------------------------------------------------

QString PublishToGitHubDialog::name()        const { return nameEdit_->text().trimmed(); }
QString PublishToGitHubDialog::description() const { return descEdit_->text().trimmed(); }
bool    PublishToGitHubDialog::isPrivate()   const { return privateRadio_->isChecked(); }
bool    PublishToGitHubDialog::pushAfterPublish() const { return pushBox_->isChecked(); }

QString PublishToGitHubDialog::licenseTemplate() const
{
    // currentData() carries the GitHub key (e.g. "mit"); the "(none)"
    // entry has an empty string so a no-license selection naturally
    // returns "" to callers.
    return licenseCombo_ ? licenseCombo_->currentData().toString() : QString();
}

QString PublishToGitHubDialog::gitignoreTemplate() const
{
    return gitignoreCombo_ ? gitignoreCombo_->currentData().toString() : QString();
}

ghm::github::Repository PublishToGitHubDialog::existingRepo() const
{
    auto* item = existingList_->currentItem();
    if (!item) return {};
    return item->data(kRepoRole).value<ghm::github::Repository>();
}

// ----- Slot handlers -------------------------------------------------------

void PublishToGitHubDialog::onModeChanged()
{
    mode_ = createRadio_->isChecked() ? Mode::CreateNew : Mode::LinkExisting;
    pages_->setCurrentIndex(mode_ == Mode::CreateNew ? 0 : 1);
    updateOkState();
}

void PublishToGitHubDialog::onSearchChanged(const QString&)
{
    rebuildExistingList();
}

void PublishToGitHubDialog::onExistingSelectionChanged()
{
    updateOkState();
}

void PublishToGitHubDialog::onAccept()
{
    if (mode_ == Mode::CreateNew) {
        if (name().isEmpty() || !isValidGithubName(name())) return;
    } else {
        if (!existingRepo().isValid()) return;
    }
    accept();
}

// ----- Helpers -------------------------------------------------------------

void PublishToGitHubDialog::rebuildExistingList()
{
    const QString filter = searchEdit_->text().trimmed().toLower();
    existingList_->clear();

    for (const auto& r : knownRepos_) {
        if (!filter.isEmpty() &&
            !r.fullName.toLower().contains(filter) &&
            !r.description.toLower().contains(filter)) {
            continue;
        }
        const QString visibility = r.isPrivate ? tr("private") : tr("public");
        const QString localBadge = r.localPath.isEmpty()
            ? QString()
            : QStringLiteral(" • already cloned");
        const QString line = QStringLiteral("%1\n  %2 • %3%4")
            .arg(r.fullName, visibility, ghm::core::relativeTime(r.updatedAt), localBadge);

        auto* item = new QListWidgetItem(line, existingList_);
        item->setData(kRepoRole, QVariant::fromValue(r));
        item->setToolTip(r.cloneUrl);

        // Soft-discourage repos that already have a clone tracked — they're
        // probably not the empty repo the user just made for this folder.
        if (!r.localPath.isEmpty()) {
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
            item->setForeground(QColor(0x9a, 0xa0, 0xa6));
        }
    }
}

void PublishToGitHubDialog::updateOkState()
{
    bool ok = false;
    if (mode_ == Mode::CreateNew) {
        ok = !accountLogin_.isEmpty()
          && !name().isEmpty()
          && isValidGithubName(name())
          && !createWarningLabel_->isVisible();
    } else {
        ok = existingRepo().isValid();
    }
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
    buttons_->button(QDialogButtonBox::Ok)->setText(
        mode_ == Mode::CreateNew ? tr("Create && Publish") : tr("Link && Publish"));
}

bool PublishToGitHubDialog::isValidGithubName(const QString& s) const
{
    static const QRegularExpression re(
        QStringLiteral(R"(^[A-Za-z0-9._-]+$)"));
    if (!re.match(s).hasMatch()) return false;
    if (s == QLatin1String(".") || s == QLatin1String("..")) return false;
    return true;
}

} // namespace ghm::ui
