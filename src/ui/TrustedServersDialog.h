#pragma once

// TrustedServersDialog — table of HTTPS servers whose TLS
// fingerprints the user has approved via TlsCertApprovalDialog
// (0.28.0). Lets the user inspect what's trusted and revoke
// individual entries.
//
// Read-mostly UI: shows host, fingerprint (formatted with colons
// for human comparison), and a Remove button per row. There's no
// "Add manually" affordance — entries here come exclusively from
// the approval dialog flow. Adding by-hand would risk fingerprint
// typos that defeat the safety mechanism.
//
// Live: each Remove click hits Settings immediately; closing the
// dialog doesn't roll changes back. We could add an "undo" or
// confirm-before-close, but the destructive impact is low — the
// only effect is "next clone re-prompts" which is recoverable.

#include <QDialog>
#include <QString>

class QTableWidget;
class QPushButton;
class QLabel;

namespace ghm::core { class Settings; }

namespace ghm::ui {

class TrustedServersDialog : public QDialog {
    Q_OBJECT
public:
    explicit TrustedServersDialog(ghm::core::Settings& settings,
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
