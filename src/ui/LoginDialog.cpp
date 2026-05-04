#include "ui/LoginDialog.h"

#include "github/GitHubClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>

namespace ghm::ui {

LoginDialog::LoginDialog(QWidget* parent)
    : QDialog(parent)
    , tokenEdit_(new QLineEdit(this))
    , signInBtn_(new QPushButton(tr("Sign in"), this))
    , cancelBtn_(new QPushButton(tr("Cancel"), this))
    , statusLabel_(new QLabel(this))
    , spinner_(new QProgressBar(this))
    , client_(new ghm::github::GitHubClient(this))
{
    setWindowTitle(tr("Sign in to GitHub"));
    setModal(true);
    setMinimumWidth(420);

    auto* heading = new QLabel(tr("Personal Access Token"), this);
    auto* hint    = new QLabel(
        tr("Generate a token at <a href=\"https://github.com/settings/tokens\">"
           "github.com/settings/tokens</a> with the <b>repo</b> scope."), this);
    hint->setOpenExternalLinks(true);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::RichText);

    tokenEdit_->setEchoMode(QLineEdit::Password);
    tokenEdit_->setPlaceholderText(tr("ghp_… or github_pat_…"));
    tokenEdit_->setClearButtonEnabled(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Token:"), tokenEdit_);

    spinner_->setRange(0, 0);     // indeterminate
    spinner_->setVisible(false);
    spinner_->setMaximumHeight(8);

    statusLabel_->setVisible(false);
    statusLabel_->setStyleSheet(QStringLiteral("color: #ff8a8a;"));
    statusLabel_->setWordWrap(true);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(cancelBtn_);
    buttons->addWidget(signInBtn_);
    signInBtn_->setDefault(true);

    auto* root = new QVBoxLayout(this);
    root->addWidget(heading);
    root->addWidget(hint);
    root->addLayout(form);
    root->addWidget(spinner_);
    root->addWidget(statusLabel_);
    root->addStretch();
    root->addLayout(buttons);

    connect(signInBtn_, &QPushButton::clicked, this, &LoginDialog::onSignIn);
    connect(cancelBtn_, &QPushButton::clicked, this, &QDialog::reject);
    connect(tokenEdit_, &QLineEdit::returnPressed, this, &LoginDialog::onSignIn);

    connect(client_, &ghm::github::GitHubClient::authenticated,
            this, &LoginDialog::onAuthenticated);
    connect(client_, &ghm::github::GitHubClient::authenticationFailed,
            this, &LoginDialog::onAuthFailed);
}

LoginDialog::~LoginDialog() = default;

void LoginDialog::setBusy(bool busy)
{
    spinner_->setVisible(busy);
    statusLabel_->setVisible(!busy && !statusLabel_->text().isEmpty());
    signInBtn_->setEnabled(!busy);
    cancelBtn_->setEnabled(!busy);
    tokenEdit_->setEnabled(!busy);
}

void LoginDialog::onSignIn()
{
    const QString tok = tokenEdit_->text().trimmed();
    if (tok.isEmpty()) {
        statusLabel_->setText(tr("Please enter a token."));
        statusLabel_->setVisible(true);
        return;
    }
    statusLabel_->clear();
    statusLabel_->setVisible(false);
    token_ = tok;
    client_->setToken(tok);
    setBusy(true);
    client_->validateToken();
}

void LoginDialog::onAuthenticated(const QString& login)
{
    verifiedUsername_ = login;
    setBusy(false);
    accept();
}

void LoginDialog::onAuthFailed(const QString& reason)
{
    token_.clear();
    setBusy(false);
    statusLabel_->setText(tr("Sign-in failed: %1").arg(reason));
    statusLabel_->setVisible(true);
}

} // namespace ghm::ui
