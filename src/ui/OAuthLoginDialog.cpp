#include "ui/OAuthLoginDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QClipboard>
#include <QGuiApplication>
#include <QDesktopServices>
#include <QUrl>

namespace ghm::ui {

OAuthLoginDialog::OAuthLoginDialog(QWidget* parent)
    : QDialog(parent)
    , codeLabel_  (new QLabel(this))
    , statusLabel_(new QLabel(this))
    , copyBtn_    (new QPushButton(tr("Copy code"), this))
    , openBtn_    (new QPushButton(tr("Open GitHub in browser"), this))
    , cancelBtn_  (new QPushButton(tr("Cancel"), this))
{
    setWindowTitle(tr("Sign in with GitHub"));
    setModal(true);
    setMinimumWidth(520);

    // Code label is intentionally large and monospace — the user is
    // going to read this and type it. Don't let it wrap or shrink.
    codeLabel_->setText(QStringLiteral("…"));
    codeLabel_->setAlignment(Qt::AlignCenter);
    codeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    codeLabel_->setStyleSheet(QStringLiteral(
        "QLabel { font-family: monospace; font-size: 28px; "
        "font-weight: bold; letter-spacing: 4px; "
        "padding: 12px 16px; background: #1f2228; "
        "border-radius: 6px; color: #e2e6ee; }"));

    auto* instructions = new QLabel(tr(
        "<p>To sign in to GitHub:</p>"
        "<ol>"
        "<li>Click <b>Open GitHub in browser</b> below (or visit "
        "<code>https://github.com/login/device</code> manually).</li>"
        "<li>Enter this code when prompted:</li>"
        "</ol>"), this);
    instructions->setWordWrap(true);
    instructions->setTextFormat(Qt::RichText);

    auto* trailer = new QLabel(tr(
        "<p>After you authorize the app, this window will close "
        "automatically. You can cancel at any time.</p>"), this);
    trailer->setWordWrap(true);
    trailer->setTextFormat(Qt::RichText);

    statusLabel_->setStyleSheet(QStringLiteral("color: #8aa3c2;"));
    statusLabel_->setWordWrap(true);

    // Start with the open/copy buttons disabled — they become useful
    // only once setUserCode() has populated the dialog.
    copyBtn_->setEnabled(false);
    openBtn_->setEnabled(false);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(copyBtn_);
    buttons->addWidget(openBtn_);
    buttons->addStretch();
    buttons->addWidget(cancelBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(instructions);
    root->addWidget(codeLabel_);
    root->addWidget(trailer);
    root->addWidget(statusLabel_);
    root->addStretch();
    root->addLayout(buttons);

    connect(copyBtn_,   &QPushButton::clicked, this, &OAuthLoginDialog::onCopyCode);
    connect(openBtn_,   &QPushButton::clicked, this, &OAuthLoginDialog::onOpenBrowser);
    connect(cancelBtn_, &QPushButton::clicked, this, [this] {
        Q_EMIT cancelled();
        reject();
    });
    // Catch window-manager close as cancel too.
    setAttribute(Qt::WA_DeleteOnClose, false);
}

void OAuthLoginDialog::setUserCode(const QString& code,
                                    const QString& verificationUri)
{
    codeLabel_->setText(code);
    verificationUri_ = verificationUri;
    copyBtn_->setEnabled(!code.isEmpty());
    openBtn_->setEnabled(!verificationUri.isEmpty());

    // Auto-open the browser on first arrival. We don't wait for the
    // user to click — that just adds friction. If their browser is
    // misconfigured the Open button still gives them a manual escape.
    if (!verificationUri.isEmpty()) {
        QDesktopServices::openUrl(QUrl(verificationUri));
    }
}

void OAuthLoginDialog::setStatus(const QString& message)
{
    statusLabel_->setText(message);
}

void OAuthLoginDialog::onCopyCode()
{
    QGuiApplication::clipboard()->setText(codeLabel_->text());
    setStatus(tr("Code copied to clipboard."));
}

void OAuthLoginDialog::onOpenBrowser()
{
    if (verificationUri_.isEmpty()) return;
    QDesktopServices::openUrl(QUrl(verificationUri_));
}

} // namespace ghm::ui
