#pragma once

// PublishController — coordinates the multi-step "Publish to GitHub"
// flow for a local Git folder.
//
// Steps (each can fail; controller handles transitions):
//   1. User picks a name (or selects existing repo) in a dialog
//      → MainWindow owns the dialog because it needs a QWidget
//      parent; the controller is told the user's choices via start().
//   2. (Create-new only) POST /user/repos via GitHubClient
//      → wait for repositoryCreated/networkError
//   3. addRemote("origin", cloneUrl) via GitWorker
//      → wait for remoteOpFinished
//   4. refreshLocalState (so the local-detail view sees the new remote
//      and we can read the current branch for the push)
//      → wait for localStateReady
//   5. (If pushAfter && commits exist) push origin <branch>
//      → wait for pushFinished
//
// The controller stores the in-progress state and filters worker/
// client callbacks against it. Side effects (dialogs, busy state,
// status bar) stay in MainWindow — controller emits high-level
// signals that the host translates to UI.
//
// Why this exists (vs leaving it inline in MainWindow):
//
//   * Self-contained state. Today's bug is that pendingPublish_
//     can get into an inconsistent state if the user kills the app
//     mid-flow; with the state hosted here we can offer reset()
//     and a watchdog timer.
//
//   * Hookable from tests. The pieces above (GitHubClient,
//     GitWorker) are already mockable in principle; once the
//     state machine is in its own class, we can drive it from
//     a test that doesn't need a QMainWindow.
//
//   * Easier to add features later — e.g. "push to a specific
//     branch other than HEAD" or "create org repos" — without
//     adding more if-statements to MainWindow.
//
// Things that stay in MainWindow:
//
//   * The PublishToGitHubDialog (needs a QWidget parent).
//   * All QMessageBox / status-bar updates (controller emits signals
//     that the host translates to UI feedback).
//   * The reposCache_ / sidebar updates (those are MainWindow's
//     responsibility — controller only tells it "publish succeeded
//     for repo X").

#include <QObject>
#include <QString>
#include <QTimer>

#include "github/Repository.h"
// Full include: PublishController's slot signatures use
// ghm::git::StatusEntry and ghm::git::RemoteInfo (from
// GitHandler.h, indirectly used by GitWorker::localStateReady).
// Forward-declaration of GitWorker is not enough — moc needs the
// complete types to generate the signal/slot bridge.
#include "git/GitHandler.h"

namespace ghm::github  { class GitHubClient; }
namespace ghm::git     { class GitWorker;    }
namespace ghm::session { class SessionController; }

namespace ghm::workspace {

class PublishController : public QObject {
    Q_OBJECT
public:
    PublishController(ghm::github::GitHubClient&     client,
                      ghm::git::GitWorker&           worker,
                      ghm::session::SessionController& session,
                      QObject* parent = nullptr);

    // True while a flow is in progress (between start() and a
    // succeeded/failed signal). Host checks this before starting a
    // new flow to refuse overlapping starts.
    bool isActive() const { return !state_.path.isEmpty(); }

    QString activePath() const { return state_.path; }

    // Inputs from the host once the user has filled in the dialog.
    // CreateNew: we'll POST /user/repos with these fields first.
    // ExistingRepo: the host has already resolved the target repo
    // and just needs us to wire up the remote.
    enum class Mode { CreateNew, ExistingRepo };

    struct StartParams {
        Mode    mode;
        QString localPath;        // path to the local folder
        QString newRepoName;      // for CreateNew
        QString newRepoDescription;
        bool    isPrivate{false};
        // License / gitignore templates (CreateNew only). Empty =
        // don't add. Both force auto_init on the GitHub side; this
        // means the remote will have an initial commit (LICENSE
        // and/or .gitignore) that diverges from the local history,
        // so the host MUST set pushAfter=false and tell the user
        // to pull first. Or treat them as documentation-only and
        // let GitHub be the source of truth for those two files.
        QString licenseTemplate;
        QString gitignoreTemplate;
        ghm::github::Repository existing;  // for ExistingRepo
        bool    pushAfter{true};
    };

    // Kick off the flow. Returns false if the controller is already
    // busy with another publish (caller should not have called us,
    // but we double-check). Returns true if the flow has started;
    // the host should expect either succeeded() or failed() down
    // the road.
    bool start(const StartParams& params);

    // Abort the current flow immediately. Used when the user closes
    // the app or switches to another folder while publish is mid-
    // flight. No signals are emitted — caller is presumed to know
    // they're aborting.
    void reset();

Q_SIGNALS:
    // Status messages for the host to flash in the status bar /
    // progress indicator. Empty string is allowed and means
    // "still busy but no specific status text" — host can show
    // a generic spinner.
    void progress(const QString& message);

    // Terminal outcomes. Exactly one of these fires per start().
    void succeeded(const QString& localPath,
                   const QString& cloneUrl,
                   const QString& repoFullName,
                   bool pushed);
    void failed(const QString& localPath,
                const QString& userFacingTitle,
                const QString& userFacingDetail);

    // Non-fatal info events that the host may want to surface:
    //
    //   * repoCreated: GitHub created the repo — caller updates
    //     its repos cache and sidebar before we proceed to remote-add.
    //
    //   * needNonEmptyBranch: we wired up origin but the branch has
    //     no commits, so push can't happen. Host shows a dialog;
    //     controller is already in idle state by the time this fires.
    void repoCreated(const ghm::github::Repository& repo,
                     const QString& localPath);
    void needNonEmptyBranch(const QString& localPath);

private Q_SLOTS:
    // Reactions to GitHubClient signals.
    void onRepositoryCreated(const ghm::github::Repository& repo);
    void onClientNetworkError(const QString& message);

    // Reactions to GitWorker signals.
    void onRemoteOpFinished(bool ok, const QString& path, const QString& error);
    void onLocalStateReady(const QString& path,
                           bool                                       isRepository,
                           const QString&                             branch,
                           const std::vector<ghm::git::StatusEntry>&  entries,
                           const std::vector<ghm::git::RemoteInfo>&   remotes);
    void onPushFinished(bool ok, const QString& path, const QString& error);

    void onWatchdogTimeout();

private:
    enum class Step {
        Idle,
        CreatingRepo,    // waiting for GitHubClient::repositoryCreated
        AddingRemote,    // waiting for GitWorker::remoteOpFinished
        RefreshingState, // waiting for GitWorker::localStateReady
        Pushing,         // waiting for GitWorker::pushFinished
    };

    struct State {
        Step    step{Step::Idle};
        QString path;
        QString repoFullName;  // populated after step 2 (create) or 1 (existing)
        QString cloneUrl;
        bool    pushAfter{true};

        bool isEmpty() const { return path.isEmpty(); }
    };

    // Move to the given step and (re)start the watchdog so a stalled
    // network/disk operation doesn't keep us busy forever. Pass
    // Step::Idle to clear without re-arming the timer.
    void transitionTo(Step next);

    // Drop state and report failure to the host. Host then shows a
    // dialog and resets its UI.
    void fail(const QString& title, const QString& detail);

    ghm::github::GitHubClient&       client_;
    ghm::git::GitWorker&             worker_;
    ghm::session::SessionController& session_;

    State   state_;
    QTimer  watchdog_;

    // Step timeouts. Network steps can be slow over flaky links;
    // local ops should be near-instant. Conservative defaults —
    // if we hit them the user is better off seeing an error than
    // staring at a hung dialog.
    static constexpr int kNetworkStepTimeoutMs = 60'000;  // 60s
    static constexpr int kLocalStepTimeoutMs   = 15'000;  // 15s
};

} // namespace ghm::workspace
