#pragma once

// CreateBranchDialog - asks for the name of a new branch.
//
// Validates the name against the most common Git rules so we don't
// surface a cryptic libgit2 error after submit. The "checkout after
// creating" checkbox is on by default since creating-without-switching
// is the rarer case.

#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;
class QCheckBox;
class QLabel;
class QDialogButtonBox;

namespace ghm::ui {

class CreateBranchDialog : public QDialog {
    Q_OBJECT
public:
    // `existingNames` is used to collision-check before submit.
    // `currentBranch` is shown in the "branching from" hint.
    CreateBranchDialog(const QString&     currentBranch,
                       const QStringList& existingNames,
                       QWidget*           parent = nullptr);

    QString name() const;
    bool    checkoutAfter() const;

private Q_SLOTS:
    void onNameChanged(const QString& s);
    void onAccept();

private:
    bool isValidName(const QString& s, QString* whyNot) const;

    QStringList       existingNames_;

    QLineEdit*        nameEdit_;
    QCheckBox*        checkoutBox_;
    QLabel*           hintLabel_;
    QLabel*           errorLabel_;
    QDialogButtonBox* buttons_;
};

} // namespace ghm::ui
