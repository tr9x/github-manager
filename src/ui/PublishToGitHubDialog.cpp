#include "ui/PublishToGitHubDialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
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

QString relativeTime(const QDateTime& when)
{
    if (!when.isValid()) return QStringLiteral("—");
    const qint64 secs = when.secsTo(QDateTime::currentDateTimeUtc());
    if (secs < 60)              return QObject::tr("just now");
    if (secs < 60 * 60)         return QObject::tr("%1 min ago").arg(secs / 60);
    if (secs < 60 * 60 * 24)    return QObject::tr("%1 h ago").arg(secs / 3600);
    if (secs < 60 * 60 * 24 * 30) return QObject::tr("%1 d ago").arg(secs / 86400);
    return QLocale().toString(when.toLocalTime().date(), QLocale::ShortFormat);
}

// Sanitises a folder name into something GitHub will accept.
// GitHub repo names: ASCII letters/digits, '-', '_', '.'. Anything else
// becomes a hyphen, runs of hyphens collapse, leading/trailing
// hyphens/dots are trimmed. Empty result means we couldn't derive
// anything sensible.
QString suggestRepoName(const QString& folderName)
{
    QString out;
    out.reserve(folderName.size());
    bool prevHyphen = false;
    for (QChar c : folderName) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-')
                                 || c == QLatin1Char('_')
                                 || c == QLatin1Char('.')) {
            out.append(c);
            prevHyphen = (c == QLatin1Char('-'));
        } else {
            if (!prevHyphen && !out.isEmpty()) out.append(QLatin1Char('-'));
            prevHyphen = true;
        }
    }
    while (!out.isEmpty() && (out.endsWith(QLatin1Char('-')) ||
                              out.endsWith(QLatin1Char('.')))) {
        out.chop(1);
    }
    while (!out.isEmpty() && (out.startsWith(QLatin1Char('-')) ||
                              out.startsWith(QLatin1Char('.')))) {
        out.remove(0, 1);
    }
    return out;
}

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
            } else {
                createWarningLabel_->setVisible(false);
            }
            updateOkState();
        };
        connect(nameEdit_, &QLineEdit::textChanged, this, [refreshPreview](const QString&) {
            refreshPreview();
        });
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
            .arg(r.fullName, visibility, relativeTime(r.updatedAt), localBadge);

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
