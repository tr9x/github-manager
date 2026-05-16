#pragma once

// RememberedKeysDialog — table of (parent-repo, submodule, keyPath)
// triples that the user has saved via the per-submodule explicit-key
// flow (0.32.0).
//
// Read-mostly, like TrustedServersDialog. Each row is removable;
// closing the dialog doesn't roll back changes. The destructive
// impact of removal is "next explicit-key submodule operation
// re-prompts for a key" which is recoverable.

#include <QDialog>
#include <QString>

class QTableWidget;
class QPushButton;
class QLabel;

namespace ghm::core { class Settings; }

namespace ghm::ui {

class RememberedKeysDialog : public QDialog {
    Q_OBJECT
public:
    explicit RememberedKeysDialog(ghm::core::Settings& settings,
                                    QWidget* parent = nullptr);

private Q_SLOTS:
    void onRemoveClicked();
    void onSelectionChanged();

private:
    void reloadTable();

    ghm::core::Settings&  settings_;
    QTableWidget*         table_{nullptr};
    QPushButton*          removeBtn_{nullptr};
    QPushButton*          closeBtn_{nullptr};
    QLabel*               emptyHint_{nullptr};
};

} // namespace ghm::ui
