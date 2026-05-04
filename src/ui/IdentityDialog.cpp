#include "ui/IdentityDialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QRegularExpression>
#include <QPushButton>

namespace ghm::ui {

namespace {
// Cheap email sanity check — doesn't try to be RFC-correct, just
// rejects obviously wrong values like "foo" or "@bar".
bool looksLikeEmail(const QString& s) {
    static const QRegularExpression re(QStringLiteral(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)"));
    return re.match(s).hasMatch();
}
}

IdentityDialog::IdentityDialog(const QString& currentName,
                               const QString& currentEmail,
                               QWidget* parent)
    : QDialog(parent)
    , nameEdit_(new QLineEdit(this))
    , emailEdit_(new QLineEdit(this))
    , buttons_(new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this))
{
    setWindowTitle(tr("Git author identity"));
    setModal(true);
    resize(420, 0);

    nameEdit_->setText(currentName);
    nameEdit_->setPlaceholderText(tr("Jan Kowalski"));

    emailEdit_->setText(currentEmail);
    emailEdit_->setPlaceholderText(tr("jan@example.com"));

    auto* hint = new QLabel(
        tr("These appear in every commit you create. They're stored only on this "
           "computer (Settings) and never sent to GitHub directly."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #9aa0a6;"));

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"),  nameEdit_);
    form->addRow(tr("Email:"), emailEdit_);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(hint);
    root->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::accepted, this, &IdentityDialog::validate);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto onText = [this] {
        const bool ok = !nameEdit_->text().trimmed().isEmpty()
                     &&  looksLikeEmail(emailEdit_->text().trimmed());
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(ok);
    };
    connect(nameEdit_,  &QLineEdit::textChanged, this, onText);
    connect(emailEdit_, &QLineEdit::textChanged, this, onText);
    onText();
}

QString IdentityDialog::name()  const { return nameEdit_->text().trimmed(); }
QString IdentityDialog::email() const { return emailEdit_->text().trimmed(); }

void IdentityDialog::validate()
{
    if (nameEdit_->text().trimmed().isEmpty() ||
        !looksLikeEmail(emailEdit_->text().trimmed())) {
        return;  // button shouldn't have been enabled
    }
    accept();
}

} // namespace ghm::ui
