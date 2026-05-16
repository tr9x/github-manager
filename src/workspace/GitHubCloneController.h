#pragma once

// GitHubCloneController — orchestrates the "clone from GitHub" and
// "open existing local copy" flows for repos in the sidebar.
//
// Two entry points:
//
//   startClone(repo, targetPath, token)
//       Validates that targetPath doesn't already exist, kicks off
//       worker_.clone(), and tracks the in-flight operation. On
//       success emits cloned() so the host can update the sidebar
//       and detail view; on failure emits failed() so the host
//       can show a dialog and roll back any optimistic UI changes.
//
//   openExisting(repo, localPath)
//       Synchronous — verifies that the directory contains a .git
//       folder and emits opened() (or failed() if not a git dir).
//       Since there's nothing async here, this is mostly a place to
//       keep the validation logic alongside the cloned-flow logic.
//
// The controller is intentionally stateless beyond knowing whether a
// clone is currently in flight (isBusy()) and which fullName is being
// cloned (so the worker callback can be filtered against it). The
// fullName → localPath map lives in MainWindow because the sidebar
// reads it too — passing through here would just be indirection.

#include <QObject>
#include <QString>

#include "github/Repository.h"
// Full include: startClone() takes a defaulted SshCredentials
// parameter, which needs the complete type for the implicit
// default-construct to work. Forward-declaring `class GitWorker`
// no longer suffices since the controller's public API surface
// now exposes a git/ type.
#include "git/GitHandler.h"

namespace ghm::git { class GitWorker; }

namespace ghm::workspace {

class GitHubCloneController : public QObject {
    Q_OBJECT
public:
    GitHubCloneController(ghm::git::GitWorker& worker,
                          QObject* parent = nullptr);

    // True between startClone() and the corresponding cloned()/failed().
    bool isBusy() const { return !inFlightFullName_.isEmpty(); }

    QString inFlightFullName() const { return inFlightFullName_; }
    QString inFlightLocalPath() const { return inFlightLocalPath_; }

    // Begin a clone. The host has already shown the CloneDialog and
    // resolved targetPath; we verify it doesn't already exist, then
    // dispatch to the worker. Returns false (and emits nothing) if
    // another clone is already in flight or the target exists.
    //
    // `repo.cloneUrl` is the HTTPS URL we'll feed to libgit2; `token`
    // is the GitHub PAT used as the password via the cred callback.
    // For public repos pass empty token.
    //
    // `sshCreds` is forwarded to the worker for SSH URLs. When the
    // URL is HTTPS or the creds are empty, the worker falls back to
    // its usual auth path (PAT for HTTPS, ssh-agent for SSH).
    bool startClone(const ghm::github::Repository& repo,
                    const QString& targetPath,
                    const QString& token,
                    const ghm::git::SshCredentials& sshCreds = {});

    // Map an existing local folder to a GitHub repo. Synchronous —
    // we just validate the path contains .git and emit opened() if
    // so. Useful when the user already has a clone made elsewhere
    // and just wants the manager to know about it.
    void openExisting(const ghm::github::Repository& repo,
                      const QString& localPath);

Q_SIGNALS:
    // Fired right after startClone() accepts. Host typically flashes
    // "Cloning <repo>…" in the status bar; controller manages the
    // worker's actual progress via the existing transfer-progress
    // signal it doesn't need to re-emit.
    void cloneStarted(const ghm::github::Repository& repo,
                      const QString& localPath);

    // Clone succeeded. Host updates sidebar badge (repo now has a
    // localPath) and switches the detail view to point at the new
    // folder.
    void cloned(const ghm::github::Repository& repo,
                const QString& localPath);

    // Open-existing succeeded — no async wait, fires immediately
    // from openExisting() when validation passes.
    void opened(const ghm::github::Repository& repo,
                const QString& localPath);

    // Any error in clone or open. Title is already translated; host
    // shows a QMessageBox with the detail text.
    void failed(const ghm::github::Repository& repo,
                const QString& localPath,
                const QString& title,
                const QString& detail);

    // After a successful clone, the host's defaultCloneDirectory
    // setting should be updated to the parent of the new clone so
    // the next CloneDialog opens with a sensible default. Settings
    // are owned by MainWindow, so we just emit and let it persist.
    void defaultCloneDirectoryChanged(const QString& newDirectory);

private Q_SLOTS:
    void onCloneFinished(bool ok, const QString& localPath, const QString& error);

private:
    ghm::git::GitWorker&    worker_;

    // Track which repo is being cloned so the worker callback (which
    // only knows localPath) can be matched back to a full Repository.
    // Cleared when clone completes (success or failure).
    QString                 inFlightFullName_;
    QString                 inFlightLocalPath_;
    ghm::github::Repository inFlightRepo_;
};

} // namespace ghm::workspace
