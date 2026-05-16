#include "ui/TrustedServersDialog.h"
#include "core/Settings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QFont>
#include <QFontDatabase>
#include <QMessageBox>

namespace ghm::ui {

namespace {

// Format a SHA-256 hex string with colons between bytes, matching
// what TlsCertApprovalDialog shows. Keeps recognition simple — the
// user can compare what they see here to what they saw at approval
// time without mental reformatting.
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

TrustedServersDialog::TrustedServersDialog(ghm::core::Settings& settings,
                                             QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
{
    setWindowTitle(tr("Manage trusted TLS servers"));
    setModal(true);
    setMinimumSize(700, 400);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    auto* heading = new QLabel(
        tr("Servers whose TLS certificate you've explicitly approved "
           "via \"Accept and remember\". Future connections to a "
           "server with the same fingerprint proceed silently; a "
           "fingerprint change re-prompts.<br><br>"
           "Remove an entry to force a fresh approval prompt next "
           "time you connect to that server."), this);
    heading->setWordWrap(true);
    heading->setTextFormat(Qt::RichText);
    root->addWidget(heading);

    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({tr("Host"), tr("SHA-256 fingerprint")});
    table_->horizontalHeader()->setSectionResizeMode(0,
        QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Monospace for fingerprint readability — eyes can compare hex
    // pairs without a proportional font's letter-width variance.
    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    table_->setFont(monoFont);
    root->addWidget(table_, 1);

    // Empty-state hint, shown when there are no trusted servers.
    // Lives at the same layout position as the table, just visible
    // when the table is empty. Keeps the dialog from looking broken
    // for users who never approved anything.
    emptyHint_ = new QLabel(
        tr("<i>No servers trusted yet. Approvals from the TLS "
           "approval dialog will appear here.</i>"), this);
    emptyHint_->setAlignment(Qt::AlignCenter);
    emptyHint_->setVisible(false);
    root->addWidget(emptyHint_);

    auto* btnRow = new QHBoxLayout;
    removeBtn_ = new QPushButton(tr("Remove selected"), this);
    removeBtn_->setEnabled(false);  // enabled by selection change
    removeBtn_->setToolTip(tr(
        "Forget the fingerprint for this server. Next connection "
        "to it will re-prompt for approval."));
    closeBtn_ = new QPushButton(tr("Close"), this);
    closeBtn_->setDefault(true);
    btnRow->addWidget(removeBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);
    root->addLayout(btnRow);

    connect(removeBtn_, &QPushButton::clicked,
            this, &TrustedServersDialog::onRemoveClicked);
    connect(closeBtn_, &QPushButton::clicked,
            this, &QDialog::accept);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &TrustedServersDialog::onSelectionChanged);

    reloadTable();
}

void TrustedServersDialog::reloadTable()
{
    const auto entries = settings_.allTrustedTlsFingerprints();

    table_->setRowCount(0);
    table_->setRowCount(entries.size());

    for (int row = 0; row < entries.size(); ++row) {
        const auto& [host, fp] = entries[row];
        auto* hostItem = new QTableWidgetItem(host);
        auto* fpItem   = new QTableWidgetItem(colonize(fp));
        // Stash the un-colonized hex on the host item so onRemoveClicked
        // can use it directly (we sanitize host inside the settings
        // setter anyway, but stashing avoids re-deriving).
        hostItem->setData(Qt::UserRole, host);
        // Tooltip on the fingerprint cell carries the un-colonized
        // form for users who want to paste/diff against their own
        // records.
        fpItem->setToolTip(fp);
        table_->setItem(row, 0, hostItem);
        table_->setItem(row, 1, fpItem);
    }

    const bool empty = entries.isEmpty();
    table_->setVisible(!empty);
    emptyHint_->setVisible(empty);

    // Reset selection state after reload.
    removeBtn_->setEnabled(false);
}

void TrustedServersDialog::onSelectionChanged()
{
    removeBtn_->setEnabled(!table_->selectionModel()->selectedRows().isEmpty());
}

void TrustedServersDialog::onRemoveClicked()
{
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    const int row = rows.first().row();
    auto* hostItem = table_->item(row, 0);
    if (!hostItem) return;
    const QString host = hostItem->data(Qt::UserRole).toString();
    if (host.isEmpty()) return;

    // Confirm before removing — soft destructive (re-prompts next
    // connection is the only impact), but a button-row removal is
    // easy to fat-finger. Default button is Cancel.
    QMessageBox confirm(this);
    confirm.setWindowTitle(tr("Remove trusted server?"));
    confirm.setIcon(QMessageBox::Warning);
    confirm.setText(tr("Remove trust for <b>%1</b>?").arg(host));
    confirm.setInformativeText(tr(
        "Next time you connect to this server, you'll see the TLS "
        "approval dialog again. If the fingerprint hasn't changed, "
        "you can re-approve it. If it has changed, this might be "
        "the early warning of a server cert rotation OR a MITM "
        "attempt — investigate before re-approving."));
    confirm.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.button(QMessageBox::Yes)->setText(tr("Remove"));
    if (confirm.exec() != QMessageBox::Yes) return;

    settings_.clearTrustedTlsFingerprint(host);
    reloadTable();
}

} // namespace ghm::ui
