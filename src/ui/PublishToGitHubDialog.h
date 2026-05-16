#pragma once

// PublishToGitHubDialog - one-stop "wire this folder up to GitHub" UI.
//
// Two modes the user picks between:
//   1. Create a new GitHub repository under their account (POSTs to
//      /user/repos under the hood). The dialog only collects the inputs;
//      the actual API call is fired by MainWindow once accept() returns.
//   2. Link to one of the user's existing repositories (typically a
//      freshly-created empty repo on github.com). No API call needed —
//      we already have the repo metadata (cloneUrl, fullName, ...) in
//      memory from the sidebar listing.
//
// Either way, the result is one of:
//   * mode() == CreateNew  -> name(), description(), isPrivate()  must be POST'd
//   * mode() == LinkExisting -> existingRepo() points at the chosen repo
//
// pushAfterPublish() asks whether to do the equivalent of `git push -u
// origin master` once the remote has been wired.

#include <QDialog>
#include <QString>
#include <QList>

#include "github/Repository.h"

class QLineEdit;
class QPlainTextEdit;
class QRadioButton;
class QStackedWidget;
class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QComboBox;
class QLabel;
class QDialogButtonBox;

namespace ghm::ui {

class PublishToGitHubDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { CreateNew, LinkExisting };

    PublishToGitHubDialog(const QString&                              folderName,
                          const QString&                              suggestedRepoName,
                          const QString&                              accountLogin,
                          const QList<ghm::github::Repository>&       knownRepos,
                          QWidget*                                    parent = nullptr);

    Mode mode() const { return mode_; }

    // Mode::CreateNew accessors
    QString name()        const;
    QString description() const;
    bool    isPrivate()   const;

    // License / gitignore templates from the combobox selections.
    // Returns the GitHub "key" (e.g. "mit", "apache-2.0", "Python"),
    // not the human-readable name. Empty when user picked "(none)".
    QString licenseTemplate()   const;
    QString gitignoreTemplate() const;

    // Mode::LinkExisting accessor
    ghm::github::Repository existingRepo() const;

    // Common
    bool    pushAfterPublish() const;

private Q_SLOTS:
    void onModeChanged();
    void onAccept();
    void onSearchChanged(const QString& text);
    void onExistingSelectionChanged();

private:
    void rebuildExistingList();
    void updateOkState();
    bool isValidGithubName(const QString& s) const;

    Mode mode_{Mode::CreateNew};

    QString accountLogin_;
    QList<ghm::github::Repository> knownRepos_;

    // Header
    QLabel* headerLabel_;

    // Mode picker
    QRadioButton* createRadio_;
    QRadioButton* linkRadio_;
    QStackedWidget* pages_;

    // Create-new page
    QLineEdit*      nameEdit_;
    QLineEdit*      descEdit_;
    QRadioButton*   publicRadio_;
    QRadioButton*   privateRadio_;
    QComboBox*      licenseCombo_{nullptr};
    QComboBox*      gitignoreCombo_{nullptr};
    QLabel*         createPreviewLabel_;
    QLabel*         createWarningLabel_;

    // Link-existing page
    QLineEdit*    searchEdit_;
    QListWidget*  existingList_;
    QLabel*       linkHintLabel_;

    // Common footer
    QCheckBox*    pushBox_;
    QDialogButtonBox* buttons_;
};

} // namespace ghm::ui
