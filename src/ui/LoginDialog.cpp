#include "ui/LoginDialog.h"

#include "github/GitHubClient.h"
#include "session/OAuthFlowController.h"
#include "ui/OAuthLoginDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>

namespace ghm::ui {

LoginDialog::LoginDialog(QWidget* parent)
    : QDialog(parent)
    , tokenEdit_(new QLineEdit(this))
    , signInBtn_(new QPushButton(tr("Sign in with token"), this))
    , oauthBtn_ (new QPushButton(tr("Sign in with GitHub"), this))
    , cancelBtn_(new QPushButton(tr("Cancel"), this))
    , statusLabel_(new QLabel(this))
    , spinner_(new QProgressBar(this))
    , client_(new ghm::github::GitHubClient(this))
{
    setWindowTitle(tr("Sign in to GitHub"));
    setModal(true);
    setMinimumWidth(460);

    // ----- OAuth section ------------------------------------------------
    //
    // The OAuth button is the "easy path" — prominently positioned at
    // the top. It's enabled only when the build was configured with a
    // GHM_OAUTH_CLIENT_ID; without that, the GitHub server has nothing
    // to authenticate the device flow against. We always SHOW the
    // button so users know OAuth is the intended primary path, but
    // disable it with an explanatory tooltip when not configured.

    auto* oauthHeading = new QLabel(tr("<b>Recommended</b>"), this);
    oauthHeading->setTextFormat(Qt::RichText);

    auto* oauthHint = new QLabel(
        tr("A browser window will open. Authorize the app to grant "
           "access to your repositories."), this);
    oauthHint->setWordWrap(true);

    oauthBtn_->setStyleSheet(QStringLiteral(
        "QPushButton { background: #238636; color: white; "
        "padding: 8px 16px; font-weight: bold; border-radius: 6px; }"
        "QPushButton:hover { background: #2ea043; }"
        "QPushButton:disabled { background: #444; color: #888; }"));

#ifdef GHM_OAUTH_CLIENT_ID
    oauthBtn_->setEnabled(true);
#else
    // No client_id at build time → device flow won't work. Disable
    // the button and tell the user why so it doesn't look broken.
    oauthBtn_->setEnabled(false);
    oauthBtn_->setToolTip(tr(
        "OAuth sign-in is not configured for this build. "
        "Use a Personal Access Token below, or rebuild with "
        "-DGHM_OAUTH_CLIENT_ID=<id>."));
#endif

    // ----- Separator ----------------------------------------------------

    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    auto* orLabel = new QLabel(tr("or"), this);
    orLabel->setAlignment(Qt::AlignCenter);
    orLabel->setStyleSheet(QStringLiteral("color: #8aa3c2;"));

    // ----- PAT section --------------------------------------------------
    //
    // Kept for users who can't or won't OAuth — corporate environments
    // without browser, scripted setups, etc. Also the only path when
    // OAuth isn't configured at build time.

    auto* patHeading = new QLabel(tr("Personal Access Token"), this);
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
    root->addWidget(oauthHeading);
    root->addWidget(oauthHint);
    root->addWidget(oauthBtn_);
    root->addSpacing(8);
    root->addWidget(sep);
    root->addWidget(orLabel);
    root->addSpacing(8);
    root->addWidget(patHeading);
    root->addWidget(hint);
    root->addLayout(form);
    root->addWidget(spinner_);
    root->addWidget(statusLabel_);
    root->addStretch();
    root->addLayout(buttons);

    connect(oauthBtn_,  &QPushButton::clicked, this, &LoginDialog::onSignInWithOAuth);
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
#ifdef GHM_OAUTH_CLIENT_ID
    oauthBtn_->setEnabled(!busy);
#else
    oauthBtn_->setEnabled(false);  // permanently disabled
#endif
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
    token_         = tok;
    tokenIsOAuth_  = false;     // PAT path
    client_->setToken(tok);
    setBusy(true);
    client_->validateToken();
}

void LoginDialog::onSignInWithOAuth()
{
#ifndef GHM_OAUTH_CLIENT_ID
    // Defensive — the button should already be disabled, but in case
    // someone wires it up programmatically:
    statusLabel_->setText(tr("OAuth is not configured for this build."));
    statusLabel_->setVisible(true);
    return;
#else
    statusLabel_->clear();
    statusLabel_->setVisible(false);

    // Lazy-construct the controller and presentation dialog. We do
    // this on first click rather than at constructor time so the
    // PAT-only path (and the no-OAuth-build path) doesn't pay any
    // setup cost.
    if (!oauthCtrl_) {
        oauthCtrl_ = new ghm::session::OAuthFlowController(*client_, this);
        connect(oauthCtrl_, &ghm::session::OAuthFlowController::userCodeReady,
                this, &LoginDialog::onOAuthUserCodeReady);
        connect(oauthCtrl_, &ghm::session::OAuthFlowController::statusChanged,
                this, &LoginDialog::onOAuthStatusChanged);
        connect(oauthCtrl_, &ghm::session::OAuthFlowController::signedIn,
                this, &LoginDialog::onOAuthSignedIn);
        connect(oauthCtrl_, &ghm::session::OAuthFlowController::failed,
                this, &LoginDialog::onOAuthFailed);
    }
    if (!oauthDialog_) {
        oauthDialog_ = new OAuthLoginDialog(this);
        connect(oauthDialog_, &OAuthLoginDialog::cancelled,
                this, &LoginDialog::onOAuthCancelled);
    }

    setBusy(true);
    // Scope: "repo" gives clone/push/pull access (private + public);
    // "read:user" lets validateToken() resolve the login. This matches
    // what we already require of PATs.
    const bool started = oauthCtrl_->start(
        QStringLiteral(GHM_OAUTH_CLIENT_ID),
        QStringLiteral("repo read:user"));
    if (!started) {
        setBusy(false);
        return;  // controller already emitted failed() if applicable
    }
    // Show the OAuth dialog. We don't exec() it modally because the
    // controller's poll responses need to update it; instead we
    // show() it as a child modal and let signals drive updates.
    oauthDialog_->setStatus(tr("Requesting device code from GitHub…"));
    oauthDialog_->show();
#endif
}

void LoginDialog::onOAuthUserCodeReady(const QString& userCode,
                                        const QString& verificationUri)
{
    if (oauthDialog_) {
        oauthDialog_->setUserCode(userCode, verificationUri);
    }
}

void LoginDialog::onOAuthStatusChanged(const QString& message)
{
    if (oauthDialog_) {
        oauthDialog_->setStatus(message);
    }
}

void LoginDialog::onOAuthSignedIn(const QString& token, const QString& /*scope*/)
{
    // OAuth gave us a token. Now reuse the existing PAT-validation
    // path to confirm it works and resolve the username — that
    // emits authenticated() / authenticationFailed() the same way
    // as the PAT path, so we don't need a separate success handler.
    if (oauthDialog_) {
        oauthDialog_->hide();
    }
    token_         = token;
    tokenIsOAuth_  = true;       // distinguish from PAT for storage
    client_->setToken(token);
    client_->validateToken();
    // Spinner stays visible until onAuthenticated() fires.
}

void LoginDialog::onOAuthFailed(const QString& message)
{
    if (oauthDialog_) {
        oauthDialog_->hide();
    }
    setBusy(false);
    statusLabel_->setText(tr("Sign-in failed: %1").arg(message));
    statusLabel_->setVisible(true);
}

void LoginDialog::onOAuthCancelled()
{
    if (oauthCtrl_) {
        oauthCtrl_->cancel();
    }
    if (oauthDialog_) {
        oauthDialog_->hide();
    }
    setBusy(false);
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
    tokenIsOAuth_ = false;
    setBusy(false);
    statusLabel_->setText(tr("Sign-in failed: %1").arg(reason));
    statusLabel_->setVisible(true);
}

} // namespace ghm::ui
