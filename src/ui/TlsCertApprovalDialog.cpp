#include "ui/TlsCertApprovalDialog.h"

#include <QSslCertificate>
#include <QCryptographicHash>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QDateTime>

namespace ghm::ui {

TlsCertApprovalDialog::TlsCertApprovalDialog(const QString& host,
                                               const QByteArray& derBytes,
                                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("TLS certificate not trusted"));
    setModal(true);
    setMinimumWidth(560);

    parseCertificate(derBytes);
    buildUi(host);
}

void TlsCertApprovalDialog::parseCertificate(const QByteArray& der)
{
    // Compute fingerprints regardless of whether QSslCertificate
    // parses the DER — fingerprints are just hashes of the bytes.
    sha256Hex_ = QString::fromLatin1(
        QCryptographicHash::hash(der, QCryptographicHash::Sha256)
            .toHex().toLower());
    sha1Hex_   = QString::fromLatin1(
        QCryptographicHash::hash(der, QCryptographicHash::Sha1)
            .toHex().toLower());

    // Parse the cert for human-readable fields. Qt's QSslCertificate
    // handles DER directly. Failures here aren't fatal — we show
    // the raw fingerprints and let the user decide.
    QList<QSslCertificate> certs =
        QSslCertificate::fromData(der, QSsl::Der);
    if (certs.isEmpty()) {
        parseOk_    = false;
        parseError_ = tr("Couldn't parse the certificate bytes. "
                         "Decide based on the fingerprint alone.");
        return;
    }

    const QSslCertificate& c = certs.first();

    // Subject DN + CN. We surface CN (Common Name) prominently
    // because that's the hostname binding everyone recognises.
    QStringList subjParts;
    const auto subjectInfo = c.subjectInfo(QSslCertificate::CommonName);
    if (!subjectInfo.isEmpty()) {
        subjParts << tr("CN=%1").arg(subjectInfo.first());
    }
    const auto orgInfo = c.subjectInfo(QSslCertificate::Organization);
    if (!orgInfo.isEmpty()) {
        subjParts << tr("O=%1").arg(orgInfo.first());
    }
    subject_ = subjParts.isEmpty()
        ? tr("(no subject info)")
        : subjParts.join(QStringLiteral(", "));

    // Issuer
    QStringList issuerParts;
    const auto issuerCn = c.issuerInfo(QSslCertificate::CommonName);
    if (!issuerCn.isEmpty()) {
        issuerParts << tr("CN=%1").arg(issuerCn.first());
    }
    const auto issuerOrg = c.issuerInfo(QSslCertificate::Organization);
    if (!issuerOrg.isEmpty()) {
        issuerParts << tr("O=%1").arg(issuerOrg.first());
    }
    issuer_ = issuerParts.isEmpty()
        ? tr("(no issuer info)")
        : issuerParts.join(QStringLiteral(", "));

    validFrom_  = c.effectiveDate().toLocalTime()
                    .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    validUntil_ = c.expiryDate().toLocalTime()
                    .toString(QStringLiteral("yyyy-MM-dd HH:mm"));

    parseOk_ = true;
}

namespace {
// Format a SHA-256 hex string with colons between bytes, the way
// every certificate viewer on Earth does it. "a1b2c3..." → "a1:b2:c3:...".
QString colonize(const QString& hex)
{
    QString out;
    out.reserve(hex.length() + hex.length() / 2);
    for (int i = 0; i < hex.length(); i += 2) {
        if (!out.isEmpty()) out += QLatin1Char(':');
        out += hex.mid(i, 2);
    }
    return out;
}
} // anonymous namespace

void TlsCertApprovalDialog::buildUi(const QString& host)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(12);

    // Heading: warning icon + headline.
    auto* heading = new QLabel(
        tr("<b>The TLS certificate for <code>%1</code> isn't trusted "
           "by your system.</b><br><br>"
           "This usually means one of:<br>"
           "&nbsp;&nbsp;• the server uses a self-signed certificate<br>"
           "&nbsp;&nbsp;• the certificate was issued by an internal CA "
           "(common for GitHub Enterprise)<br>"
           "&nbsp;&nbsp;• someone may be intercepting your connection<br><br>"
           "Verify the fingerprint below matches what your server "
           "administrator told you. If it doesn't, <b>reject</b> — "
           "don't proceed.").arg(host), this);
    heading->setWordWrap(true);
    heading->setTextFormat(Qt::RichText);
    root->addWidget(heading);

    // Separator
    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    root->addWidget(line);

    // Cert details grid.
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(6);
    int row = 0;
    auto addRow = [&](const QString& label, const QString& value,
                      bool monospace = false) {
        auto* l = new QLabel(QStringLiteral("<b>%1</b>").arg(label), this);
        auto* v = new QLabel(value, this);
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        v->setWordWrap(true);
        if (monospace) {
            QFont f = v->font();
            f.setFamily(QStringLiteral("monospace"));
            f.setStyleHint(QFont::Monospace);
            v->setFont(f);
        }
        grid->addWidget(l, row, 0, Qt::AlignTop | Qt::AlignLeft);
        grid->addWidget(v, row, 1);
        ++row;
    };

    if (parseOk_) {
        addRow(tr("Subject"),     subject_);
        addRow(tr("Issuer"),      issuer_);
        addRow(tr("Valid from"),  validFrom_);
        addRow(tr("Valid until"), validUntil_);
    } else {
        addRow(tr("Status"), parseError_);
    }
    addRow(tr("SHA-256"), colonize(sha256Hex_), /*monospace*/ true);
    addRow(tr("SHA-1"),   colonize(sha1Hex_),   /*monospace*/ true);

    root->addLayout(grid);

    root->addSpacing(8);

    // Buttons. Three outcomes, ordered safest-first (Reject is the
    // recommended choice when in doubt, so it goes left).
    auto* btnRow = new QHBoxLayout;
    auto* rejectBtn = new QPushButton(tr("Reject"), this);
    rejectBtn->setDefault(true);  // Enter = safest action
    auto* onceBtn   = new QPushButton(tr("Accept once"), this);
    auto* alwaysBtn = new QPushButton(tr("Accept and remember"), this);
    onceBtn->setToolTip(tr(
        "Proceed with this connection only. Next time you connect "
        "to %1 you'll be asked again.").arg(host));
    alwaysBtn->setToolTip(tr(
        "Proceed and remember this certificate's fingerprint. Future "
        "connections to %1 with the same fingerprint will proceed "
        "silently. If the fingerprint ever changes (could be a "
        "legitimate rotation OR a MITM attempt), you'll be asked again.")
        .arg(host));

    btnRow->addWidget(rejectBtn);
    btnRow->addStretch();
    btnRow->addWidget(onceBtn);
    btnRow->addWidget(alwaysBtn);
    root->addLayout(btnRow);

    connect(rejectBtn, &QPushButton::clicked, this, &TlsCertApprovalDialog::onReject);
    connect(onceBtn,   &QPushButton::clicked, this, &TlsCertApprovalDialog::onAcceptOnce);
    connect(alwaysBtn, &QPushButton::clicked, this, &TlsCertApprovalDialog::onAcceptAlways);
}

void TlsCertApprovalDialog::onReject()
{
    outcome_ = Reject;
    reject();  // QDialog::reject() — sets DialogCode::Rejected
}

void TlsCertApprovalDialog::onAcceptOnce()
{
    outcome_ = AcceptOnce;
    accept();
}

void TlsCertApprovalDialog::onAcceptAlways()
{
    outcome_ = AcceptAlways;
    accept();
}

} // namespace ghm::ui
