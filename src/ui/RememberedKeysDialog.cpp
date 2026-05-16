#include "ui/RememberedKeysDialog.h"
#include "core/Settings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QFileInfo>
#include <QFile>

namespace ghm::ui {

RememberedKeysDialog::RememberedKeysDialog(ghm::core::Settings& settings,
                                             QWidget* parent)
    : QDialog(parent)
    , settings_(settings)
{
    setWindowTitle(tr("Remembered submodule SSH keys"));
    setModal(true);
    setMinimumSize(800, 400);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 16, 20, 16);
    root->setSpacing(10);

    auto* heading = new QLabel(
        tr("Per-submodule SSH key mappings saved when you used "
           "\"Init / Update with explicit SSH key…\" on a submodule. "
           "Each entry is reused for future operations on that "
           "submodule without re-prompting (passphrases are still "
           "asked each time — they're never persisted).<br><br>"
           "Remove an entry to force a fresh key picker next time. "
           "If the key file marked <i>missing</i> below was moved "
           "or deleted, you'll be re-prompted on next operation "
           "and the new pick replaces the stale entry."), this);
    heading->setWordWrap(true);
    heading->setTextFormat(Qt::RichText);
    root->addWidget(heading);

    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({
        tr("Parent repository"),
        tr("Submodule"),
        tr("SSH key path"),
    });
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setColumnWidth(0, 220);
    table_->setColumnWidth(1, 180);
    root->addWidget(table_, 1);

    emptyHint_ = new QLabel(
        tr("<i>No remembered keys yet. Choices made via the submodule "
           "context menu \"Init/Update with explicit SSH key…\" will "
           "appear here.</i>"), this);
    emptyHint_->setAlignment(Qt::AlignCenter);
    emptyHint_->setVisible(false);
    root->addWidget(emptyHint_);

    auto* btnRow = new QHBoxLayout;
    removeBtn_ = new QPushButton(tr("Remove selected"), this);
    removeBtn_->setEnabled(false);
    removeBtn_->setToolTip(tr(
        "Forget this (submodule → key file) mapping. Next "
        "explicit-key operation on this submodule will re-prompt "
        "for a key."));
    closeBtn_ = new QPushButton(tr("Close"), this);
    closeBtn_->setDefault(true);
    btnRow->addWidget(removeBtn_);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn_);
    root->addLayout(btnRow);

    connect(removeBtn_, &QPushButton::clicked,
            this, &RememberedKeysDialog::onRemoveClicked);
    connect(closeBtn_, &QPushButton::clicked,
            this, &QDialog::accept);
    connect(table_, &QTableWidget::itemSelectionChanged,
            this, &RememberedKeysDialog::onSelectionChanged);

    reloadTable();
}

void RememberedKeysDialog::reloadTable()
{
    const auto entries = settings_.allRememberedSubmoduleKeys();

    table_->setRowCount(0);
    table_->setRowCount(entries.size());

    for (int row = 0; row < entries.size(); ++row) {
        const auto& e = entries[row];

        auto* parentItem    = new QTableWidgetItem(e.parentPath);
        auto* submoduleItem = new QTableWidgetItem(e.submoduleName);
        QString keyDisplay  = e.keyPath;
        // Annotate missing keys so the user can spot stale entries.
        if (!QFile::exists(e.keyPath)) {
            keyDisplay = tr("%1 (missing)").arg(e.keyPath);
            // Italic / muted via item flags. We could set a
            // QColor::red here but italic is less alarming and
            // matches the "stale" rather than "broken" feel.
            QFont f = parentItem->font();
            f.setItalic(true);
            submoduleItem->setFont(f);
            parentItem->setFont(f);
        }
        auto* keyItem = new QTableWidgetItem(keyDisplay);

        // Stash original strings on the parent item for removeClicked.
        parentItem->setData(Qt::UserRole, e.parentPath);
        parentItem->setData(Qt::UserRole + 1, e.submoduleName);

        // Tooltips with the unabbreviated full strings.
        parentItem->setToolTip(e.parentPath);
        submoduleItem->setToolTip(e.submoduleName);
        keyItem->setToolTip(e.keyPath);

        table_->setItem(row, 0, parentItem);
        table_->setItem(row, 1, submoduleItem);
        table_->setItem(row, 2, keyItem);
    }

    const bool empty = entries.isEmpty();
    table_->setVisible(!empty);
    emptyHint_->setVisible(empty);

    removeBtn_->setEnabled(false);
}

void RememberedKeysDialog::onSelectionChanged()
{
    removeBtn_->setEnabled(!table_->selectionModel()->selectedRows().isEmpty());
}

void RememberedKeysDialog::onRemoveClicked()
{
    const auto rows = table_->selectionModel()->selectedRows();
    if (rows.isEmpty()) return;
    const int row = rows.first().row();
    auto* parentItem = table_->item(row, 0);
    if (!parentItem) return;
    const QString parentPath    = parentItem->data(Qt::UserRole).toString();
    const QString submoduleName = parentItem->data(Qt::UserRole + 1).toString();
    if (parentPath.isEmpty() || submoduleName.isEmpty()) return;

    QMessageBox confirm(this);
    confirm.setWindowTitle(tr("Forget remembered key?"));
    confirm.setIcon(QMessageBox::Question);
    confirm.setText(tr("Forget the SSH key for submodule "
                       "<b>%1</b> in <b>%2</b>?")
                      .arg(submoduleName, parentPath));
    confirm.setInformativeText(tr(
        "The next time you run \"Init/Update with explicit SSH key\" "
        "on this submodule, you'll be asked to pick a key file again. "
        "The key file itself isn't touched — only the saved mapping."));
    confirm.setStandardButtons(QMessageBox::Cancel | QMessageBox::Yes);
    confirm.setDefaultButton(QMessageBox::Cancel);
    confirm.button(QMessageBox::Yes)->setText(tr("Forget"));
    if (confirm.exec() != QMessageBox::Yes) return;

    settings_.clearRememberedSubmoduleKey(parentPath, submoduleName);
    reloadTable();
}

} // namespace ghm::ui
