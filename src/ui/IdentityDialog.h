#pragma once

// IdentityDialog - asks for the user's git author identity (name +
// email). Shown lazily before the first commit if the values aren't
// already in Settings, and on demand when the user clicks the identity
// label in LocalRepositoryWidget.

#include <QDialog>
#include <QString>

class QLineEdit;
class QDialogButtonBox;

namespace ghm::ui {

class IdentityDialog : public QDialog {
    Q_OBJECT
public:
    IdentityDialog(const QString& currentName,
                   const QString& currentEmail,
                   QWidget* parent = nullptr);

    QString name()  const;
    QString email() const;

private Q_SLOTS:
    void validate();

private:
    QLineEdit* nameEdit_;
    QLineEdit* emailEdit_;
    QDialogButtonBox* buttons_;
};

} // namespace ghm::ui
