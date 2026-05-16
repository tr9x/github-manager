#include "ui/SigningSettingsDialog.h"

#include <git2.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>

namespace ghm::ui {

SigningSettingsDialog::SigningSettingsDialog(ghm::core::Settings& settings,
                                              QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
    , modeCombo_     (new QComboBox(this))
    , modePages_     (new QStackedWidget(this))
    , gpgKeyEdit_    (new QLineEdit(this))
    , sshKeyPathEdit_(new QLineEdit(this))
    , browseBtn_     (new QPushButton(tr("Browse…"), this))
    , importBtn_     (new QPushButton(tr("Import from git config"), this))
    , okBtn_         (new QPushButton(tr("Save"),    this))
    , cancelBtn_     (new QPushButton(tr("Cancel"),  this))
{
    setWindowTitle(tr("Commit signing"));
    setModal(true);
    setMinimumWidth(540);

    // The combobox carries the SigningMode value in user-data — saves
    // a lookup table on save. Items must be added in the order we
    // expect indices in modePages_.
    using SM = ghm::core::Settings::SigningMode;
    modeCombo_->addItem(tr("None (commits not signed)"),
                        static_cast<int>(SM::None));
    modeCombo_->addItem(tr("GPG (gpg / gpg2)"),
                        static_cast<int>(SM::Gpg));
    modeCombo_->addItem(tr("SSH (ssh-keygen, git 2.34+)"),
                        static_cast<int>(SM::Ssh));

    // None page — informational only.
    auto* nonePage = new QWidget(this);
    {
        auto* lay = new QVBoxLayout(nonePage);
        auto* lbl = new QLabel(tr(
            "Commits will not be signed. This matches git's default. "
            "GitHub will show commits as <i>Unverified</i>, but they "
            "still work normally otherwise."), nonePage);
        lbl->setWordWrap(true);
        lbl->setStyleSheet(QStringLiteral("color: #8aa3c2;"));
        lay->addWidget(lbl);
        lay->addStretch();
    }

    // GPG page.
    auto* gpgPage = new QWidget(this);
    {
        auto* lay = new QVBoxLayout(gpgPage);
        gpgKeyEdit_->setPlaceholderText(
            QStringLiteral("0x1234ABCD or full fingerprint"));
        auto* form = new QFormLayout;
        form->addRow(tr("GPG key ID:"), gpgKeyEdit_);
        lay->addLayout(form);
        auto* hint = new QLabel(tr(
            "Find your GPG key IDs by running <code>gpg --list-secret-keys "
            "--keyid-format=long</code> in a terminal. Use the full "
            "fingerprint (without spaces) if your keyring has multiple "
            "keys with the same short ID."), gpgPage);
        hint->setTextFormat(Qt::RichText);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: #8aa3c2;"));
        lay->addWidget(hint);
        lay->addStretch();
    }

    // SSH page.
    auto* sshPage = new QWidget(this);
    {
        auto* lay = new QVBoxLayout(sshPage);
        sshKeyPathEdit_->setPlaceholderText(
            QStringLiteral("/home/you/.ssh/id_ed25519"));
        auto* row = new QHBoxLayout;
        row->addWidget(sshKeyPathEdit_, 1);
        row->addWidget(browseBtn_);
        auto* form = new QFormLayout;
        form->addRow(tr("SSH key path:"), row);
        lay->addLayout(form);
        auto* hint = new QLabel(tr(
            "Pick the <b>private</b> key file. ssh-keygen uses it for "
            "signing. If the key has a passphrase, make sure it's "
            "loaded into ssh-agent (<code>ssh-add &lt;keypath&gt;</code>) "
            "or signing will hang waiting for input. Requires git 2.34+ "
            "and OpenSSH ≥ 8.0 on the verifier side."), sshPage);
        hint->setTextFormat(Qt::RichText);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: #8aa3c2;"));
        lay->addWidget(hint);
        lay->addStretch();
    }

    modePages_->addWidget(nonePage);  // index 0
    modePages_->addWidget(gpgPage);   // index 1
    modePages_->addWidget(sshPage);   // index 2

    auto* topRow = new QFormLayout;
    topRow->addRow(tr("Mode:"), modeCombo_);

    auto* buttons = new QHBoxLayout;
    importBtn_->setToolTip(tr(
        "Read user.signingkey and gpg.format from your global "
        "git config (~/.gitconfig) and populate the fields above. "
        "Leaves anything not present in your config untouched."));
    buttons->addWidget(importBtn_);
    buttons->addStretch();
    buttons->addWidget(cancelBtn_);
    buttons->addWidget(okBtn_);
    okBtn_->setDefault(true);

    auto* root = new QVBoxLayout(this);
    root->addLayout(topRow);
    root->addWidget(modePages_, 1);
    root->addLayout(buttons);

    connect(modeCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SigningSettingsDialog::onModeChanged);
    connect(browseBtn_, &QPushButton::clicked,
            this, &SigningSettingsDialog::onBrowseSshKey);
    connect(importBtn_, &QPushButton::clicked,
            this, &SigningSettingsDialog::onImportFromGitConfig);
    connect(okBtn_,     &QPushButton::clicked,
            this, &SigningSettingsDialog::onAccepted);
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);

    loadFromSettings();
}

void SigningSettingsDialog::loadFromSettings()
{
    using SM = ghm::core::Settings::SigningMode;

    const SM current = settings_.signingMode();
    int comboIdx = 0;
    switch (current) {
        case SM::None: comboIdx = 0; break;
        case SM::Gpg:  comboIdx = 1; break;
        case SM::Ssh:  comboIdx = 2; break;
    }
    modeCombo_->setCurrentIndex(comboIdx);
    modePages_->setCurrentIndex(comboIdx);

    // Populate the key fields. We share one storage key
    // (`git/signingKey`) across both modes — if the user switches
    // GPG↔SSH, the previous mode's key shows up in the wrong field.
    // To avoid that confusion, we put the stored key into whichever
    // field matches the current mode and leave the other empty.
    const QString key = settings_.signingKey();
    if (current == SM::Gpg) {
        gpgKeyEdit_->setText(key);
    } else if (current == SM::Ssh) {
        sshKeyPathEdit_->setText(key);
    }
}

void SigningSettingsDialog::onModeChanged(int index)
{
    modePages_->setCurrentIndex(index);
}

void SigningSettingsDialog::onBrowseSshKey()
{
    // Default the file picker at ~/.ssh — that's where 99% of users
    // keep their keys. If it doesn't exist we fall back to home.
    const QString homeDir =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    QString startDir = QDir(homeDir).filePath(QStringLiteral(".ssh"));
    if (!QDir(startDir).exists()) startDir = homeDir;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select SSH private key"), startDir,
        tr("All files (*)"));
    if (path.isEmpty()) return;
    sshKeyPathEdit_->setText(path);
}

void SigningSettingsDialog::onImportFromGitConfig()
{
    // Read three keys from global git config:
    //   gpg.format       — "openpgp" (default) or "ssh"
    //   user.signingkey  — key ID for GPG, key path for SSH
    //   commit.gpgsign   — bool, indicates the user *wants* signing
    //                      enabled. We use this to decide whether to
    //                      switch mode away from None on import.
    //
    // If a key is unset, we leave the corresponding field alone — no
    // surprise wipes of what the user already typed.
    git_config* cfgRaw = nullptr;
    if (git_config_open_default(&cfgRaw) != 0 || !cfgRaw) {
        return;
    }
    auto cfgFree = [cfgRaw] { git_config_free(cfgRaw); };

    QString format;        // "" / "openpgp" / "ssh"
    QString signingKey;    // GPG key ID or SSH path
    bool    gpgsignOn = false;

    {
        git_buf buf = GIT_BUF_INIT_CONST(nullptr, 0);
        if (git_config_get_string_buf(&buf, cfgRaw, "gpg.format") == 0) {
            format = QString::fromUtf8(buf.ptr, static_cast<int>(buf.size))
                       .trimmed().toLower();
        }
        git_buf_dispose(&buf);
    }
    {
        git_buf buf = GIT_BUF_INIT_CONST(nullptr, 0);
        if (git_config_get_string_buf(&buf, cfgRaw, "user.signingkey") == 0) {
            signingKey = QString::fromUtf8(buf.ptr,
                static_cast<int>(buf.size)).trimmed();
            // SSH path may begin with ~/; expand.
            if (signingKey.startsWith(QLatin1String("~/"))) {
                signingKey = QStandardPaths::writableLocation(
                    QStandardPaths::HomeLocation) + signingKey.mid(1);
            }
        }
        git_buf_dispose(&buf);
    }
    {
        int val = 0;
        if (git_config_get_bool(&val, cfgRaw, "commit.gpgsign") == 0) {
            gpgsignOn = (val != 0);
        }
    }
    cfgFree();

    // Decide the mode. Order:
    //   1. If gpg.format == "ssh" → SSH mode
    //   2. If user.signingkey looks like a path (contains '/') → SSH
    //   3. If user.signingkey is non-empty → GPG (default for openpgp)
    //   4. If nothing matches → leave mode unchanged
    //
    // The path heuristic catches the common case where someone has
    // gpg.format=ssh + user.signingkey=~/.ssh/id_ed25519 (which is
    // technically how SSH signing is configured in git docs).
    int newComboIdx = -1;
    if (format == QLatin1String("ssh") ||
        signingKey.contains(QLatin1Char('/'))) {
        newComboIdx = 2;  // SSH (index matches modePages_ stacking)
        sshKeyPathEdit_->setText(signingKey);
    } else if (!signingKey.isEmpty()) {
        newComboIdx = 1;  // GPG
        gpgKeyEdit_->setText(signingKey);
    }
    if (newComboIdx >= 0) {
        modeCombo_->setCurrentIndex(newComboIdx);
        modePages_->setCurrentIndex(newComboIdx);
    }

    // We deliberately don't auto-flip the user's chosen mode to None
    // if commit.gpgsign=false in their git config — they're in OUR
    // settings dialog because they want to manage this here, so
    // we treat the import as "populate the fields, don't override
    // intent". gpgsignOn is read but unused (kept for future use).
    (void)gpgsignOn;
}

void SigningSettingsDialog::onAccepted()
{
    using SM = ghm::core::Settings::SigningMode;
    const SM mode = static_cast<SM>(
        modeCombo_->currentData().toInt());

    settings_.setSigningMode(mode);
    QString key;
    if (mode == SM::Gpg) key = gpgKeyEdit_->text().trimmed();
    if (mode == SM::Ssh) key = sshKeyPathEdit_->text().trimmed();
    // For None we leave whatever was previously stored — switching
    // off signing doesn't have to clear the user's key reference.
    if (mode != SM::None) settings_.setSigningKey(key);

    accept();
}

} // namespace ghm::ui
