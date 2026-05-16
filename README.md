# GitHub Manager

[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](CHANGELOG.md)
[![Platform](https://img.shields.io/badge/platform-Linux-lightgrey.svg)]()
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)]()
[![Qt](https://img.shields.io/badge/Qt-6.5%2B-41CD52.svg)]()

A native Linux desktop client for managing GitHub repositories and
local Git folders. Built with Qt 6 Widgets, libgit2, and libsecret —
no Electron, no web wrapper, no telemetry. Just a desktop app that
respects your system.

```
   Sidebar                       Detail panel (remote repo)
   ┌──────────────────┐  ┌────────────────────────────────────────┐
   │ Search…          │  │ tr9xpl/github-manager                  │
   ├──────────────────┤  │ Public repository                      │
   │ GITHUB           │  │ Linux desktop GitHub repository...     │
   │ • myorg/api      │  │                                        │
   │ • octocat/hello  │  │ [Clone…] [Open Local…] [Open on GitHub]│
   │ • tr9xpl/...★    │  │                                        │
   │                  │  │ ╭─README─╮─Files─╮─About─╮             │
   │ LOCAL FOLDERS    │  │ # github-manager                       │
   │ + Add local…     │  │                                        │
   │ • myproject      │  │ A native Linux desktop client for      │
   │   /home/me/…     │  │ managing GitHub repositories and       │
   │                  │  │ local Git folders. Built with Qt 6,    │
   │                  │  │ libgit2, and libsecret...              │
   └──────────────────┘  └────────────────────────────────────────┘
```

## Features

**Local Git operations** — full feature set without leaving the app:
- Stage / unstage / commit with author identity detection from
  `~/.gitconfig`
- Branch switching, creation, deletion, fast-forward / rebase pulls
- Tags (lightweight and annotated), with GPG signing support
- Stash save / list / pop / drop
- Submodules with per-submodule SSH key memory
- Conflict resolution UI when merges go sideways
- Reflog viewer for recovering lost commits
- "Undo last commit" with reset modes (soft/mixed/hard)

**GitHub integration**:
- OAuth device flow or Personal Access Token authentication
- Browse all your repos in a searchable sidebar
- README, file tree, and project stats preview before cloning
- One-click clone with HTTPS or SSH
- Publish a local folder as a new GitHub repo (with optional
  LICENSE and .gitignore templates)
- Toggle repository visibility (public ↔ private) from a context menu
- Push, pull, fetch with progress feedback

**Security & privacy**:
- Tokens stored in the system keyring (libsecret / GNOME Keyring /
  KWallet via libsecret)
- TLS certificate approval UI for self-signed / internal CAs —
  manage trusted servers from a dedicated settings page
- GPG commit and tag signing with verification badges in History
- Per-submodule explicit SSH key support for deploy keys
- No telemetry, no analytics, no "phone home"

**UI niceties**:
- Light and dark theme support
- Multi-language UI: English, Polski, Deutsch, Español, Français
  (partial coverage for the latter three)
- Inline diff viewer with syntax highlighting
- History filter with match counter ("3 of 47")
- Viewport-only signature verification — no waiting on GPG for
  commits you're not looking at
- F5 to refresh, Ctrl+E to open repo folder, right-click for
  context actions everywhere it makes sense
- Click the version label in the bottom-right to see the full
  changelog

## Screenshots

*(Drop screenshots in `docs/screenshots/` and link them here. The
short ASCII diagram at the top of this README is what a screenshot
would replace.)*

## Install

### Arch Linux / Manjaro

```bash
git clone https://github.com/tr9xpl/github-manager.git
cd github-manager
python install.py
```

The installer pulls dependencies via pacman, configures with CMake,
builds with Ninja, and installs to `~/.local`. After it finishes:

```bash
github-manager
```

…or launch from your application menu (a `.desktop` file is
installed under `~/.local/share/applications/`).

### Ubuntu / Debian

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
                 qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
                 libgit2-dev libsecret-1-dev git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build --parallel
cmake --install build --prefix ~/.local
```

### Fedora

```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config \
                 qt6-qtbase-devel qt6-qttools-devel \
                 libgit2-devel libsecret-devel git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build --parallel
cmake --install build --prefix ~/.local
```

### OAuth setup (optional)

GitHub Manager works fine with Personal Access Tokens (the default).
For one-click OAuth device flow login, supply a GitHub OAuth App
client ID at build time:

```bash
cmake -S . -B build -DGHM_OAUTH_CLIENT_ID=Ov23liXXXXXXXXXXXXXX -G Ninja
```

Without `GHM_OAUTH_CLIENT_ID`, the app falls back to PAT-only login.
Create a PAT at https://github.com/settings/tokens with `repo`
scope (or `public_repo` if you only manage public repos).

## Quick tour

After signing in:

1. **Browse your GitHub repos** in the left sidebar. Click any one
   to see its README, root file listing, languages bar, stars,
   forks, and topics — without cloning.
2. **Clone** a repo via the Clone… button. The clone happens in a
   background thread; the sidebar updates when it's done.
3. **Open** an existing local folder via *File → Add Local Folder…*.
   GitHub Manager detects whether it's a git repository and reveals
   the working-copy controls if so.
4. **Make changes**, then use the Changes tab to stage and commit.
   Author identity comes from your `~/.gitconfig`.
5. **Push** via the toolbar. First push to a new branch sets the
   upstream automatically.

## Keyboard shortcuts

| Action                    | Shortcut          |
|---------------------------|-------------------|
| Refresh status / history  | F5 or Ctrl+R      |
| Open folder in file mgr   | Ctrl+E            |
| Fetch from origin         | Ctrl+Shift+F      |
| Undo last commit…         | Ctrl+Shift+Z      |
| Quit                      | Ctrl+Q            |
| Search filter in lists    | Ctrl+F            |

## Architecture

```
src/
├── core/           # Settings, SecureStorage (libsecret), Translator
├── git/            # GitHandler (sync, reentrant), GitWorker (async),
│                   # CommitSigner (GPG), SignatureVerifier
├── github/         # GitHubClient (REST + OAuth device flow), Repository
├── session/        # SessionController, OAuthFlowController
├── workspace/      # LocalWorkspaceController, PublishController,
│                   # ConflictController, GitHubCloneController
└── ui/             # MainWindow, dialogs, widgets
```

**Threading model**: libgit2 work happens on a dedicated worker
thread (`GitWorker`); GUI thread never blocks on disk or network.
Credential prompts (SSH host keys, TLS certificates, OAuth) use
`BlockingQueuedConnection` to pop dialogs synchronously from the
worker's perspective.

**Storage**: Settings via `QSettings` (INI under `~/.config/`),
tokens via libsecret. No SQLite, no per-app config directory soup.

**Translations**: Qt's standard `.ts` → `.qm` pipeline. PL has full
coverage; DE / ES / FR have partial coverage for common UI
vocabulary (menus, buttons, common labels). See *Contributing →
Translations* below.

## Building from source

Requires:
- C++20 compiler (GCC 11+ or Clang 14+)
- CMake 3.21+
- Qt 6.5+ (Core, Gui, Widgets, Network, Concurrent, LinguistTools)
- libgit2 1.5+
- libsecret 0.20+
- Ninja (recommended; Make works too)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build --parallel
```

The build embeds CHANGELOG.md and translation `.qm` files as Qt
resources — no runtime file lookup, no path issues. The resulting
binary at `build/github-manager` is self-contained apart from its
dynamic library dependencies (libgit2, libsecret, Qt itself).

## Tests

Opt-in via `-DGHM_BUILD_TESTS=ON`:

```bash
cmake -S . -B build -DGHM_BUILD_TESTS=ON -G Ninja
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Unit tests cover parsers and pure helpers (Link header parsing, SSH
URL conversion, OAuth response parsing, branch name validation, repo
name suggestion). Integration tests exercise GitHandler against
temp directories with libgit2.

## Contributing

### Bug reports

Open an issue. Include:
- Linux distro and version (`cat /etc/os-release`)
- Qt version (`pacman -Q qt6-base` or equivalent)
- libgit2 version (`pacman -Q libgit2` or `pkg-config --modversion libgit2`)
- What you expected vs what happened
- Any stderr output (run from terminal to capture it)

### Code

Pull requests welcome. Style notes:
- C++20 with Qt-flavored idioms (`QString`, `Q_OBJECT`, signals/slots)
- Format with `clang-format` if your editor supports it
- New widgets go under `src/ui/`; pure logic helpers under
  `src/core/` or `src/git/`
- Add unit tests for parsers and pure helpers; integration tests
  for anything that touches libgit2

### Translations

Adding a new language:
1. Copy `translations/github-manager_pl.ts` to
   `translations/github-manager_<langcode>.ts`
2. Change the `language="…"` attribute in the `<TS>` tag
3. Blank out the `<translation>` elements (or use Qt Linguist)
4. Add the filename to `GHM_TS_FILES` in `CMakeLists.txt`
5. Add the language to `Settings::supportedLanguages()` and
   `Settings::languageDisplayName()` in `src/core/Settings.cpp`
6. Translate strings in Qt Linguist; rebuild

The format is Qt-standard so any translation tooling (Weblate,
Crowdin, Transifex) can ingest it.

Completing existing partial translations (DE, ES, FR) is also
welcome — those have ~12% coverage and would benefit from native
speaker review.

## Roadmap

See `CHANGELOG.md` for what's done. Open issues for what's planned.

Big-picture: 1.0 is feature-complete for the everyday "manage my
GitHub repos and local folders" workflow. Future directions include
GraphQL API support (for issues/PRs/discussions inline), git-lfs
handling, README image rendering, and broader translation coverage.

## Support

GitHub Manager is built in spare time. If it saves you time and
you'd like to chip in, *Help → Support / Donate…* shows bank
transfer details. Every contribution helps keep the project alive.

## License

See `LICENSE` in the repository root.

## Acknowledgements

Built on the shoulders of:
- [Qt 6](https://www.qt.io/) — UI framework
- [libgit2](https://libgit2.org/) — Git plumbing
- [libsecret](https://wiki.gnome.org/Projects/Libsecret) — credential storage
- [GitHub REST API](https://docs.github.com/en/rest) — remote operations

Inspired by GitHub Desktop, GitKraken, and the wish for a Linux-native
client that doesn't ship a Chromium runtime.
