#pragma once

// SupportDialog - "Wesprzyj rozwój" / "Support development".
//
// Shows author info (with the brand-styled red brackets in
// "Z3r[0x30]"), a thank-you message, and bank-transfer details with
// per-row Copy buttons so users can quickly grab the account number
// or transfer title to paste into their bank's app.

#include <QDialog>

class QLabel;
class QPushButton;

namespace ghm::ui {

class SupportDialog : public QDialog {
    Q_OBJECT
public:
    explicit SupportDialog(QWidget* parent = nullptr);

private:
    QWidget* makeAuthorBlock(QWidget* parent);
    QWidget* makeMessageBlock(QWidget* parent);
    QWidget* makeBankBlock(QWidget* parent);
    QWidget* makeCopyableRow(const QString& label,
                             const QString& value,
                             bool monospace,
                             QWidget* parent);
};

} // namespace ghm::ui
