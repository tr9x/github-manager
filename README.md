# GitHub Manager

A native Linux desktop client for working with GitHub repositories and
local Git folders. Built with Qt 6, libgit2, and libsecret.

```
   Sidebar                       Detail panel (local folder mode)
   ┌──────────────────┐  ┌─────────────────────────────────────────┐
   │ Search…          │  │ myproject                               │
   ├──────────────────┤  │ /home/me/code/myproject                 │
   │ GITHUB           │  │                                         │
   │ • myorg/api      │  │ [master ↑2 ▾]    Author: Me <me@…>      │
   │ • octocat/hello  │  │ ┌─────────────────────────────────────┐ │
   │                  │  │ │ Changes  History  Remotes           │ │
   │ LOCAL FOLDERS    │  │ ├─────────────────────────────────────┤ │
   │ + Add local…     │  │ │ [A ] src/main.cpp                   │ │
   │ • myproject      │  │ │ [ M] README.md                      │ │
   │   /home/me/…     │  │ │ [??] notes.txt                      │ │
   │                  │  │ │ ─────────────────────────────────── │ │
   │                  │  │ │ Find in diff  (Ctrl+F)   3 of 17 ▲▼ │ │
   │                  │  │ │ ─────────────────────────────────── │ │
   │                  │  │ │   12 |   13 │  context line         │ │
   │                  │  │ │      |   14 │ +added line           │ │
   │                  │  │ │   13 |      │ -removed line         │ │
   │                  │  │ │ ─────────────────────────────────── │ │
   │                  │  │ │ Commit message...        [Commit]   │ │
   │                  │  │ └─────────────────────────────────────┘ │
   └──────────────────┘  └─────────────────────────────────────────┘
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

### Publish a local folder to GitHub

The most polished part of the app. From an arbitrary directory on disk
to a fully wired-up, pushed-to-GitHub repository in a couple of clicks:

- **One dialog, two paths.** *Create a new GitHub repository* (the app
  POSTs to `/user/repos` for you, no need to visit github.com first), or
  *Link to an existing repository* (pick from your repo list).
- **Smart defaults.** Repo name pre-filled from folder name, sanitised
  for GitHub's naming rules. Collisions with your existing repos
  detected before submit.
- **Optional one-shot push.** Leave "Push my commits after publishing"
  ticked and the app does `git remote add origin … + git push -u origin
  <branch>` for you. Replaces the entire `git init … remote add … push`
  dance with a single button.
- **Identity prompted lazily.** First commit asks for your name and
  email once and remembers them.

### Local-folder workflow (any directory on disk)

- **Add any folder** to a "Local Folders" section in the sidebar. Works
  for both existing repositories and fresh directories.
- **`git init`** for non-repo folders, with a configurable initial
  branch name (defaults to `master` to match GitHub's "create empty
  repo" instructions; `main` also works).
- **Three-tab workflow**:
  - **Changes** — per-file status (`[I W] path`, mirroring `git status
    --short`), multi-select stage / unstage, "Stage all" (`git add .`),
    a commit message field, and a **side-by-side diff view** for the
    selected file (green additions, red deletions, blue hunk headers,
    colour-coded line numbers).
  - **History** — `git log` rendered as a list, with the full commit
    message and **the diff a commit introduces** (a la `git show`)
    shown for each click. Lists files changed by the commit; clicking
    a file shows that file's diff.
  - **Remotes** — list of configured remotes, "Add remote…" with smart
    paste (drop in the entire `git remote add origin …` line GitHub
    gives you, or just the URL), and a Push panel with optional "Set
    upstream (-u)".
- **Branch management.** The branch name in the header is a popup
  picker — click to switch branches, create a new one (with rule-based
  name validation: rejects spaces, leading dots, reserved names, etc.),
  or delete one. Force-delete prompts a second confirmation when the
  branch has unique commits.

### Diff viewer

- **Unified diff with colours**, line numbers in a left gutter, hunk
  headers that match what `git diff` shows.
- **Find in diff (Ctrl+F).** A permanent search bar above every diff
  pane. Highlights all matches, navigate with `Enter` / `Shift+Enter`
  or the ▲▼ arrows. Live counter ("3 of 17"). Toggle case-sensitive
  with the `Aa` button. Esc clears the query.
- **Reused everywhere.** The same diff view is used for working-tree
  changes (Changes tab) and historical commits (History tab), so you
  learn one set of UX once.

### Cross-cutting

- **Auto-refresh.** `QFileSystemWatcher` watches `.git/HEAD`,
  `.git/index`, and the working-tree root for external changes
  (file edits, CLI git operations, branch switches, etc.). Hits are
  debounced (300 ms) so a single `git add .` from your terminal
  triggers exactly one UI refresh.
- **Multilingual.** UI available in English and Polish; switch under
  *Settings → Language*. Defaults to system locale on first run.
- **Non-blocking UI.** GitHub HTTP calls use `QNetworkAccessManager`;
  libgit2 work goes through `QtConcurrent::run` with a
  `QFutureWatcher` marshalling results back to the GUI thread.
- **Dark theme** by default (disable with `--no-dark-mode`).

## Architecture

| Component | Responsibility |
|-----------|---------------|
| `core/SecureStorage` | libsecret-backed PAT vault. The only place that touches the keyring. |
| `core/Settings` | QSettings wrapper for non-secret preferences (last user, paths, window state, author identity, local folder list, default init branch, language). |
| `core/Translator` | In-process `QTranslator` subclass holding an EN→PL phrasebook. No `.ts/.qm` pipeline required. |
| `github/Repository` | Plain data model for a GitHub repo. |
| `github/GitHubClient` | Async REST client (auth, repo listing with pagination, repository creation). |
| `git/GitHandler` | Synchronous libgit2 wrapper with RAII handles. Implements clone/pull/push, full local workflow (init, status, stage, commit, log, remote add/remove, push), branch management, and diff (single-file + commit). |
| `git/GitWorker` | Adapts `GitHandler` to async signal/slot semantics via `QtConcurrent`. |
| `ui/MainWindow` | Coordinator — owns long-lived collaborators, wires signals, hosts a `QStackedWidget` swapping the detail panel. |
| `ui/RepositoryListWidget` | Sidebar with two sections: GitHub repos and local folders. |
| `ui/RepositoryDetailWidget` | GitHub-clone detail panel (read-only file tree, branch combo, push/pull buttons). |
| `ui/LocalRepositoryWidget` | Local-folder detail panel with three tabs (Changes / History / Remotes), branch picker, identity bar. |
| `ui/DiffViewWidget` | Read-only unified-diff renderer with permanent search bar (Ctrl+F). |
| `ui/SearchBar` | Reusable Ctrl+F find-bar (live highlights, prev/next, case toggle). |
| `ui/LoginDialog` | Token entry + validation. |
| `ui/CloneDialog` | Picks target directory for a clone. |
| `ui/PublishToGitHubDialog` | Two-mode publish dialog (create new / link existing). |
| `ui/AddRemoteDialog` | Smart-paste remote-add dialog. |
| `ui/CreateBranchDialog` | New-branch creation with name validation. |
| `ui/IdentityDialog` | Lazy-prompted git author name + email. |
| `ui/SupportDialog` | "Help → Support / Donate" — author info and donation details. |

## Local-folder workflow at a glance

The fastest path from "empty directory on disk" to "pushed to GitHub":

1. **File → Add Local Folder…** (or `Ctrl+L`), pick the folder.
2. If the folder isn't a git repo yet, the panel shows **Initialize
   Repository** with a branch-name field. Click **Initialize**.
3. **Changes** tab: edit files in your editor, the app auto-refreshes
   when you save. Stage files (or **Stage all** for `git add .`), write
   a commit message, click **Commit**. The first commit prompts for
   your name + email.
4. Click **Publish to GitHub…** in the blue banner that appears once
   you have commits but no `origin`. Pick *Create new* or *Link to
   existing*; leave **Push my commits** ticked. One click does the rest.
5. From here on, just **Commit** + **Push** in the Remotes tab as
   normal.

Equivalent CLI sequence:

```bash
git init -b master
git add .
git commit -m "Initial commit"
gh repo create myproject --public --source=. --remote=origin --push
# (or:  git remote add origin <url> + git push -u origin master)
```

## Building

### Dependencies

- Qt 6.4 or newer (`qt6-base`, `qt6-tools`)
- libgit2 (1.6+)
- libsecret 0.20+
- A C++20 compiler (gcc 13+, clang 16+)
- CMake 3.20+, Ninja or make

### Distro packages

```bash
# Arch / Manjaro
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base qt6-tools libgit2 libsecret git

# Debian / Ubuntu
sudo apt install build-essential cmake ninja-build pkg-config \
    qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
    libgit2-dev libsecret-1-dev git

# Fedora
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config \
    qt6-qtbase-devel qt6-qttools-devel libgit2-devel libsecret-devel git
```

### Install via the wrapper script

```bash
python3 install.py            # installs to ~/.local/bin
python3 install.py --system   # installs to /usr/local/bin (needs sudo)
```

The script detects your distro, installs missing dependencies, runs
CMake, builds, and copies the binary + the `.desktop` file into place.

### Manual build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/github-manager
```

### Uninstall

```bash
python3 uninstall.py
```

## Limitations / known scope cuts

- **No merge / rebase / cherry-pick.** Branches and merges still happen
  in your CLI for now; the app handles linear history (commit / push /
  fast-forward pull / branch create-switch-delete).
- **No SSH clones or pushes.** HTTPS + PAT only. SSH URLs are detected
  and a warning is shown when adding such a remote.
- **No two-factor flow.** PATs (or fine-grained tokens) are required.
- **No commit signing.** GPG/SSH signing isn't wired through libgit2's
  callback yet.
- **No submodules support.** `.gitmodules` is not handled.

See `ROADMAP.md` (or open an issue) for what's planned next — the next
likely additions are SSH support, branch rename, and a tag manager.

## Privacy and data handling

- Your PAT lives in your system keyring only, accessed exclusively
  through `core/SecureStorage`. The app never logs it, sends it
  anywhere except `api.github.com`, or writes it to disk in plaintext.
- Your git author identity (`name`, `email`) is stored in `QSettings`
  on this machine and used only when creating commits locally. It is
  never sent to GitHub directly — it goes into the commit, which is
  later pushed by you.
- `Settings → Language`, the list of local folders, last sign-in name,
  and window geometry are also stored in `QSettings`. Nothing else
  leaves your computer.

## Support / Donate

GitHub Manager is built in spare time. If it saves you time and you
want to chip in, *Help → Support / Donate…* shows bank transfer details
in PLN. Every contribution helps keep the project alive — fixing bugs,
adding features, supporting Linux as a first-class platform.

## License

(See `LICENSE` in the repository root.)
