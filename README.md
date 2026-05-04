# GitHub Manager

A native Linux desktop client for browsing, cloning, and synchronizing your
GitHub repositories. Built with Qt 6, libgit2, and libsecret.

```
   Sidebar (search + sections)        Detail panel
   ┌──────────────────┐  ┌──────────────────────────────┐
   │ Search…          │  │ myproject                    │
   ├──────────────────┤  │ /home/me/code/myproject      │
   │ GITHUB           │  │                              │
   │ • myorg/api      │  │ master   Author: Me <me@…>   │
   │ • octocat/hello  │  │ ┌──────────────────────────┐ │
   │                  │  │ │ Changes  History Remotes │ │
   │ LOCAL FOLDERS    │  │ ├──────────────────────────┤ │
   │ + Add local…     │  │ │ [A ] src/main.cpp        │ │
   │ • myproject      │  │ │ [ M] README.md           │ │
   │   /home/me/…     │  │ │ [??] notes.txt           │ │
   │                  │  │ │                          │ │
   │                  │  │ │ Stage all  Refresh       │ │
   │                  │  │ │ ──────────────────────── │ │
   │                  │  │ │ [commit message...]      │ │
   │                  │  │ │                  [Commit]│ │
   │                  │  │ └──────────────────────────┘ │
   └──────────────────┘  └──────────────────────────────┘
                       Signed in as octocat
```

## Features

### GitHub repositories
- **Sign in with a GitHub Personal Access Token.** The token is validated
  against the API before being stored in your system keyring (GNOME
  Keyring, KWallet, KeePassXC — anything that speaks the
  `org.freedesktop.Secret.Service` D-Bus interface). No plaintext fallback.
- **Browse all your repos** (owner, collaborator, organization member),
  with a search box and visual indicators for visibility and local-clone
  status.
- **Clone, pull, and push** through libgit2. The PAT is fed to libgit2
  as the HTTPS password via a credentials callback — never written to
  `.git/config`.
- **Pull is fast-forward-only.** Divergent histories are reported as
  conflicts; resolve them with the git CLI rather than producing
  surprise merge commits.

### Local folders (any directory on disk)
- **Add any folder** to a "Local Folders" section in the sidebar — works
  for both existing repositories and fresh directories.
- **`git init`** for non-repo folders, with a configurable initial branch
  name (defaults to `master` to match GitHub's "create empty repo"
  instructions; `main` also works).
- **Three-tab workflow**:
  - **Changes** — per-file status (`[I W] path`, mirroring `git status --short`),
    multi-select stage / unstage, "Stage all" (`git add .`), and a commit
    message field that fires off `git commit -m`.
  - **History** — `git log` rendered as a list of `shortId · summary · author · time ago`,
    with the full commit message of the selected commit shown below.
  - **Remotes** — list of configured remotes, "Add remote…" with smart paste
    (drop in the entire `git remote add origin https://github.com/owner/repo.git`
    line GitHub gives you, or just the URL), and a Push panel with optional
    "Set upstream (-u)".
- **Author identity** is asked once (lazy-prompted before the first commit)
  and stored in `QSettings` only — never written to `.git/config` or
  uploaded anywhere.

### Cross-cutting
- **Browse local files** (read-only `QFileSystemModel` tree) and switch
  between local branches in the GitHub-clone view.
- **Non-blocking UI.** GitHub HTTP calls use `QNetworkAccessManager`;
  libgit2 work goes through `QtConcurrent::run` with a `QFutureWatcher`
  marshalling results back to the GUI thread.
- **Dark theme** by default (disable with `--no-dark-mode`).

## Architecture

The codebase is split into four layers, each in its own directory under
`src/`. Lower layers don't know about higher ones.

```
ui/        ─── widgets and dialogs (Qt Widgets)
git/       ─── libgit2 wrapper + threading
github/    ─── REST API client and data model
core/      ─── settings, secure storage
```

| Component | Responsibility |
|-----------|---------------|
| `core/SecureStorage` | libsecret-backed PAT vault. Only place that touches the keyring. |
| `core/Settings` | QSettings wrapper for non-secret preferences (last user, paths, window state, **author identity**, **local folder list**, **default init branch**). |
| `github/Repository` | Plain data model. |
| `github/GitHubClient` | Async REST client (auth, repo listing with pagination). |
| `git/GitHandler` | Synchronous libgit2 wrapper with RAII handles. Implements clone/pull/push and the full local workflow (init, status, stage, commit, log, remote add/remove, push to arbitrary remote with optional upstream). |
| `git/GitWorker` | Adapts `GitHandler` to async signal/slot semantics via QtConcurrent. |
| `ui/LoginDialog` | Token entry + validation. |
| `ui/CloneDialog` | Picks target directory for a clone. |
| `ui/IdentityDialog` | Asks for git author name + email (lazy-prompted before the first commit). |
| `ui/AddRemoteDialog` | Smart-paste field that accepts either `git remote add origin <url>` or a bare URL. Warns about SSH URLs. |
| `ui/RepositoryListWidget` | Sidebar with two sections: GitHub repos and local folders. |
| `ui/RepositoryDetailWidget` | Right-hand panel for GitHub clones: metadata, branch combo, file tree, action buttons. |
| `ui/LocalRepositoryWidget` | Right-hand panel for local folders: init prompt OR three tabs (Changes / History / Remotes) with commit and push UI. |
| `ui/MainWindow` | Coordinator — owns long-lived collaborators, wires signals between them, hosts a `QStackedWidget` that swaps the detail panel based on selection. |

`MainWindow` is the only place that knows about both the GitHub client
and the git worker; everything else communicates through signals so the
layers stay genuinely decoupled.

## Building

### One-line install

```bash
python3 install.py             # builds and installs to ~/.local/bin
python3 install.py --system    # builds and installs to /usr/local/bin (uses sudo)
python3 install.py --no-deps   # skip distro-package install, just build
```

(Both scripts have a `#!/usr/bin/env python3` shebang, so `chmod +x install.py uninstall.py && ./install.py` works too.)

The installer detects Arch, Debian/Ubuntu, and Fedora and uses the
matching package manager. On other distributions you'll be prompted to
install the dependencies manually:

- CMake ≥ 3.21, Ninja, a C++20 compiler, pkg-config
- Qt 6.4+ (`Core`, `Gui`, `Widgets`, `Network`, `Concurrent`)
- libgit2 ≥ 1.5
- libsecret-1

### Manual build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/github-manager
```

### Uninstall

```bash
python3 uninstall.py            # removes installed binary + build/
python3 uninstall.py --purge    # also removes ~/.config/github-manager
```

Tokens in the keyring are not auto-removed. Use the in-app **Sign out**
menu, or open `seahorse` / KWallet and delete entries with schema
`local.github-manager.Token`.

## Running

On first launch you'll be prompted for a Personal Access Token. Generate
one at <https://github.com/settings/tokens> with at least the **repo**
scope (and **admin:org** if you want to see private organisation repos).

The token is validated against `GET /user`, and on success stored in
your default keyring under the schema `local.github-manager.Token`.
Subsequent launches restore the session silently.

### Command-line flags

```
github-manager                # default
github-manager --no-dark-mode # use the system palette instead
```

## Error handling

| Failure mode | Surface |
|--------------|---------|
| Invalid token | Login dialog shows GitHub's message inline; existing sessions trigger a warning + re-prompt. |
| Network error | Status-bar message + `QMessageBox::warning` with the HTTP status and GitHub error body. |
| Missing keyring | Warning dialog after login; the user can keep working but won't get auto-login next time. |
| Clone target exists | Pre-flight check with a clear message before libgit2 is even called. |
| Pull divergence | Operation is aborted with "branches have diverged"; the user fixes it with the CLI. |
| libgit2 errors | Wrapped in `GitResult` with the original `git_error_last()` message. |

## Local-folder workflow at a glance

The fastest path from "empty directory on disk" to "pushed to GitHub":

1. **File → Add Local Folder…** (or `Ctrl+L`), pick the folder.
2. If the folder isn't a git repo yet, the panel shows **Initialize Repository**
   with a branch-name field. Defaults to `master` so you can copy-paste GitHub's
   sample push line verbatim. Click **Initialize**.
3. **Changes** tab: edit files in your editor, hit **Refresh**, double-click
   files (or use **Stage all** for `git add .`), write a commit message,
   click **Commit**. The first commit prompts for your name + email.
4. Switch to the **Remotes** tab. Click **Add remote…**, paste either:
   - the entire `git remote add origin https://github.com/owner/repo.git` line
     that GitHub shows on a freshly-created empty repo, **or**
   - just the URL.
5. Below the remotes list, the **Push** panel lets you choose remote/branch
   and push. Leave **Set upstream (-u)** checked for the very first push so
   subsequent pushes know where to go.

Equivalent CLI sequence:

```bash
git init -b master
git add .
git commit -m "Initial commit"
git remote add origin https://github.com/owner/repo.git
git push -u origin master
```

## Limitations / known scope cuts

- **No merge / rebase / cherry-pick.** Branches and merges still happen in your
  CLI for now; the app handles linear history (commit / push / fast-forward pull).
- **No SSH clones or pushes.** HTTPS + PAT only. SSH URLs are detected and a
  warning is shown when adding such a remote.
- **No two-factor flow.** PATs (or fine-grained tokens) are required.
- **No diff viewer.** The Changes list shows status flags; double-click toggles
  staged/unstaged but doesn't open a diff.

## License

Source files include no license headers; choose what suits you (MIT, Apache 2.0,
GPL-3.0) and add a `LICENSE` file at the project root before publishing.
