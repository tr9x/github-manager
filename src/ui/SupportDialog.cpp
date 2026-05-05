#include "ui/SupportDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFrame>
#include <QGuiApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QTimer>

namespace ghm::ui {

namespace {

// Bank details — keep these as constants in one place so updating them
// later (e.g. adding IBAN/SWIFT) doesn't require touching layout code.
const QString kAuthorName     = QStringLiteral("Z3r[0x30]");
const QString kBankName       = QStringLiteral("ING Bank Śląski");
const QString kAccountNumber  = QStringLiteral("15 1050 1243 1000 0090 6374 2739");
const QString kTransferTitle  = QStringLiteral("githubmanager");
const QString kRecipient      = QStringLiteral("Krzysiek");

// Renders "Z3r[0x30]" with the square brackets coloured red. The
// non-bracket parts use the label's regular style.
QString authorRichText()
{
    constexpr const char* kRed = "#e83033";
    return QStringLiteral(
        "<span style='font-size: 18pt; font-weight: bold;'>"
        "Z3r"
        "<span style='color: %1;'>[</span>"
        "0x30"
        "<span style='color: %1;'>]</span>"
        "</span>"
    ).arg(QLatin1String(kRed));
}

QFrame* makeHRule(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    line->setStyleSheet(QStringLiteral("color: #3c4148;"));
    return line;
}

} // namespace

// ---------------------------------------------------------------------------

SupportDialog::SupportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Support GitHub Manager"));
    setModal(true);
    resize(560, 0);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(12);

    root->addWidget(makeAuthorBlock(this));
    root->addWidget(makeHRule(this));
    root->addWidget(makeMessageBlock(this));
    root->addWidget(makeHRule(this));
    root->addWidget(makeBankBlock(this));

    // Footer
    auto* buttons = new QDialogButtonBox(this);
    auto* closeBtn = buttons->addButton(tr("Close"), QDialogButtonBox::AcceptRole);
    closeBtn->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    root->addWidget(buttons);
}

// ----- Author block --------------------------------------------------------

QWidget* SupportDialog::makeAuthorBlock(QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);

    // Heart + title
    auto* title = new QLabel(QStringLiteral("❤  ") + tr("Support GitHub Manager"), w);
    {
        QFont f = title->font();
        f.setPointSizeF(f.pointSizeF() * 1.3);
        f.setBold(true);
        title->setFont(f);
    }

    auto* authorRow = new QLabel(w);
    authorRow->setTextFormat(Qt::RichText);
    authorRow->setText(QStringLiteral("<span style='color: #9aa0a6;'>%1:</span>&nbsp;%2")
                       .arg(tr("Author"), authorRichText()));
    authorRow->setTextInteractionFlags(Qt::TextSelectableByMouse);

    col->addWidget(title);
    col->addWidget(authorRow);
    return w;
}

// ----- Thank-you message ---------------------------------------------------

QWidget* SupportDialog::makeMessageBlock(QWidget* parent)
{
    auto* msg = new QLabel(parent);
    msg->setTextFormat(Qt::RichText);
    msg->setWordWrap(true);
    // Two paragraphs: a short hello, then the explanation/thank-you. Using
    // RichText so the HTML span keeping the brackets red lives inside the
    // first sentence.
    msg->setText(
        QStringLiteral("<p>%1</p><p>%2</p>")
            .arg(tr("Hi! I'm %1, the author of this app.").arg(authorRichText()),
                 tr("GitHub Manager is something I build in my spare time. Every donation helps "
                    "me keep the project alive — fixing bugs, adding features, and supporting "
                    "Linux as a first-class platform for developers. Thank you for any support — "
                    "it genuinely makes a difference. ❤")));
    msg->setStyleSheet(QStringLiteral("color: #d8dde2;"));
    return msg;
}

// ----- Bank transfer block -------------------------------------------------

QWidget* SupportDialog::makeBankBlock(QWidget* parent)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("bankCard"));
    card->setStyleSheet(QStringLiteral(
        "#bankCard { background: #232830; border: 1px solid #3c4148; "
        "border-radius: 6px; padding: 10px; }"));

    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(12, 10, 12, 10);
    col->setSpacing(6);

    auto* heading = new QLabel(tr("Bank transfer details"), card);
    {
        QFont f = heading->font();
        f.setBold(true);
        heading->setFont(f);
    }
    col->addWidget(heading);
    col->addSpacing(2);

    col->addWidget(makeCopyableRow(tr("Bank"),           kBankName,
                                   /*monospace*/ false, card));
    col->addWidget(makeCopyableRow(tr("Account number"), kAccountNumber,
                                   /*monospace*/ true,  card));
    col->addWidget(makeCopyableRow(tr("Title"),          kTransferTitle,
                                   /*monospace*/ true,  card));
    col->addWidget(makeCopyableRow(tr("Recipient"),      kRecipient,
                                   /*monospace*/ false, card));

    auto* note = new QLabel(
        tr("This account is in PLN. International transfers are also welcome — "
           "please contact me first for the IBAN/SWIFT format."), card);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: #9aa0a6; padding-top: 8px;"));
    col->addWidget(note);

    return card;
}

// ----- Reusable "Label   value [Copy]" row ---------------------------------

QWidget* SupportDialog::makeCopyableRow(const QString& label,
                                       const QString& value,
                                       bool           monospace,
                                       QWidget*       parent)
{
    auto* w = new QWidget(parent);
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    auto* labelLbl = new QLabel(label + QLatin1Char(':'), w);
    labelLbl->setStyleSheet(QStringLiteral("color: #9aa0a6;"));
    labelLbl->setMinimumWidth(120);
    labelLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* valueLbl = new QLabel(value, w);
    valueLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    if (monospace) {
        valueLbl->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    } else {
        QFont f = valueLbl->font();
        f.setBold(true);
        valueLbl->setFont(f);
    }

    auto* copyBtn = new QPushButton(tr("Copy"), w);
    copyBtn->setCursor(Qt::PointingHandCursor);
    copyBtn->setFixedWidth(80);
    connect(copyBtn, &QPushButton::clicked, this, [copyBtn, value, this] {
        QGuiApplication::clipboard()->setText(value);
        const QString original = tr("Copy");
        copyBtn->setText(tr("Copied!"));
        copyBtn->setEnabled(false);
        // Snap back after 1.2s so the affordance is reusable.
        QTimer::singleShot(1200, copyBtn, [copyBtn, original] {
            copyBtn->setText(original);
            copyBtn->setEnabled(true);
        });
    });

    row->addWidget(labelLbl);
    row->addWidget(valueLbl, 1);
    row->addWidget(copyBtn);
    return w;
}

} // namespace ghm::ui
