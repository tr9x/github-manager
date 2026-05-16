#include "workspace/PublishController.h"

#include "github/GitHubClient.h"
#include "git/GitWorker.h"
#include "session/SessionController.h"

namespace ghm::workspace {

PublishController::PublishController(
        ghm::github::GitHubClient&     client,
        ghm::git::GitWorker&           worker,
        ghm::session::SessionController& session,
        QObject*                       parent)
    : QObject(parent)
    , client_(client)
    , worker_(worker)
    , session_(session)
{
    // Subscribe to every relevant signal once. Each handler is a
    // no-op unless we're in the right step of the flow — that
    // keeps the controller from reacting to e.g. a manual push the
    // user kicked off from the Remotes tab.
    connect(&client_, &ghm::github::GitHubClient::repositoryCreated,
            this, &PublishController::onRepositoryCreated);
    connect(&client_, &ghm::github::GitHubClient::networkError,
            this, &PublishController::onClientNetworkError);

    connect(&worker_, &ghm::git::GitWorker::remoteOpFinished,
            this, &PublishController::onRemoteOpFinished);
    connect(&worker_, &ghm::git::GitWorker::localStateReady,
            this, &PublishController::onLocalStateReady);
    connect(&worker_, &ghm::git::GitWorker::pushFinished,
            this, &PublishController::onPushFinished);

    // Watchdog: one-shot timer per step. We restart it each
    // transition; if it ever fires, the current step is stuck and
    // we abort the flow with a clear error.
    watchdog_.setSingleShot(true);
    connect(&watchdog_, &QTimer::timeout,
            this, &PublishController::onWatchdogTimeout);
}

bool PublishController::start(const StartParams& params)
{
    if (isActive()) {
        // Caller is supposed to check isActive() first, but defend
        // anyway — silently dropping a second start would be confusing.
        return false;
    }
    if (params.localPath.isEmpty()) return false;

    state_           = {};  // reset before populating
    state_.path      = params.localPath;
    state_.pushAfter = params.pushAfter;

    if (params.mode == Mode::CreateNew) {
        Q_EMIT progress(tr("Creating GitHub repository \"%1\"…")
                            .arg(params.newRepoName));
        transitionTo(Step::CreatingRepo);
        client_.createRepository(params.newRepoName,
                                 params.newRepoDescription,
                                 params.isPrivate,
                                 /*autoInit*/ false,
                                 params.licenseTemplate,
                                 params.gitignoreTemplate);
        return true;
    }

    // ExistingRepo: skip the create step.
    if (!params.existing.isValid() || params.existing.cloneUrl.isEmpty()) {
        // Bail before touching any worker — fail() will fire failed()
        // and reset state.
        fail(tr("Publish failed"),
             tr("The selected repository has no clone URL. "
                "Try refreshing the list."));
        return true;  // we DID accept the call, even though it failed instantly
    }

    state_.cloneUrl     = params.existing.cloneUrl;
    state_.repoFullName = params.existing.fullName;

    // Tell host the repo is effectively "created" — it's the same
    // payload it would receive after the API call, so the host can
    // update its caches/sidebar identically for both modes.
    Q_EMIT repoCreated(params.existing, state_.path);

    Q_EMIT progress(tr("Linking %1 → %2…")
                        .arg(state_.path, params.existing.fullName));
    transitionTo(Step::AddingRemote);
    worker_.addRemote(state_.path, QStringLiteral("origin"),
                      state_.cloneUrl);
    return true;
}

void PublishController::reset()
{
    state_ = {};
    watchdog_.stop();
}

void PublishController::transitionTo(Step next)
{
    state_.step = next;
    if (next == Step::Idle) {
        watchdog_.stop();
        return;
    }
    // Pick a sensible timeout for the step kind. Network steps get a
    // longer window because GitHub can be slow when busy.
    const int timeoutMs =
        (next == Step::CreatingRepo || next == Step::Pushing)
            ? kNetworkStepTimeoutMs
            : kLocalStepTimeoutMs;
    watchdog_.start(timeoutMs);
}

void PublishController::fail(const QString& title, const QString& detail)
{
    const QString path = state_.path;  // copy before reset
    reset();
    Q_EMIT failed(path, title, detail);
}

// ----- Client signal handlers -----------------------------------------------

void PublishController::onRepositoryCreated(const ghm::github::Repository& repo)
{
    // We listen to repositoryCreated unconditionally, so we have to
    // filter — the user might have created a repo from elsewhere
    // (e.g. a future "New repo…" action), in which case we just
    // ignore it.
    if (state_.step != Step::CreatingRepo) return;
    if (!repo.isValid()) {
        fail(tr("Publish failed"),
             tr("GitHub created the repository but didn't return a "
                "valid clone URL."));
        return;
    }

    state_.repoFullName = repo.fullName;
    state_.cloneUrl     = repo.cloneUrl;

    Q_EMIT repoCreated(repo, state_.path);

    Q_EMIT progress(tr("Created %1 — wiring up origin…").arg(repo.fullName));
    transitionTo(Step::AddingRemote);
    worker_.addRemote(state_.path, QStringLiteral("origin"), repo.cloneUrl);
}

void PublishController::onClientNetworkError(const QString& message)
{
    // Only relevant if the network error interrupted the CreatingRepo
    // step. Errors during other steps come from GitWorker.
    if (state_.step != Step::CreatingRepo) return;
    fail(tr("Publish failed"), message);
}

// ----- Worker signal handlers ----------------------------------------------

void PublishController::onRemoteOpFinished(bool ok, const QString& path,
                                           const QString& error)
{
    if (state_.step != Step::AddingRemote || path != state_.path) return;

    if (!ok) {
        // The GitHub repo exists but origin failed to wire up.
        // Surface the clone URL so the user can recover manually.
        fail(tr("Publish failed"),
             tr("The GitHub repository was created (or selected), but "
                "wiring up the local 'origin' remote failed:\n\n%1\n\n"
                "You can add it manually with:\n  git remote add origin %2")
                 .arg(error, state_.cloneUrl));
        return;
    }

    // Remote is wired. If we don't have to push, we're done.
    if (!state_.pushAfter) {
        const QString fullClone = state_.cloneUrl;
        const QString repoName  = state_.repoFullName;
        const QString path_     = state_.path;
        reset();
        Q_EMIT progress(tr("Connected to %1.").arg(fullClone));
        Q_EMIT succeeded(path_, fullClone, repoName, /*pushed*/ false);
        return;
    }

    // We need to push. We don't know the current branch name here —
    // refresh local state to read it, then push from the resulting
    // callback. This is the original design and avoids an extra
    // worker entrypoint just to fetch the branch name.
    transitionTo(Step::RefreshingState);
    worker_.refreshLocalState(state_.path);
}

void PublishController::onLocalStateReady(
    const QString& path,
    bool                                       isRepository,
    const QString&                             branch,
    const std::vector<ghm::git::StatusEntry>&  entries,
    const std::vector<ghm::git::RemoteInfo>&   remotes)
{
    Q_UNUSED(isRepository);
    Q_UNUSED(entries);
    Q_UNUSED(remotes);
    if (state_.step != Step::RefreshingState || path != state_.path) return;

    // "Unborn" branch — repo has no commits. Push impossible. Tell
    // the host so it can prompt the user to commit first; we wrap
    // up as a non-failure (the remote IS connected; the push step
    // was never possible).
    const bool unborn = branch.isEmpty() || branch.startsWith(QLatin1Char('('));
    if (unborn) {
        const QString fullClone = state_.cloneUrl;
        const QString repoName  = state_.repoFullName;
        const QString path_     = state_.path;
        reset();
        Q_EMIT needNonEmptyBranch(path_);
        Q_EMIT succeeded(path_, fullClone, repoName, /*pushed*/ false);
        return;
    }

    Q_EMIT progress(tr("Pushing %1 → origin…").arg(branch));
    transitionTo(Step::Pushing);
    worker_.pushTo(state_.path, QStringLiteral("origin"), branch,
                   /*setUpstreamAfter*/ true, session_.token());
}

void PublishController::onPushFinished(bool ok, const QString& path,
                                       const QString& error)
{
    if (state_.step != Step::Pushing || path != state_.path) return;

    if (!ok) {
        fail(tr("Publish failed at push"),
             tr("The remote is connected, but pushing your commits "
                "failed:\n\n%1\n\nYou can retry from the Remotes tab.")
                 .arg(error));
        return;
    }

    const QString fullClone = state_.cloneUrl;
    const QString repoName  = state_.repoFullName;
    const QString path_     = state_.path;
    reset();
    Q_EMIT succeeded(path_, fullClone, repoName, /*pushed*/ true);
}

void PublishController::onWatchdogTimeout()
{
    // The current step took too long. We don't know whether the
    // underlying op completed and the signal got lost or whether
    // it's genuinely hung, but either way the user is staring at a
    // spinner — better to surface an error and let them retry.
    QString stepName;
    switch (state_.step) {
        case Step::CreatingRepo:    stepName = tr("creating the repository"); break;
        case Step::AddingRemote:    stepName = tr("wiring up origin");        break;
        case Step::RefreshingState: stepName = tr("reading local state");     break;
        case Step::Pushing:         stepName = tr("pushing your commits");    break;
        case Step::Idle:            return;  // shouldn't fire when idle
    }
    fail(tr("Publish timed out"),
         tr("Step \"%1\" did not finish in time. The network may be slow "
            "or the operation may be stuck. You can retry from the "
            "Remotes tab once you're back online.").arg(stepName));
}

} // namespace ghm::workspace
