#include "ui/TagsDialog.h"
#include "ui/BranchNameValidator.h"   // tag and branch refs share format rules

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFontDatabase>
#include <QMessageBox>
#include <QGroupBox>

namespace ghm::ui {

namespace {

constexpr int kTagNameRole = Qt::UserRole + 1;

// Tag names follow the same git-check-ref-format rules as branches.
// We lean on the existing branch validator rather than duplicating
// the logic; the only difference at the git layer is which directory
// holds the ref (refs/tags vs refs/heads), not the syntax.
bool isValidTagName(const QString& s, QString* whyNot)
{
    return isValidBranchName(s, whyNot);
}

} // namespace

TagsDialog::TagsDialog(QWidget* parent)
    : QDialog(parent)
    , list_           (new QListWidget(this))
    , nameEdit_       (new QLineEdit(this))
    , messageEdit_    (new QPlainTextEdit(this))
    , createBtn_      (new QPushButton(tr("Create"), this))
    , deleteBtn_      (new QPushButton(tr("Delete"), this))
    , closeBtn_       (new QPushButton(tr("Close"), this))
    , identityWarning_(new QLabel(this))
    , nameError_      (new QLabel(this))
{
    setWindowTitle(tr("Tags"));
    setModal(true);
    resize(560, 540);

    // ---- Top: list of existing tags --------------------------------
    list_->setAlternatingRowColors(true);
    list_->setUniformItemSizes(true);
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &TagsDialog::onSelectionChanged);
    deleteBtn_->setEnabled(false);
    connect(deleteBtn_, &QPushButton::clicked,
            this, &TagsDialog::onDeleteClicked);

    auto* listRow = new QHBoxLayout;
    listRow->addWidget(list_, 1);
    auto* sideCol = new QVBoxLayout;
    sideCol->addWidget(deleteBtn_);
    sideCol->addStretch();
    listRow->addLayout(sideCol);

    auto* listGroup = new QGroupBox(tr("Existing tags"), this);
    auto* listGroupCol = new QVBoxLayout(listGroup);
    listGroupCol->addLayout(listRow);

    // ---- Bottom: create form ---------------------------------------
    nameEdit_->setPlaceholderText(QStringLiteral("v1.2.3"));
    nameEdit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    connect(nameEdit_, &QLineEdit::textChanged,
            this, &TagsDialog::onNameChanged);

    messageEdit_->setPlaceholderText(
        tr("Optional. Leave empty for a lightweight tag, or write a "
           "release note for an annotated tag."));
    messageEdit_->setMaximumHeight(80);

    nameError_->setVisible(false);
    nameError_->setStyleSheet(
        QStringLiteral("color: #f0b400; padding: 4px 6px; "
                       "border: 1px solid #4a3a00; border-radius: 4px; "
                       "background: #2a2110;"));

    identityWarning_->setVisible(false);
    identityWarning_->setWordWrap(true);
    identityWarning_->setStyleSheet(
        QStringLiteral("color: #f0b400; padding: 4px 6px;"));

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"),    nameEdit_);
    form->addRow(tr("Message:"), messageEdit_);

    createBtn_->setEnabled(false);
    connect(createBtn_, &QPushButton::clicked,
            this, &TagsDialog::onCreateClicked);

    auto* formButtonRow = new QHBoxLayout;
    formButtonRow->addStretch();
    formButtonRow->addWidget(createBtn_);

    auto* createGroup = new QGroupBox(tr("Create tag at HEAD"), this);
    auto* createGroupCol = new QVBoxLayout(createGroup);
    createGroupCol->addLayout(form);
    createGroupCol->addWidget(nameError_);
    createGroupCol->addWidget(identityWarning_);
    createGroupCol->addLayout(formButtonRow);

    // ---- Footer ----------------------------------------------------
    closeBtn_->setDefault(true);
    connect(closeBtn_, &QPushButton::clicked, this, &QDialog::accept);
    auto* footer = new QHBoxLayout;
    footer->addStretch();
    footer->addWidget(closeBtn_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(listGroup, 1);
    root->addWidget(createGroup);
    root->addLayout(footer);
}

void TagsDialog::setTags(const std::vector<ghm::git::TagInfo>& tags)
{
    currentTags_ = tags;
    list_->clear();

    if (tags.empty()) {
        auto* item = new QListWidgetItem(
            tr("(no tags yet — create one below)"), list_);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        return;
    }

    for (const auto& t : tags) {
        QString text = t.name;
        if (t.isAnnotated) {
            text += QStringLiteral("    ★ annotated");
            if (!t.message.isEmpty()) {
                // Show the first message line as a preview.
                const QString firstLine = t.message.split(QLatin1Char('\n')).first();
                text += QStringLiteral("    %1").arg(firstLine);
            }
        }
        auto* item = new QListWidgetItem(text, list_);
        item->setData(kTagNameRole, t.name);
        item->setToolTip(t.targetSha);
    }
}

void TagsDialog::setBusy(bool busy)
{
    createBtn_->setEnabled(!busy && !nameEdit_->text().trimmed().isEmpty()
                                 && !nameError_->isVisible());
    deleteBtn_->setEnabled(!busy && list_->currentItem() != nullptr
                                 && (list_->currentItem()->flags() & Qt::ItemIsSelectable));
    list_->setEnabled(!busy);
    nameEdit_->setEnabled(!busy);
    messageEdit_->setEnabled(!busy);
}

void TagsDialog::setIdentityWarning(const QString& warning)
{
    if (warning.isEmpty()) {
        identityWarning_->setVisible(false);
    } else {
        identityWarning_->setText(warning);
        identityWarning_->setVisible(true);
    }
}

void TagsDialog::onSelectionChanged()
{
    auto* it = list_->currentItem();
    const bool deletable = it && (it->flags() & Qt::ItemIsSelectable);
    deleteBtn_->setEnabled(deletable);
}

void TagsDialog::onNameChanged(const QString& s)
{
    const QString trimmed = s.trimmed();
    if (trimmed.isEmpty()) {
        nameError_->setVisible(false);
        createBtn_->setEnabled(false);
        return;
    }
    QString why;
    if (!isValidTagName(trimmed, &why)) {
        nameError_->setText(why);
        nameError_->setVisible(true);
        createBtn_->setEnabled(false);
        return;
    }
    // Collision with an existing tag.
    for (const auto& t : currentTags_) {
        if (t.name == trimmed) {
            nameError_->setText(tr("A tag named '%1' already exists.").arg(trimmed));
            nameError_->setVisible(true);
            createBtn_->setEnabled(false);
            return;
        }
    }
    nameError_->setVisible(false);
    createBtn_->setEnabled(true);
}

void TagsDialog::onCreateClicked()
{
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) return;
    Q_EMIT createRequested(name, messageEdit_->toPlainText().trimmed());
    // Don't clear the form yet — the host's tagOpFinished slot calls
    // setTags() with the refreshed list, and we leave fields filled
    // in case creation actually failed and the user wants to edit.
}

void TagsDialog::onDeleteClicked()
{
    auto* it = list_->currentItem();
    if (!it) return;
    const QString name = it->data(kTagNameRole).toString();
    if (name.isEmpty()) return;

    const auto reply = QMessageBox::question(this, tr("Delete tag"),
        tr("Delete the tag <b>%1</b>?<br><br>"
           "This is a local-only operation; tags pushed to a remote "
           "are not affected until you delete them remotely too.")
            .arg(name.toHtmlEscaped()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    Q_EMIT deleteRequested(name);
}

} // namespace ghm::ui
