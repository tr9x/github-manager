#pragma once

// CloneDialog - asks the user where to clone a selected GitHub repo.
//
// This is presentation-only. The actual clone runs from MainWindow on
// accept() so we can stream progress into the main status bar instead
// of a transient modal.

#include <QDialog>
#include <QString>

#include "github/Repository.h"

class QLineEdit;
class QLabel;
class QPushButton;

namespace ghm::ui {

class CloneDialog : public QDialog {
    Q_OBJECT
public:
    CloneDialog(const ghm::github::Repository& repo,
                const QString& defaultParentDir,
                QWidget* parent = nullptr);
    ~CloneDialog() override;

    QString targetPath() const;

private Q_SLOTS:
    void onBrowse();
    void onPathChanged(const QString& s);

private:
    ghm::github::Repository repo_;

    QLineEdit*   parentDirEdit_;
    QLineEdit*   folderNameEdit_;
    QLabel*      finalPathLabel_;
    QPushButton* okBtn_;
    QPushButton* cancelBtn_;
};

} // namespace ghm::ui
