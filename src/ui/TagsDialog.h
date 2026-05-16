#pragma once

// TagsDialog - manage tags in one place.
//
// Top half: list of existing tags with name + annotation indicator.
// Bottom: form for creating a new tag (name + optional message;
// non-empty message ⇒ annotated tag).
// Delete button next to the list acts on the current selection.

#include <QDialog>
#include <QString>
#include <vector>

#include "git/GitHandler.h"

class QListWidget;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QLabel;

namespace ghm::ui {

class TagsDialog : public QDialog {
    Q_OBJECT
public:
    explicit TagsDialog(QWidget* parent = nullptr);

    void setTags(const std::vector<ghm::git::TagInfo>& tags);
    void setBusy(bool busy);

    // Hint shown above the create form. Used by the host to warn when
    // the author identity isn't configured (annotated tags need it).
    void setIdentityWarning(const QString& warning);

Q_SIGNALS:
    // name + message; empty message ⇒ lightweight tag.
    void createRequested(const QString& name, const QString& message);
    void deleteRequested(const QString& name);

private Q_SLOTS:
    void onSelectionChanged();
    void onCreateClicked();
    void onDeleteClicked();
    void onNameChanged(const QString& s);

private:
    QListWidget*    list_;
    QLineEdit*      nameEdit_;
    QPlainTextEdit* messageEdit_;
    QPushButton*    createBtn_;
    QPushButton*    deleteBtn_;
    QPushButton*    closeBtn_;
    QLabel*         identityWarning_;
    QLabel*         nameError_;

    std::vector<ghm::git::TagInfo> currentTags_;
};

} // namespace ghm::ui
