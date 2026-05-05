#include "ui/CreateBranchDialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFontDatabase>

namespace ghm::ui {

namespace {

// Subset of git-check-ref-format rules that catch the obvious
// mistakes. We're not trying to be exhaustive — libgit2 will reject
// anything we miss with a clear error. The point is to give immediate
// feedback for things like "feature branch" (space) or starting with '.'.
//
// Rules enforced here:
//   * cannot be empty
//   * cannot contain space, tab, or any of: ~ ^ : ? * [ (backslash)
//   * cannot start with '-' or '.'
//   * cannot end with '/' or '.'
//   * cannot contain ".." or "@{"
//   * cannot be exactly "HEAD" or "@"
bool nameLooksValid(const QString& s, QString* whyNot)
{
    auto fail = [&](QString reason) {
        if (whyNot) *whyNot = std::move(reason);
        return false;
    };

    if (s.isEmpty())                       return fail(QObject::tr("Name is empty."));
    if (s.startsWith(QLatin1Char('-')))    return fail(QObject::tr("Cannot start with '-'."));
    if (s.startsWith(QLatin1Char('.')))    return fail(QObject::tr("Cannot start with '.'."));
    if (s.endsWith(QLatin1Char('/')))      return fail(QObject::tr("Cannot end with '/'."));
    if (s.endsWith(QLatin1Char('.')))      return fail(QObject::tr("Cannot end with '.'."));
    if (s == QLatin1String("HEAD") ||
        s == QLatin1String("@"))            return fail(QObject::tr("Reserved name."));
    if (s.contains(QLatin1String("..")))   return fail(QObject::tr("Cannot contain '..'."));
    if (s.contains(QLatin1String("@{")))   return fail(QObject::tr("Cannot contain '@{'."));

    for (QChar c : s) {
        if (c.isSpace())                    return fail(QObject::tr("Cannot contain whitespace."));
        const ushort u = c.unicode();
        if (u == '~' || u == '^' || u == ':' || u == '?' ||
            u == '*' || u == '[' || u == '\\') {
            return fail(QObject::tr("Cannot contain '%1'.").arg(c));
        }
        if (u < 0x20 || u == 0x7F) {
            return fail(QObject::tr("Cannot contain control characters."));
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------

CreateBranchDialog::CreateBranchDialog(const QString&     currentBranch,
                                       const QStringList& existingNames,
                                       QWidget*           parent)
    : QDialog(parent)
    , existingNames_(existingNames)
    , nameEdit_   (new QLineEdit(this))
    , checkoutBox_(new QCheckBox(tr("Switch to the new branch after creating"), this))
    , hintLabel_  (new QLabel(this))
    , errorLabel_ (new QLabel(this))
    , buttons_    (new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("Create branch"));
    setModal(true);
    resize(440, 0);

    nameEdit_->setPlaceholderText(QStringLiteral("feature/login-form"));
    nameEdit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    checkoutBox_->setChecked(true);
    checkoutBox_->setToolTip(
        tr("Equivalent to 'git checkout -b <name>'. Uncheck to create the branch "
           "without switching to it (like 'git branch <name>')."));

    hintLabel_->setWordWrap(true);
    hintLabel_->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    if (currentBranch.isEmpty() || currentBranch.startsWith(QLatin1Char('('))) {
        hintLabel_->setText(tr("The new branch will point at the current HEAD."));
    } else {
        hintLabel_->setText(
            tr("The new branch will be created from <b>%1</b> "
               "(your current branch).").arg(currentBranch.toHtmlEscaped()));
        hintLabel_->setTextFormat(Qt::RichText);
    }

    errorLabel_->setWordWrap(true);
    errorLabel_->setVisible(false);
    errorLabel_->setStyleSheet(
        QStringLiteral("color: #f0b400; padding: 4px 6px; "
                       "border: 1px solid #4a3a00; border-radius: 4px; "
                       "background: #2a2110;"));

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"), nameEdit_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(hintLabel_);
    root->addWidget(errorLabel_);
    root->addSpacing(6);
    root->addWidget(checkoutBox_);
    root->addWidget(buttons_);

    connect(nameEdit_, &QLineEdit::textChanged,
            this, &CreateBranchDialog::onNameChanged);
    connect(buttons_, &QDialogButtonBox::accepted,
            this, &CreateBranchDialog::onAccept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onNameChanged(nameEdit_->text());
    nameEdit_->setFocus();
}

QString CreateBranchDialog::name() const          { return nameEdit_->text().trimmed(); }
bool    CreateBranchDialog::checkoutAfter() const { return checkoutBox_->isChecked(); }

bool CreateBranchDialog::isValidName(const QString& s, QString* whyNot) const
{
    if (!nameLooksValid(s, whyNot)) return false;
    if (existingNames_.contains(s)) {
        if (whyNot) *whyNot = tr("A branch named '%1' already exists.").arg(s);
        return false;
    }
    return true;
}

void CreateBranchDialog::onNameChanged(const QString& s)
{
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty()) {
        errorLabel_->setVisible(false);
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
        return;
    }
    QString why;
    const bool ok = isValidName(trimmed, &why);
    if (ok) {
        errorLabel_->setVisible(false);
    } else {
        errorLabel_->setText(why);
        errorLabel_->setVisible(true);
    }
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

void CreateBranchDialog::onAccept()
{
    if (isValidName(name(), nullptr)) accept();
}

} // namespace ghm::ui
