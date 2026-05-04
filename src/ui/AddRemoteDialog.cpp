#include "ui/AddRemoteDialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QRegularExpression>

namespace ghm::ui {

namespace {

struct Parsed {
    QString name;
    QString url;
    bool    ok{false};
};

// Try to recognise either the full `git remote add <name> <url>` form
// or a bare URL.
Parsed parsePaste(const QString& raw)
{
    Parsed out;
    QString s = raw.trimmed();
    if (s.isEmpty()) return out;

    // Strip an optional "$ " prompt prefix that some users copy from
    // GitHub's instruction blocks.
    if (s.startsWith(QLatin1String("$ "))) s.remove(0, 2);

    // Form: git remote add <name> <url> [...]
    static const QRegularExpression cmdRe(
        QStringLiteral(R"(^\s*git\s+remote\s+add\s+(\S+)\s+(\S+)\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = cmdRe.match(s); m.hasMatch()) {
        out.name = m.captured(1);
        out.url  = m.captured(2);
        out.ok   = true;
        return out;
    }

    // Bare URL — anything that looks like a recognised git URL scheme.
    static const QRegularExpression urlRe(
        QStringLiteral(R"(^(https?://\S+|git@\S+:\S+|ssh://\S+|git://\S+))"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = urlRe.match(s); m.hasMatch()) {
        out.url  = m.captured(1);
        out.name = QStringLiteral("origin");
        out.ok   = true;
        return out;
    }

    return out;
}

bool isSshUrl(const QString& url)
{
    return url.startsWith(QLatin1String("git@"))
        || url.startsWith(QLatin1String("ssh://"));
}

bool isHttpUrl(const QString& url)
{
    return url.startsWith(QLatin1String("http://"))
        || url.startsWith(QLatin1String("https://"));
}

} // namespace

AddRemoteDialog::AddRemoteDialog(const QString& suggestedBranch, QWidget* parent)
    : QDialog(parent)
    , pasteEdit_     (new QLineEdit(this))
    , nameEdit_      (new QLineEdit(this))
    , urlEdit_       (new QLineEdit(this))
    , warningLabel_  (new QLabel(this))
    , setUpstreamBox_(new QCheckBox(this))
    , buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
    , suggestedBranch_(suggestedBranch)
{
    setWindowTitle(tr("Add Git remote"));
    setModal(true);
    resize(560, 0);

    pasteEdit_->setPlaceholderText(
        tr("Paste 'git remote add origin https://…' or just the URL"));
    pasteEdit_->setClearButtonEnabled(true);

    nameEdit_->setPlaceholderText(QStringLiteral("origin"));
    nameEdit_->setText(QStringLiteral("origin"));

    urlEdit_->setPlaceholderText(QStringLiteral("https://github.com/<owner>/<repo>.git"));

    setUpstreamBox_->setText(
        suggestedBranch.isEmpty()
            ? tr("Set as upstream when I push (-u)")
            : tr("Set as upstream when I push (git push -u %1 %2)")
                .arg(QStringLiteral("origin"), suggestedBranch));
    setUpstreamBox_->setChecked(true);

    warningLabel_->setWordWrap(true);
    warningLabel_->setVisible(false);
    warningLabel_->setStyleSheet(
        QStringLiteral("color: #f0b400; padding: 6px; "
                       "border: 1px solid #4a3a00; border-radius: 4px; "
                       "background: #2a2110;"));

    auto* hint = new QLabel(
        tr("Tip: GitHub shows you a 'git remote add origin …' command on a freshly "
           "created empty repo. You can paste the whole line above and we'll fill "
           "the fields in for you."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto* form = new QFormLayout;
    form->addRow(tr("Paste:"), pasteEdit_);
    form->addRow(tr("Name:"),  nameEdit_);
    form->addRow(tr("URL:"),   urlEdit_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(hint);
    root->addWidget(warningLabel_);
    root->addSpacing(4);
    root->addWidget(setUpstreamBox_);
    root->addWidget(buttons_);

    connect(pasteEdit_, &QLineEdit::textChanged,
            this, &AddRemoteDialog::onPasteChanged);
    connect(urlEdit_, &QLineEdit::textChanged,
            this, [this] { updateWarning(); });
    connect(buttons_, &QDialogButtonBox::accepted,
            this, &AddRemoteDialog::onAccept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    pasteEdit_->setFocus();
    updateWarning();
}

QString AddRemoteDialog::name() const { return nameEdit_->text().trimmed(); }
QString AddRemoteDialog::url()  const { return urlEdit_->text().trimmed(); }
bool    AddRemoteDialog::setUpstreamOnPush() const { return setUpstreamBox_->isChecked(); }

void AddRemoteDialog::onPasteChanged(const QString& text)
{
    auto p = parsePaste(text);
    if (!p.ok) return;

    nameEdit_->setText(p.name);
    urlEdit_->setText(p.url);
    updateWarning();
}

void AddRemoteDialog::updateWarning()
{
    const QString u = urlEdit_->text().trimmed();
    if (u.isEmpty()) {
        warningLabel_->setVisible(false);
        return;
    }
    if (isSshUrl(u)) {
        warningLabel_->setText(
            tr("⚠ This is an SSH URL. The app authenticates with your GitHub "
               "personal access token over HTTPS. SSH URLs require a configured "
               "ssh-agent or key — push may fail. Consider using the HTTPS URL "
               "instead (https://github.com/owner/repo.git)."));
        warningLabel_->setVisible(true);
        return;
    }
    if (!isHttpUrl(u)) {
        warningLabel_->setText(
            tr("⚠ Unrecognised URL scheme. Push is supported for HTTPS GitHub "
               "URLs (https://github.com/owner/repo.git)."));
        warningLabel_->setVisible(true);
        return;
    }
    warningLabel_->setVisible(false);
}

void AddRemoteDialog::onAccept()
{
    if (name().isEmpty() || url().isEmpty()) return;
    accept();
}

} // namespace ghm::ui
