#pragma once

// StashSaveDialog - asks for a stash message + the two flag toggles
// that match `git stash` defaults.
//
// Kept deliberately small: one optional text field, two checkboxes.
// Most users just want to type a label and click Save.

#include <QDialog>
#include <QString>

class QLineEdit;
class QCheckBox;
class QDialogButtonBox;

namespace ghm::ui {

class StashSaveDialog : public QDialog {
    Q_OBJECT
public:
    explicit StashSaveDialog(QWidget* parent = nullptr);

    QString message() const;
    bool    includeUntracked() const;
    bool    keepIndex() const;

private:
    QLineEdit* messageEdit_;
    QCheckBox* untrackedBox_;
    QCheckBox* keepIndexBox_;
};

} // namespace ghm::ui
