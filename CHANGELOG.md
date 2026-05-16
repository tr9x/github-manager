# Changelog

All notable changes to this project will be documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project adheres to [Semantic Versioning](https://semver.org/).

Post-1.0 status note: minor bumps (1.x.0) add features without breaking
compatibility; patch bumps (1.x.y) cover fixes. Major bumps (2.0.0+) will
mark breaking changes to the on-disk format, config schema, or public
behavior.

## [1.0.1] — 2026-05-16

### Fixed
- Build failure in 1.0.0: `MainWindow.cpp` creates a `QPushButton`
  for the version label, but the only `QPushButton` declaration
  visible in that translation unit was a forward declaration
  (pulled in via `QDialog`). Method calls and `new QPushButton(…)`
  need the complete type.

  Fix: added `#include <QPushButton>` to `MainWindow.cpp`.

  Pattern note: this is the fourth time in the project's history
  (0.20.1, 0.23.1, 0.32.1, 1.0.1) we've hit "forward-decl OK for
  pointer/ref, need complete type for method calls and `new`".
  MainWindow had been getting away with it via transitive includes
  from dialogs that pulled `<QPushButton>` themselves; switching
  `versionLabel` from `QLabel` to `QPushButton` exposed the gap
  because the version-label site was the first MainWindow direct
  use of `new QPushButton`.

## [1.0.0] — 2026-05-16

### Stable release

This is the first production-ready release of github-manager.
Functionally identical to 0.37.0 — same code, just promoted across
the milestone boundary. The version jump signals that the app is
considered feature-complete and stable enough for daily use.

### Added — clickable version label

The version indicator in the bottom-right corner of the status bar
is now a button. Clicking it opens a Changelog dialog rendering
this very file (`CHANGELOG.md`) so users can review what changed
across versions without leaving the app.

The button styles itself as a subtle link — grey by default, blue
+ underlined on hover, cursor changes to a pointing hand. Matches
the affordance pattern users expect from links in other GUIs.

The changelog content is embedded into the binary as a Qt resource
(`:/CHANGELOG.md`) so the dialog works regardless of where the
app is installed — no filesystem lookup, no path-resolution
issues. Single source of truth: the CHANGELOG.md you edit becomes
the changelog users see, automatically.

### Added — production README

`README.md` rewritten as a real project landing page suitable for
GitHub:
- Hero description and feature list
- Screenshots placeholder section
- Install instructions (Arch Linux focus, but install.py works on
  Ubuntu/Fedora too with the dependency lookup table)
- Architecture overview pointing into the codebase
- Contributing pointers and license note

### Why now

Every backlog item from the 0.x series is either done or
deliberately deferred. The "long backlog" items remaining (DE/ES/FR
full translation, image rendering in README preview, nested
file browsing) are nice-to-have, not blocking. Cutting 1.0.0
acknowledges that.

### Files

- New: `src/ui/ChangelogDialog.{h,cpp}` — modal dialog with
  embedded markdown viewer, link routing to system browser
- New: `README.md` — production landing page
- Modified: `CMakeLists.txt` — VERSION 1.0.0, ChangelogDialog
  sources, CHANGELOG.md as resource
- Modified: `src/ui/MainWindow.cpp` — version QLabel replaced
  with clickable QPushButton + include

## [0.37.0] — 2026-05-16

### Added — Repository detail preview (README / Files / About)

User wanted clicking a repo in the GitHub list to show actual
content, not just metadata. `RepositoryDetailWidget` grows three
new tabs powered by three new GitHub API endpoints. Plus an
"Open on GitHub" button for jumping to the browser.

### What the user sees now

When a remote-only repo is selected, the right panel shows
(in addition to the existing name/description/visibility/last-
updated block and Clone/Open buttons):

```
[Clone…] [Open Local…] [Open on GitHub]              [Refresh] [Pull] [Push]
─────────────────────────────────────────────────────────────────────────────
┌─ README ──┬─ Files ──┬─ About ──┐
│                                  │
│ # Project Name                   │
│                                  │
│ A short description of what      │
│ this project does and who it's   │
│ for…                             │
│                                  │
│ ## Installation                  │
│ ```bash                          │
│ python install.py                │
│ ```                              │
└──────────────────────────────────┘
```

**README tab.** Markdown-rendered via `QTextBrowser::setMarkdown`
(CommonMark + GFM tables). Links route through the same
"Open on GitHub" path. Inline images don't load (Qt would need
an HTTP resource loader hookup) — they appear as broken-image
placeholders, but text content renders cleanly.

**Files tab.** Flat list of root-level files and folders fetched
from `/contents/`. Dirs are sorted first, then files, all
alphabetical. Each entry has a file/folder icon (from the
system theme via `QStyle::standardIcon`). Activating an entry
opens its github.com page in the browser — descending inline
would need breadcrumb UI and back navigation, which the browser
does perfectly.

**About tab.** Five things:
- **Stats line**: ★ stars, ⑂ forks, ● open issues, 📦 size
- **Default branch** (clickable for copying)
- **Primary language** (e.g. "C++")
- **Topics** rendered as inline pill chips
  (`<span style='background:#1f6feb22;…'>tag</span>`)
- **Languages bar** — colored horizontal segments proportional to
  language bytes from `/languages`. Hover shows percentage.
  Colors match GitHub's broad palette (Python blue, JS yellow,
  Rust orange, etc.)

### Local-mode behavior

When a repo IS cloned locally, the **Files tab is hidden** —
user has the real filesystem tree (`QFileSystemModel`) right
above. README and About tabs stay populated; they describe the
GitHub-side state which is independent of the local working
copy.

### API endpoints used

Three new methods on `GitHubClient`:

```cpp
fetchReadme(fullName);     // GET /repos/{owner}/{repo}/readme
fetchContents(fullName,    // GET /repos/{owner}/{repo}/contents/{path}
              path);
fetchLanguages(fullName);  // GET /repos/{owner}/{repo}/languages
```

Each is fire-and-forget; responses come back via three new
signals: `readmeFetched`, `contentsFetched`, `languagesFetched`.
There's also `readmeNotFound` because plenty of repos legitimately
lack a README — not an error worth shouting about.

README content arrives base64-encoded per GitHub spec; we decode
with `QByteArray::fromBase64` which handles the RFC 2045 newlines
that GitHub embeds.

### Cache layer

Each fetch result is cached in `RepositoryDetailWidget` keyed by
`fullName`:

```cpp
QMap<QString, QString>                              readmeCache_;
QMap<QString, QList<ghm::github::ContentEntry>>     contentsCache_;
QMap<QString, QMap<QString, qint64>>                languagesCache_;
```

User clicking between repos in the sidebar doesn't trigger
re-fetch for already-seen repos. Cache is per-session — clearing
on app restart, no disk persistence. The trade-off matches
typical browse patterns (read a few repos quickly, move on).

### Repository struct extensions

Eleven new fields populated from the GitHub API response:
- `htmlUrl`, `primaryLanguage`, `pushedAt`
- `isArchived`, `stargazers`, `forks`, `openIssues`, `watchers`
- `topics` (string list)

`parseRepo()` updated to pull all of these. Backward compatible
(default values match "missing" semantics).

New `ContentEntry` struct in `Repository.h` (sibling type) with
`name`, `type`, `size`, `htmlUrl`, `downloadUrl`. Used by the
files tab. `Q_DECLARE_METATYPE` so it can live in QVariant.

### Architecture — host-mediated dispatch

The detail widget emits intent signals
(`readmeRequested`, `contentsRequested`, `languagesRequested`)
which `MainWindow` forwards to `GitHubClient`. Response signals
go straight from `GitHubClient` to the widget setters via
direct `connect` in `wireSignals()`. The widget never holds a
`GitHubClient` pointer — keeps it testable in isolation.

`openInBrowserRequested(url)` is emitted by:
- The "Open on GitHub" button (`onOpenInBrowser()`)
- README hyperlinks (`QTextBrowser::anchorClicked`)
- File list item activation (`onRemoteFileActivated`)

MainWindow catches all three in one lambda that calls
`QDesktopServices::openUrl(QUrl(url))`. Same xdg-open
plumbing as 0.33.0's "Open folder in file manager".

### Languages bar rendering

`renderLanguagesBar()` walks the cached byte map sorted by
value desc. Each language becomes a `QFrame` with inline
stylesheet background color, stretch factor `pct × 100` (so
0.5% languages still get visible width), tooltip showing
percentage.

The 13-color palette in `kLangColors()` cycles for languages
beyond the table. Not strictly required — we could pull
GitHub's official colors from `linguist/languages.yml` — but
hardcoding the top ones is enough for visual recognition.

### Files

- Modified: `src/github/Repository.h` — 11 new struct fields,
  new `ContentEntry` struct, `Q_DECLARE_METATYPE` for both.
- Modified: `src/github/GitHubClient.{h,cpp}` — `fetchReadme`,
  `fetchContents`, `fetchLanguages` + matching signals; QMap
  include; expanded `parseRepo`.
- Modified: `src/ui/RepositoryDetailWidget.{h,cpp}` — substantial
  expansion. Three new tabs (README QTextBrowser, files
  QListWidget, About form), languages bar, caches, helper
  methods, openInBrowser flow.
- Modified: `src/ui/MainWindow.cpp` — six new connect() calls
  in `wireSignals()` for fetch dispatch + response routing +
  openInBrowser.
- Modified: `translations/github-manager_pl.ts` — 19 new strings.

### Known limitations

- **Files tab is one level deep.** Clicking a dir opens it in
  the browser rather than descending inline. Adding breadcrumb
  + back navigation is doable but adds maybe 100 lines for a
  feature that overlaps with what the browser does anyway.
- **README images don't render.** `QTextDocument` needs a custom
  resource handler to fetch them via HTTP. Skipped — markdown
  text is the meat of most READMEs.
- **No language colors for Brainfuck, COBOL, etc.** Anything past
  the 13-entry palette gets a recycled color. Functional but
  not strictly correct.
- **Cache is process-local.** No disk persistence. Closing the
  app forgets all cached READMEs. Acceptable for "preview while
  browsing" workflow — disk caching would need invalidation
  policy (push timestamps? ETag headers?) and isn't worth it.
- **No rate-limiting awareness.** Three fetches per repo
  selection × N repos browsed × M switches = potentially
  hundreds of API calls. GitHub gives 5000/hour for
  authenticated users so this is fine in practice but a power
  user clicking through 100 repos could notice.
- **Empty private repos** (no commits yet) might return 404 on
  README and 200 with empty array on contents. We handle both
  gracefully — empty hint texts, no error popups.

## [0.36.0] — 2026-05-16

### Added — partial DE/ES/FR localization

Three new languages join PL: **Deutsch**, **Español**, **Français**.
All three appear in Settings → Language and Translator picks them up.

This is **partial coverage**, not full localization:
- ~12% of strings translated per language (~145 common UI items
  out of ~614 unique strings)
- The translated items are the high-traffic vocabulary: buttons
  (Cancel/OK/Refresh/Remove…), menu items (File/Repository/
  Settings/Help…), tab titles (Changes/History/Remotes/
  Submodules), common labels (Name/URL/Visibility…), and
  recently-added features (visibility toggle, license picker,
  remembered keys management, F5 refresh, Copy SHA)
- Untranslated strings fall back to English at runtime, marked
  `type="unfinished"` in the .ts so Qt Linguist can flag them
  for future contributors

### Why partial

Full translation of 614 unique strings × 3 languages = 1842
entries of legal/Git/security-technical English-to-target-language
work. Doing it badly is worse than not doing it — wrong
terminology in a security-critical confirmation dialog could
mislead users. Doing it well requires native speakers familiar
with both Git terminology and the local conventions for tools
like file managers. That's out of scope for this sprint.

What we delivered is the **UI vocabulary** — predictable short
strings where the translation is unambiguous. Menus, button
labels, common form fields, status verbs. The long-form prose
(warning dialogs explaining the consequences of "Make public",
the license auto_init coupling note, etc.) stays in English
where the original phrasing was carefully chosen and where
miscommunication has real cost.

### How to add more translations

For contributors who want to fill in gaps:
1. Edit the relevant `translations/github-manager_<lang>.ts` in
   Qt Linguist or a text editor
2. Change `<translation type="unfinished"></translation>` to
   `<translation>actual translation</translation>`
3. Rebuild — `qt_add_translations` runs lrelease automatically

For complete pass: `cmake --build build --target
github-manager_lupdate` regenerates the .ts from current source,
then process the `<translation type="unfinished">` entries.

### Language selection

- **Settings → Language** now lists five options (English, Polski,
  Deutsch, Español, Français) as exclusive radio actions
- Switching is immediate; Translator reloads and Qt's
  `LanguageChange` event propagates to all retranslatable widgets
- Selection persists in QSettings (`appearance/language`) across
  sessions
- Translator falls back to English when a non-translated string
  comes up — no broken-looking placeholder text, just clean
  source-language fallback

### Architecture / files

- New: `translations/github-manager_de.ts` — German
- New: `translations/github-manager_es.ts` — Spanish
- New: `translations/github-manager_fr.ts` — French
- Modified: `src/core/Settings.{h,cpp}` — `supportedLanguages()`
  returns `{en, pl, de, es, fr}`, `languageDisplayName()` covers
  all five
- Modified: `CMakeLists.txt` — three new entries in `GHM_TS_FILES`
  (qt_add_translations runs lrelease for each at build time)

### Known limitations

- **Coverage is uneven across screens.** Settings menu and main
  toolbar feel mostly-localized in DE/ES/FR; the Publish dialog
  warnings and most error messages stay in English.
- **No regional variants** (de-AT, fr-CA, es-MX, etc.). Each
  language is one canonical form (de_DE, es_ES, fr_FR).
- **Translations are generated from a hardcoded lookup table**,
  not from a translator workflow. A native speaker review would
  catch awkward phrasing (e.g. "Push" stays "Push" because the
  Git term doesn't translate cleanly — debatable choice).
- **No RTL languages** yet (Arabic, Hebrew). Layout direction is
  hardcoded LTR throughout the codebase.

This brings the i18n backlog item to "done enough" — the
infrastructure is there, the framework loads .ts files for all
supported languages, and partial translations are better than
none for the high-traffic UI vocabulary.

## [0.35.0] — 2026-05-16

### Added — visibility toggle from GitHub sidebar context menu

Right-click any repo in the GitHub list → context menu with
"Make public…" or "Make private…" (label changes based on current
state). Confirmation dialog explains the consequences before
hitting the API.

### UX

The menu offers exactly one action per right-click, and its
label tells you what's about to happen:

```
private repo:                       public repo:
┌──────────────────────────┐        ┌──────────────────────────┐
│ Make public…             │        │ Make private…            │
└──────────────────────────┘        └──────────────────────────┘
```

After clicking, a confirmation dialog appears. Both paths warn
about the consequences, but the warnings differ:

- **private → public:** prominent warning to scrub credentials,
  API keys, and other sensitive material BEFORE confirming.
  Mentions BFG Repo-Cleaner / `git filter-branch` for history
  scrubbing. Default button is Cancel.
- **public → private:** lists what gets erased — stars,
  watchers, public-fork attachment, GitHub Pages — and notes
  that content/history is preserved. Default button is Cancel.

Both dialogs label the confirm button with the action verb
("Make private" / "Make public") rather than a generic "OK" so
the user can't autopilot through it.

### API

Wire-level: `PATCH /repos/{owner}/{repo}` with body
`{"private": <bool>}`. We use the `private` boolean rather than
the newer `visibility` enum because:
- The boolean is universally supported (no preview/Accept header)
- We don't currently support enterprise "internal" visibility
- The response is the same shape either way

New methods on `GitHubClient`:
- `updateRepositoryVisibility(fullName, makePrivate)` — fires
  the PATCH
- `patchJson(url, body, callback)` — private helper. QNAM has no
  first-class `patch()`, so this wraps `sendCustomRequest("PATCH",
  body)` with a QBuffer scoped to the reply lifetime.

New signal: `visibilityChanged(Repository)` carrying the
freshly-PATCH'd object (GitHub returns full repo on success).

### Cache patching

When the PATCH succeeds, `MainWindow::onVisibilityChanged`
patches `reposCache_` in-place by `fullName`:
- The matching entry is replaced with the response payload
- `localPath` (our client-side annotation) is preserved across
  the replacement — GitHub's response doesn't know about it
- The sidebar's `setRepositories()` is called with the updated
  list so any visibility-dependent styling refreshes

No round-trip to `/user/repos` — we trust the PATCH response.

### Architecture notes

The whole chain is small:
- `GitHubClient` gets the new method + signal
- `RepositoryListWidget` gets a context menu hook
  (`onGithubContextMenu`) symmetric to the existing local-list
  one. New signal `changeVisibilityRequested(repo, makePrivate)`
- `MainWindow` gets two new slots: `onChangeVisibilityRequested`
  (confirmation + dispatch) and `onVisibilityChanged` (cache
  patch + status)

The host owns the confirmation dialog. Widgets stay UI-only and
just emit intent — keeps testability of the widget intact (no
dialog to mock).

### Files

- Modified: `src/github/GitHubClient.{h,cpp}` —
  `updateRepositoryVisibility`, `patchJson`, `visibilityChanged`
  signal, `QBuffer` include.
- Modified: `src/ui/RepositoryListWidget.{h,cpp}` — GitHub list
  context menu, `onGithubContextMenu` slot,
  `changeVisibilityRequested` signal.
- Modified: `src/ui/MainWindow.{h,cpp}` —
  `onChangeVisibilityRequested` + `onVisibilityChanged` slots,
  cache patching, wire-up.
- Modified: `translations/github-manager_pl.ts` — 14 strings.

### Known limitations

- **No enterprise "internal" visibility.** GitHub Enterprise
  supports `visibility=internal` as a third option; we only
  toggle between `private: true/false`. Adding it would need a
  three-way dialog and runtime-detected GitHub-vs-Enterprise mode.
- **No bulk visibility change.** Each repo is one menu click +
  one confirmation. For users mass-managing many repos this gets
  tedious. Acceptable for the typical "fix one slip-up" case.
- **403 on visibility change** (org policy locked it down) just
  surfaces as `networkError` in the status bar. We don't try to
  re-prompt with org-admin guidance — the GitHub error message
  is usually clear enough.
- **No optimistic UI update.** The sidebar icon doesn't flip
  until the PATCH succeeds and we get the response. For a slow
  GitHub the menu might feel laggy. Trade-off for not having to
  roll back on failure.

## [0.34.0] — 2026-05-16

### Added — License & .gitignore template picker in Publish dialog

User reported: "program nie ma możliwości ustawienia czy repo ma byc
prywatne czy publiczne, dodatkowo nie ma mozliwosci ustawienia
licencji". Half-correct: private/public radio existed since forever
(visible at the top of the Create-new page). But there was no way
to pick a LICENSE or .gitignore template at publish time. Both now
present as comboboxes.

### What's new

Two new combobox rows in the Create-new page of
`PublishToGitHubDialog`, right under Visibility:

```
Owner:        tr9xpl
Name:         my-cool-project
Description:  …
Visibility:   ● Public   ○ Private
License:      [(none)             ▼]    ← NEW
.gitignore:   [(none)             ▼]    ← NEW
```

Both default to "(none)", matching the previous behavior. When the
user picks a real value, an info banner appears explaining the
auto_init coupling.

### License list

Hardcoded curated subset of GitHub's `/licenses` endpoint:
- MIT, Apache 2.0, GPL-3.0, GPL-2.0, LGPL-3.0, AGPL-3.0
- BSD 2-Clause, BSD 3-Clause
- MPL 2.0, Boost 1.0, Unlicense

Items carry both a human-readable display name (translated) and
the GitHub `license_template` key (e.g. `"mit"`) via
`QComboBox::currentData()`. We hardcode rather than fetch from
`/licenses` because (a) the list doesn't change, (b) it would add
a network call before the dialog could open.

### .gitignore list

Curated subset of GitHub's gitignore templates:
- C, C++, Go, Java, JavaScript (→ Node), Python, Ruby, Rust, Swift
- Kotlin (→ Java, closest match), TypeScript (→ Node)
- Visual Studio (.NET), Unity, Qt, CMake

Some entries map a friendly display name to a different GitHub key
(JavaScript and TypeScript both use the "Node" template since that's
what GitHub provides). The Kotlin → Java mapping is the closest
match; GitHub doesn't have a Kotlin-specific template.

### Why both forced auto_init=true

GitHub only accepts `license_template` / `gitignore_template` in
`POST /user/repos` when `auto_init=true`. This is because GitHub
creates the LICENSE / .gitignore as part of the **initial commit**;
there has to be one. The `GitHubClient::createRepository` method
now silently upgrades `autoInit` to `true` whenever either template
is provided:

```cpp
const bool needsInit = autoInit ||
                        !licenseTemplate.isEmpty() ||
                        !gitignoreTemplate.isEmpty();
```

### UX warning

The auto_init coupling has a practical implication: if the local
folder already has commits, those commits won't push cleanly onto
a remote that already has an initial commit from GitHub. The user
must `git pull --rebase` first. This is the same friction as
publishing a folder to an already-populated GitHub repo, but it
might surprise users who thought "create a new repo" meant "empty
remote".

The dialog's existing `createWarningLabel_` now also fires when
license or gitignore is non-empty:

> ℹ Adding a LICENSE or .gitignore will make GitHub create the
> repository with an initial commit. If your local folder already
> has commits, you'll need to run `git pull --rebase` before
> pushing (or uncheck "Push my commits after publishing" and
> resolve the merge yourself).

This warning is `Qt::RichText`-formatted so the inline `<code>`
renders monospaced.

### Architecture

The change ripples through five layers but each touch is small:

1. **`GitHubClient::createRepository`** — two optional QString
   parameters (`licenseTemplate`, `gitignoreTemplate`) at the end,
   defaulting to empty. JSON body inserts `license_template` /
   `gitignore_template` keys when non-empty.
2. **`PublishController::StartParams`** — two new `QString` fields.
3. **`PublishController::start()`** — forwards the new params into
   the `createRepository` call.
4. **`PublishToGitHubDialog`** — two new `QComboBox*` fields, form
   rows, accessors (`licenseTemplate()` / `gitignoreTemplate()`),
   warning logic in `refreshPreview` lambda triggered by combo
   `currentIndexChanged`.
5. **`MainWindow::onPublishToGitHubRequested`** — pipes the dialog
   accessors into `params.licenseTemplate` / `.gitignoreTemplate`.

### Files

- Modified: `src/github/GitHubClient.{h,cpp}` — extended signature,
  `needsInit` upgrade logic, JSON body inserts.
- Modified: `src/workspace/PublishController.{h,cpp}` — StartParams
  fields, forward call.
- Modified: `src/ui/PublishToGitHubDialog.{h,cpp}` — comboboxes,
  accessors, hardcoded lists, warning. `QComboBox` forward decl in
  header, include in cpp.
- Modified: `src/ui/MainWindow.cpp` — `params.licenseTemplate /
  .gitignoreTemplate` wire-up.
- Modified: `translations/github-manager_pl.ts` — 18 strings
  (including license display names — most kept canonical, only
  prose translated).

### Known limitations

- **License list is curated, not dynamic.** GitHub has ~30 license
  templates available; we offer 12 most-used. Niche licenses
  (CC0, EPL, MS-PL, etc.) require the user to add the LICENSE file
  manually after publish. Reasonable trade-off for dialog clarity;
  expanding the list is one constant array entry.
- **Gitignore mappings are heuristic.** Kotlin → Java isn't ideal
  (Java's gitignore is JVM-broad). TypeScript → Node assumes a
  Node toolchain. Users who need precise control can leave it as
  "(none)" and provide their own .gitignore in the local folder.
- **No preview of what the LICENSE/.gitignore will contain.** A
  popup or right-pane preview could be added but adds complexity;
  the user can always reverse the choice by editing/deleting the
  file after pull.
- **Auto_init upgrade is silent in the controller.** Callers that
  pass `autoInit=false` alongside a license get `auto_init=true`
  on the wire without explicit notification, but it's the only
  sensible thing to do (license_template needs auto_init). The
  dialog warning is the user-visible signal.

## [0.33.0] — 2026-05-16

### Added — drobne quick wins

**1. F5 as Refresh shortcut.** Standard file-manager / browser
keybinding now works in `LocalRepositoryWidget`. One widget-wide
`QShortcut` with `Qt::WidgetWithChildrenShortcut` scope dispatches
based on the active tab:
- Changes tab → working-tree status refresh (same as Ctrl+R)
- History tab → re-fetch commit log
- Remotes / Submodules → falls back to working-tree refresh

We use one shortcut + tab check rather than per-tab shortcuts
because `Qt::WidgetWithChildrenShortcut` scopes overlap across
sibling tabs (they all live inside the same `QTabWidget`) and
would conflict. Tooltips on the Refresh buttons mention both
keybindings.

**2. Right-click context menu on history rows.** Two entries:
- "Copy SHA" — full 40-char hash
- "Copy short SHA (a1b2c3d)" — first 7 chars, label shows the
  actual abbreviated SHA so users know exactly what they're
  about to paste

Both use `QGuiApplication::clipboard()`. Common workflow: paste
a commit reference into a bug tracker, chat, or `git checkout`
invocation without having to manually select-and-copy from the
row display text. Skips the "Load more…" sentinel row defensively.

**3. "Open folder in file manager".** New `Repository → Open
folder in file manager` menu entry plus Ctrl+E shortcut. Uses
`QDesktopServices::openUrl(QUrl::fromLocalFile(...))`, which
goes through xdg-open and respects the user's default file
manager (Nautilus, Dolphin, Thunar, etc.). Disabled until a
local folder is active, like the rest of the Repository menu.

### Implementation notes

- F5 was supposed to be added in two places (Changes + History
  refresh buttons) but `QPushButton::setShortcut` only carries
  one shortcut per button. We resolved by adding a separate
  `QShortcut` at the widget level. Tried per-button F5
  shortcuts first — they conflicted because both buttons live
  in sibling tabs under the same `QTabWidget`, and
  `Qt::WidgetWithChildrenShortcut` couldn't disambiguate.
- "Copy short SHA" label shows the actual hash so users with
  multiple commits in their History don't second-guess which
  one they'll paste. Trivial readability win.
- `Ctrl+E` chosen for Open Folder because "E" → Explorer.
  Alternatives considered: Ctrl+O (already conventional for
  "Open" elsewhere), Ctrl+Shift+E (longer combo). Ctrl+E is
  free and mnemonic.

### Files

- Modified: `src/ui/LocalRepositoryWidget.cpp` — F5 shortcut +
  history list context menu + 4 includes (QShortcut,
  QGuiApplication, QClipboard, and the existing QListWidget /
  QScrollBar already there). Refresh button tooltips updated.
- Modified: `src/ui/MainWindow.{h,cpp}` — `openFolderAction_`
  field/action, wire-up, enable-on-folder-active, includes
  (QDesktopServices, QUrl).
- Modified: `translations/github-manager_pl.ts` — 6 strings.

### Not done (deliberately skipped)

- **Sidebar tooltip with full path.** Started looking at it but
  the existing `pathLabel_` in the header already shows the
  full path in the active-repo view. Adding tooltips to sidebar
  rows would be more useful if the sidebar ever crops paths,
  which it currently doesn't. Deferred until there's a real
  need.

## [0.32.1] — 2026-05-16

### Fixed
- Build failure in 0.32.0: `LocalRepositoryWidget.cpp` calls
  `settings_->rememberedSubmoduleKey(...)` in the submodule
  context menu lambda but only had the forward declaration
  `class Settings;` (from the header). Method calls on a
  forward-declared pointer need the complete type.

  Fix: added `#include "core/Settings.h"` to
  `LocalRepositoryWidget.cpp`. Header stays with forward decl
  only (no `Settings` member fields, just a pointer), keeping
  compile-time exposure minimal.

  This is the same class of mistake as the QAbstractButton
  cases (0.20.1, 0.23.1) — forward-decl OK for pointer/ref
  declarations, complete type needed for method calls. Should
  have caught it before zipping; the read-only Settings access
  was a new pattern for this widget so its include
  requirements weren't established.

## [0.32.0] — 2026-05-16

### Added — remember SSH key per submodule

Dopełnienie of 0.29.0. The "Init/Update with explicit SSH key…"
context menu items now remember the user's key choice for each
(parent-repo, submodule-name) pair. Next time you right-click the
same submodule, the menu offers a reuse option that skips the
file picker.

### UX flow

**First time** on a deploy-key-protected submodule:
1. Right-click submodule → menu shows "Init && Update with
   explicit SSH key…"
2. SshKeyDialog pops, user picks `~/.ssh/deploy_acme_secret`
3. Worker runs init with that key; the path is saved in QSettings
   under `submoduleKeys/<sanitized-key>=<keyPath>`

**Next time** on the same submodule:
1. Right-click → menu shows "Init && Update with remembered key
   (deploy_acme_secret)"
2. SshKeyDialog still pops but with the key pre-filled and a
   contextual message; user only types passphrase (or hits Enter
   if unencrypted) and clicks OK
3. Worker runs with same key

**Forgetting:** the context menu also gains "Forget remembered
key" when a mapping exists. Click it → mapping wiped, next
explicit-key operation re-prompts.

### Settings → Remembered submodule keys…

New management dialog (`RememberedKeysDialog`) shows all saved
mappings in a table:

```
Parent repository   | Submodule       | SSH key path
────────────────────|─────────────────|──────────────────────
/home/u/proj-a      | acme-secret      | /home/u/.ssh/deploy_acme
/home/u/proj-a      | acme-other       | /home/u/.ssh/deploy_other
/home/u/proj-b      | shared-lib       | /home/u/.ssh/deploy_lib (missing)
```

Stale entries (key file moved/deleted) are flagged with "(missing)"
in italic. Per-row remove with confirmation dialog. Same pattern
as TrustedServersDialog from 0.31.0.

### Why no passphrase persistence

The mapping persists **only the key path**, never the passphrase.
Storing passphrases would require either:
- Plaintext in QSettings (bad — defeats the purpose of encrypting
  the key)
- An OS keyring entry per key (more work, deferred)

So encrypted keys still prompt for passphrase each operation. The
prompt comes with the key path pre-filled, so it's a one-field
form — much better than the original pick-key-and-passphrase
two-field dialog.

### Stale-entry handling

When `rememberedSubmoduleKey()` returns a path but `QFile::exists()`
on it is false (user moved their .ssh directory, deleted the key,
etc.), the explicit-key flow:
1. Surfaces a status bar message: "Remembered key ... not found
   on disk; please pick a new one"
2. Clears the stale mapping
3. Pops SshKeyDialog as if there was no remembered entry
4. Saves the new pick

No silent fallback to ssh-agent — the user asked for explicit key
mode, so we ask them again rather than quietly downgrading.

### Architecture changes

- **`ghm::core::Settings`** gains four methods:
  - `rememberedSubmoduleKey(parentPath, submoduleName)` → keyPath
  - `setRememberedSubmoduleKey(...)`
  - `clearRememberedSubmoduleKey(...)`
  - `allRememberedSubmoduleKeys()` → list of `RememberedKeyEntry`
    triples
- **Storage key format:** flat under `submoduleKeys/<id>` where
  `<id>` = `<sanitized_parent>\\\\<sanitized_submodule>`. `/`
  becomes `\` (QSettings reserves `/`), and `\\` separates the
  two components (reversible parse).
- **`LocalRepositoryWidget`** gains a read-only Settings pointer
  via `setSettings()`. Used only to drive context menu labels
  ("Init with remembered key (acme-deploy)" vs "Init with
  explicit key…"). Writes still flow through new signal
  `submoduleForgetRememberedKeyRequested` to MainWindow.
- **`SshKeyDialog`** gains `setMessage(QString)` for the
  contextual header line shown when reusing a remembered key.
- **`MainWindow`** consolidates the two explicit-key handlers
  (init and update) into one `dispatchExplicitKey` lambda that
  handles the remembered/fresh/stale branches uniformly.

### Files

- New: `src/ui/RememberedKeysDialog.{h,cpp}`
- Modified: `src/core/Settings.{h,cpp}` — submodule key API +
  `submoduleKeyId`/`parseSubmoduleKeyId` helpers.
- Modified: `src/ui/LocalRepositoryWidget.{h,cpp}` — `setSettings`,
  context menu split into remembered/explicit branches, new
  forget signal.
- Modified: `src/ui/SshKeyDialog.{h,cpp}` — `setMessage` + label.
- Modified: `src/ui/MainWindow.cpp` — `dispatchExplicitKey`
  lambda, forget signal handler, settings menu entry, include +
  setSettings call.
- Modified: `CMakeLists.txt` — two new sources.
- Modified: `translations/github-manager_pl.ts` — 26 strings.

### Known limitations

- **No bulk import/export** of remembered key mappings. Users who
  reinstall lose them; the conf file is the manual backup.
- **Path-only matching.** If the user has two copies of the same
  key file at different paths, they're treated as different keys
  in the mapping. Acceptable since key path is a stable identity
  for our purposes.
- **No automatic cleanup of orphan entries.** If a submodule is
  removed from the parent repo (`git submodule deinit && rm`),
  the saved key mapping stays behind. Listed in the management
  dialog so users can clean up manually. Auto-pruning would
  require walking every known parent repo on app start; not
  worth the cost.

## [0.31.0] — 2026-05-15

### Added — "Trusted TLS servers" settings page

Dopełnienie of 0.28.0. The TLS approval dialog from that sprint
let users approve self-signed/internal-CA certs with "Accept and
remember", which persisted the fingerprint in QSettings. There
was no UI to see WHAT was trusted or to revoke individual entries
— users had to edit `~/.config/<org>/github-manager.conf` by hand.

This version adds **Settings → Trusted TLS servers…** which opens
`TrustedServersDialog` showing:

```
┌────────────────────────────────────────────────────────────────┐
│ Manage trusted TLS servers                                     │
│                                                                │
│ Servers whose TLS certificate you've explicitly approved via   │
│ "Accept and remember"...                                       │
│                                                                │
│ Host                          | SHA-256 fingerprint            │
│ ──────────────────────────────|──────────────────────────────  │
│ git.internal.corp:8443        | a1:b2:c3:d4:e5:f6:...          │
│ self-signed.example.com        | ee:99:ff:00:11:22:...          │
│                                                                │
│ [Remove selected]                                  [Close]     │
└────────────────────────────────────────────────────────────────┘
```

### Behavior

- **Read-mostly:** entries only get created via the TLS approval
  dialog flow. No "Add manually" — typing a wrong fingerprint
  would defeat the safety mechanism.
- **Per-row remove:** select a row, click "Remove selected",
  confirm via warning dialog. The entry is wiped from QSettings
  immediately (no batch / cancel). The next connection to that
  host re-prompts for approval.
- **Empty state:** when no servers are trusted, the dialog shows
  an italic hint instead of an empty table.
- **Monospace fingerprints:** matches what `TlsCertApprovalDialog`
  shows, so eyes can compare without reformatting.
- **Tooltip on fingerprint cell:** carries the un-colonized hex
  for users who want to paste/diff against their own records.

### Settings API extension

New method on `ghm::core::Settings`:
- `allTrustedTlsFingerprints()` → `QList<QPair<host, sha256Hex>>`
  Iterates `QSettings::childKeys()` under the `trustedTlsFingerprints`
  group, sorts alphabetically by host for stable display.

### Confirm-before-remove

Single-click Remove pops a `QMessageBox::Warning` with default
button Cancel. The body explains the consequence ("next connection
re-prompts") and the threat model ("fingerprint change might be a
legit rotation OR an early MITM warning"). Wanted to make this
softly destructive — the impact is recoverable (just re-approve)
but worth a moment of thought.

### Files

- New: `src/ui/TrustedServersDialog.{h,cpp}` — modal dialog with
  table + remove flow.
- Modified: `src/core/Settings.{h,cpp}` — `allTrustedTlsFingerprints()`
  method.
- Modified: `src/ui/MainWindow.cpp` — Settings menu entry +
  include.
- Modified: `CMakeLists.txt` — two new sources.
- Modified: `translations/github-manager_pl.ts` — 14 new strings.

### Known limitations

- **Host names lose sanitization context.** When we save a
  fingerprint, the host's `:` and `/` are replaced with `_` for
  QSettings key safety. We can't reverse-map (which `_` came from
  which original char?), so the dialog shows the sanitized
  version. For typical hosts without ports (`github.internal.corp`)
  this matches the user's mental model. For hosts WITH ports
  (`git.example.com:8443`) the user sees `git.example.com_8443`.
  A future enhancement could store the original host in a sibling
  attribute, but it's low-impact — the user still recognizes the
  server.

- **No bulk remove / "Forget all".** The dialog is per-row. A
  "Reset all trust" button would be useful for the rare "I've
  changed environments and want to start fresh" case but adds
  cognitive load for the common case. Left out.

- **No export / backup.** Power users who reinstall might want to
  export their trust list and re-import. Out of scope; the conf
  file itself is the export.

## [0.30.0] — 2026-05-15

### Changed — viewport-only signature verify dispatch

Performance dopełnienie of 0.25.0. Signature verification is now
dispatched ONLY for commits currently visible in the History tab
viewport, not for every commit returned by `log()`.

### Before this version

`setHistory(commits)` and `appendHistory(commits)` iterated the
entire commit vector and emitted `verifyCommitSignatureRequested`
for every signed commit not already in `sigCache_`. The worker
queued these sequentially and chewed through them in the
background. For a 1000-commit history of mostly-signed commits
on cold start (gpg keyring locked, every verify prompts for
passphrase): the user gets 1000 passphrase prompts.

### After this version

Verify dispatch happens via `dispatchVerifyForVisibleRows()`,
which iterates the list and emits ONLY for rows whose
`visualItemRect` intersects the viewport rect. The first
dispatch (right after `setHistory`) covers maybe 20-30 rows
depending on window height. Scrolling fires further dispatches
through a 150ms-debounced scrollbar valueChanged hook.

For a 1000-commit history: ~20 verifies on initial load instead
of 1000. Scrolling adds verifies incrementally as the user
explores the history. Verifies past commits the user never
looked at: zero.

### Plumbing

- New private method `dispatchVerifyForVisibleRows()`.
- New slot `onHistoryScrolled()` — calls
  `verifyDebounce_->start()` (single-shot 150ms QTimer).
- The debounce timer connects to `dispatchVerifyForVisibleRows`.
- `historyList_->verticalScrollBar()::valueChanged` →
  `onHistoryScrolled`.
- `setHistory` and `appendHistory` end with
  `dispatchVerifyForVisibleRows()` instead of the old
  iterate-all-commits emit loop.
- Filter changes (`onHistoryFilterChanged`) also restart the
  debounce timer because hiding/unhiding rows changes which set
  is "visible".

### Why debounce

Scrollbar `valueChanged` fires once per pixel during scrolling.
Without debouncing, a Page Down keystroke that scrolls 30 rows
would emit 30 dispatch calls, each iterating the whole list.
150ms quiet period coalesces bursts into a single dispatch.

The trade-off: a fast scroll past signed commits won't badge
them mid-scroll. That's fine because the user isn't looking at
mid-scroll content anyway.

### Visibility check

`historyList_->visualItemRect(item)` returns the row's pixel
rect in the viewport's coordinate system. `viewport()->rect()`
is the viewport's rect (origin 0,0). Intersect → visible.
`isEmpty()` catches rows not yet laid out (can happen briefly
after appendCommitRow before the list pixmaps).

### Known limitations

- **Window resize** doesn't trigger redispatch. If the user
  enlarges the window and new rows become visible at the
  bottom, they won't get badge'd until the user scrolls (or
  filter changes). Acceptable: badges show as "pending" until
  next interaction, and resize-without-scroll is rare for
  History inspection. A `QResizeEvent` override would fix it
  but adds complexity for negligible gain.

- **`dispatched` counter is unused**. Kept as a `(void)dispatched`
  in the helper to make instrumentation easy if we ever want
  to log hit rates.

- The pre-existing `hasSignature` check happens BEFORE the
  viewport check, but since the viewport check is cheaper
  (O(1) per row vs nothing for has_sig), reordering doesn't
  materially help.

### Files

- Modified: `src/ui/LocalRepositoryWidget.h` — new private
  method/slot decl, `verifyDebounce_` QTimer field.
- Modified: `src/ui/LocalRepositoryWidget.cpp` — timer init,
  scrollbar wire, `dispatchVerifyForVisibleRows` + 
  `onHistoryScrolled` impls, replacement of the iterate-all
  emit loops in `setHistory`/`appendHistory`, dispatch trigger
  in `onHistoryFilterChanged`.

No new translations — purely an internal performance change.

## [0.29.0] — 2026-05-15

### Added — explicit SSH key for existing submodules

Right-click a submodule row in the Submodules tab → context menu:
```
Init && Update
Update
─────────────────────────
Init && Update with explicit SSH key…
Update with explicit SSH key…
```

The plain "Init && Update" / "Update" entries (also available as
the row-action buttons below the table) use ssh-agent — same as
before. The "with explicit SSH key…" variants pop `SshKeyDialog`,
let the user pick a specific private key file (and passphrase if
encrypted), and forward that to the worker as an `SshCredentials`
value.

### Use case

Submodules that use **deploy keys** scoped to one specific
repository. Example:
- Parent repo `acme/platform` uses your main personal SSH key
  loaded in ssh-agent
- Submodule `acme/secret-service` is locked behind a deploy key
  scoped only to that repo — your main key can't access it

Before this version, both ops shared ssh-agent and the submodule
init would fail with `git_credential_acquire` errors. Now the user
picks the deploy key explicitly for that one submodule.

### Architecture — minimal addition

This sprint dopełnia 0.23.0. The handler/worker layer already had
the `*WithCreds` variants (and even the regular `initAndUpdateSubmodule`
/ `updateSubmodule` worker methods accept `SshCredentials` since
0.23). What was missing was the UI plumbing to actually use them
for existing submodules — only the Add Submodule flow had the
SshKeyDialog hook.

Three additions wire this through:

1. **Widget** (`LocalRepositoryWidget`) — context menu on
   `submoduleTable_` with two extra entries. Emits new signals
   `submoduleInitWithExplicitKeyRequested` and
   `submoduleUpdateWithExplicitKeyRequested`.

2. **MainWindow** — two new `connect()` calls that pop
   `SshKeyDialog`, assemble `SshCredentials`, and call
   `worker_->initAndUpdateSubmodule(...)` /
   `worker_->updateSubmodule(...)` with the creds.

3. **PL translations** — 7 new strings (menu items, tooltips, busy
   status messages).

### Why not auto-detect?

Considered: try ssh-agent first, on `GIT_EAUTH` failure pop the
key dialog. Rejected because (a) the failure path requires a retry
mechanism the worker doesn't have, (b) explicit-only is clearer
about intent — when a user reaches for the "explicit key" menu
item they know exactly what they want, no surprise prompts.

Also considered: prompt every time. Rejected because most
submodules work with ssh-agent and would mean unnecessary friction.

The chosen design (context menu, opt-in per operation) is the
least-invasive plumbing that solves the specific problem. The
plain Init/Update buttons keep their existing behavior — no
regression for users who don't have deploy-key submodules.

### Files

Modified only:
- `src/ui/LocalRepositoryWidget.h` — two new signals
- `src/ui/LocalRepositoryWidget.cpp` — context menu on
  `submoduleTable_`
- `src/ui/MainWindow.cpp` — two new `connect()` lambdas
- `translations/github-manager_pl.ts` — 7 strings

No handler or worker changes — the API was already there from 0.23.

### Known limitations

- The chosen key is **not remembered** for next time. If you
  routinely use the same deploy key for the same submodule, the
  prompt re-fires each operation. A future "remember key per
  submodule" feature would store the mapping in QSettings keyed
  by submodule name; out of scope for this sprint.
- No mass "Init all with explicit key" — the existing "Update all"
  button (which iterates all submodules) still uses ssh-agent.
  Mixed-key parents would need to invoke each submodule
  individually. Acceptable for typical "1-3 submodules with deploy
  keys" projects.

## [0.28.0] — 2026-05-15

### Added — TLS certificate approval UI

Connections to git servers whose TLS certificate doesn't validate
against the system trust store (self-signed, internal CA, MITM
proxies) now pop an interactive approval dialog instead of
silently failing.

The dialog shows:
- **Hostname** (prominent)
- **Subject** (CN, O parsed from the cert)
- **Issuer** (CN, O)
- **Valid from / until** dates
- **SHA-256 fingerprint** (the canonical thing to verify against
  what your server admin tells you out-of-band)
- **SHA-1 fingerprint** (for legacy verification flows)

Three outcomes:
- **Reject** (default, Enter key) — connection aborts
- **Accept once** — proceeds this time, re-prompts next time
- **Accept and remember** — proceeds AND saves the SHA-256 in
  QSettings under `trustedTlsFingerprints/<host>`. Future
  connections with the same fingerprint pass silently. A
  fingerprint change re-prompts.

### Architecture mirrors HostKeyApprover

This builds on the SSH host-key approval pattern from 0.22.0:
- `TlsCertApprover` is a singleton owned by MainWindow on the GUI
  thread. The libgit2 cert callback (on the worker thread) reaches
  it via `QMetaObject::invokeMethod(..., BlockingQueuedConnection)`.
- `TlsCertApprovalDialog` is a plain QDialog that parses the DER
  bytes using `QSslCertificate::fromData` for human-readable
  fields, and hashes them with `QCryptographicHash` for
  fingerprints.

### libgit2 wire-up

The existing `certificateCheckCb` already handled `GIT_CERT_HOSTKEY_LIBSSH2`.
It now also handles `GIT_CERT_X509`:

1. Extract DER bytes from `git_cert_x509::data`
2. Compute SHA-256 fingerprint on the worker thread
3. Quick path: if fingerprint already in trusted list → proceed
   silently (no dialog)
4. Slow path: route to GUI thread, show approval dialog

The quick path matters: once a server is approved, every
clone/fetch/push to it is silent. We don't want a dialog flash on
every operation.

### QNetworkAccessManager wire-up

`GitHubClient::sslErrors` handler does the same flow for HTTPS
requests (GitHub API, OAuth device flow):

1. Extract peer cert via `reply->sslConfiguration().peerCertificate()`
2. Same fingerprint quick-path / dialog slow-path
3. On approval, `reply->ignoreSslErrors(errors)` lets the request
   proceed

This is inline (no invokeMethod) because QNAM signals already fire
on the GUI thread.

### Settings extension

New API on `ghm::core::Settings`:
- `trustedTlsFingerprint(host)` → SHA-256 hex
- `setTrustedTlsFingerprint(host, sha256Hex)`
- `clearTrustedTlsFingerprint(host)` (not yet exposed in UI; reserved
  for a future "Manage trusted servers" settings page)

Storage layout in QSettings:
```
[trustedTlsFingerprints]
github_example_com=a1b2c3d4...
git_internal_corp_lan_8443=ee99ff00...
```
Hostnames are sanitized (slashes/colons → underscores) so port
suffixes like `host:8443` work as QSettings keys.

### Files

- New: `src/ui/TlsCertApprover.{h,cpp}`
- New: `src/ui/TlsCertApprovalDialog.{h,cpp}`
- Modified: `src/git/GitHandler.cpp` (X.509 branch in cert callback)
- Modified: `src/github/GitHubClient.{h,cpp}` (sslErrors handler)
- Modified: `src/core/Settings.{h,cpp}` (trustedTls API)
- Modified: `src/ui/MainWindow.{h,cpp}` (approver construction)

### PL translations
- 19 new strings for the approval dialog (heading, button labels,
  tooltips, field labels).

### Security model

This feature trades safety for compatibility. The default policy
is still "system trust store decides"; the dialog is opt-in
override per server. Risks the user takes by clicking "Accept and
remember":
- A legitimate cert rotation by the server admin → re-prompt
  (treated correctly)
- A MITM attacker successfully replacing the cert → silent pass
  (treated as compromise; user has only the initial out-of-band
  fingerprint verification to defend against this)

We do NOT support "trust all certs" globally — every approval is
per-(host, fingerprint) tuple. This matches what `git config
http.sslVerify false` does, but per-server and visible.

### Known limitations

- No "Manage trusted servers" settings page yet. Users can clear
  individual entries by editing `~/.config/<orgname>/github-manager.conf`
  manually. Future work.
- Approver isn't invoked for libgit2's `GIT_CERT_NONE` path (some
  exotic transports). Rare in practice.
- The QNAM approval dialog runs inline on the GUI thread. While
  it's open, other network requests in the same NAM are paused.
  Acceptable because dialog latency is bounded by user click.

## [0.27.0] — 2026-05-15

### Added — `tokenType` metadata in SecureStorage

Stored credentials now carry a `tokenType` attribute distinguishing
PAT (Personal Access Token, manually entered) from OAuth (device-
flow). The schema gains a third attribute alongside `username` and
`service`. Backward-compatible: legacy items from before 0.27.0
don't have the attribute and load as `TokenType::Unknown`.

**API changes (breaking for SecureStorage callers):**
- `saveToken(user, token)` → `saveToken(user, token, TokenType)`
- `loadToken()` returns `std::optional<LoadedToken>` (struct with
  `token` + `type`) instead of `std::optional<QString>`. Added
  `loadTokenString()` as a thin wrapper for callers that don't
  care about type.
- New enum `ghm::core::TokenType { Unknown, Pat, Oauth }`.

**Why now:** the OAuth device flow has been wired since 0.21.0 but
the credential storage layer was treating OAuth tokens identically
to PATs. That hid two pieces of information from the UI: (1) which
auth mechanism is currently in use, and (2) which lifecycle
expectations apply (OAuth ~1h, PAT ~90d). This unblocks future
work like auto-refresh (only meaningful for OAuth) and
mechanism-specific logout flows.

**LoginDialog already had `tokenIsOAuth()`** — that's been sitting
since the OAuth sprint but `SessionController` ignored it and
saved every token as if it were a PAT. The wire-up is finally
complete in this version.

### Added — auth mechanism tooltip

The "Signed in as X" label in the toolbar gains a tooltip
explaining how the user authenticated:
- OAuth → "Authenticated via GitHub OAuth (device flow). Tokens
  issued this way are tied to your GitHub account and can be
  revoked from your GitHub settings → Applications page."
- PAT → "Authenticated via Personal Access Token. Manage this
  token at GitHub → Settings → Developer settings → Personal
  access tokens."
- Unknown (legacy) → "Authenticated (token type unknown — legacy
  credential from an older version of this app)."

### Implementation notes

- libsecret's `secret_password_lookup_sync` only returns the
  password string, not attributes. To read `tokenType` back we
  switched the `loadToken` path to `secret_service_search_sync` +
  `secret_item_get_attributes`. More verbose (lots of GLib
  ref-counting boilerplate) but it's the only way to round-trip
  attributes.
- `saveToken` still uses the simpler `secret_password_store_sync`,
  but explicitly calls `clear_sync` first to avoid creating a
  second item when re-saving with a different `tokenType` (libsecret
  treats the full attribute tuple as the key for an item).

### PL translations
- 3 new tooltip strings translated for the auth mechanism label.

### Existing migration
- Tokens stored by earlier versions of this app are still readable
  — they just come back with `TokenType::Unknown` and the UI shows
  the "legacy credential" tooltip. Users can re-sign-in to get the
  current mechanism written into the keyring.

## [0.26.0] — 2026-05-15

### Added

**Match counter for History tab filter.** The "Filter commits"
field now shows a "N of M" counter on the right side:

```
Filter commits by summary, SHA, or author…   [fix]        3 of 47
```

Three states:
- Empty filter → counter is blank (no clutter)
- Filter typed, some matches → "3 of 47"
- Filter typed, no matches → "no matches"
- Filter typed, all rows match → "47 matches" (cleaner than "47 of 47")

Implemented as a `QLabel` placed in a horizontal row alongside the
existing `commitFilter_` QLineEdit. The handler
`onHistoryFilterChanged` now counts visible vs. total commit rows
(excluding the "Load more…" sentinel) and updates the label.

### Note on diff viewer

The DiffViewWidget already has "X of Y" matches in its SearchBar
(visible when the user activates Ctrl+F). That was wired up in an
earlier sprint via `SearchBar::setMatches(current, total)`. The
backlog item from the 0.25 retrospective ("search count in diff
viewer") turned out to refer to the History tab filter, which had
no counter — that's what this version adds.

### PL translations
- No new strings — "%1 of %2", "%1 matches", "no matches" were
  already in the `LocalRepositoryWidget` context from earlier
  search-related work.

## [0.25.2] — 2026-05-15

### Fixed
- Two warnings from 0.25.1 (build succeeded but emitted them):
  - `GitHandler.cpp:2671` — dead `if (const git_signature* committer
    = …)` block inside `verifyCommitSignature`. Leftover from a
    refactor where I first sketched out a committer lookup then
    wrote the real one below it (`const git_signature* c`).
    Removed the dead block; the real lookup stays.

  - `SigningSettingsDialog.cpp:251` — `using SM =
    ghm::core::Settings::SigningMode` typedef that nothing uses.
    The function works in raw combobox indices (1/2) rather than
    the enum, so the typedef was dead. Removed.

- Bonus: `verifyCommitSignature` now reports an explicit Invalid
  verdict if `git_commit_lookup` fails (couldn't load the commit
  object), rather than silently leaving the result default-
  constructed. Defensive change for edge cases like SHA pointing
  at a pruned commit.

## [0.25.1] — 2026-05-15

### Fixed
- Build failure in 0.25.0: orphaned code block in
  `LocalRepositoryWidget.cpp` between `setSignatureVerifyResult`
  and `setIdentity`. When I did the str_replace that inserted
  `setSignatureVerifyResult` into the appendHistory area, I left
  five lines of leftover appendHistory body (filter-reapply check)
  AFTER `setSignatureVerifyResult`'s closing brace. The extra `}`
  closed `namespace ghm::ui` early, so all the methods further down
  in the file were parsed in global scope — which is why the
  compiler complained that `LocalRepositoryWidget` "has not been
  declared" for methods starting from `onUnstageSelectedClicked`.

  Fix: removed the orphaned 5 lines. Brace structure now: anonymous
  namespace 34→93, ghm::ui namespace 32→2044, all methods inside.

  This is the second cleanup-after-multi-edit miss in the project
  (first was 0.20.1 forward-decl). When doing a large
  `str_replace` that subsumes multiple original blocks, I should
  scan the diff for the closing context after the replacement to
  catch leftover lines like these. Mea culpa.

## [0.25.0] — 2026-05-15

### Added — signature verification badges in History tab

Each commit row in the History tab now shows a signature badge prefix:
- **✓** Verified — signature valid, key trusted (GPG: ultimate/full
  trust; SSH: identity present in allowed_signers file)
- **◐** Signed — signature valid but key trust unknown (GPG:
  marginal/unknown trust; SSH: no allowed_signers configured)
- **✗** Invalid — signature doesn't match commit body, OR key was
  revoked/expired/not in keyring, OR the verifier subprocess failed
- **·** Pending — commit has a signature header but verification
  hasn't completed yet
- (no badge) — commit is unsigned

Tooltip on each row explains the verdict, including the signer's
identity for verified commits and the failure reason for invalid ones.

**Lazy verification.** The `log()` handler call cheaply checks
whether each commit has a `gpgsig` header (one libgit2 call, no
subprocess) and stores it in `CommitInfo::hasSignature`. The widget
displays a "pending" badge for those, then dispatches one
`verifyCommitSignature` worker call per signed commit. Results
stream back asynchronously and update each row in-place. Caching
(`QHash<sha, VerifyResult>` on the widget) means re-scrolling or
re-loading history reuses previous verifications.

For a 200-commit history with all-signed commits, this means roughly
3-15 seconds of background verification work after the initial load.
Each row updates as its result arrives — the UI never blocks.

### Added — auto-detect signing config from git config

`SigningSettingsDialog` gets an **Import from git config** button
that reads three keys from the global `~/.gitconfig`:
- `gpg.format` — "openpgp" (default) or "ssh"
- `user.signingkey` — key ID for GPG, key path for SSH
- `commit.gpgsign` — bool, currently read for future use

Mode is detected by:
1. `gpg.format == "ssh"` → SSH mode
2. `user.signingkey` contains '/' → SSH mode (path heuristic)
3. otherwise → GPG mode (only if `user.signingkey` is non-empty)

SSH paths starting with `~/` are expanded to the home directory.
Fields not present in git config are left untouched — no surprise
wipes of what the user already typed.

### Added — tag signing (GPG)

`createTag` now accepts a `SigningConfig` parameter and signs
annotated tags when the mode is GPG. **SSH tag signing is not
supported** — libgit2 has no SSH-tag-aware API, and manual byte
construction with `sshsig` namespace handling is out of scope.
The user gets a friendly error suggesting `git tag -s` from the CLI.

Tag signing uses a different flow than commit signing because
libgit2 lacks `git_tag_create_with_signature`:
1. `git_tag_annotation_create` writes the unsigned tag object
2. `git_odb_read` extracts the raw bytes
3. `CommitSigner::signWithGpg` produces the GPG signature
4. We append `\n<signature>\n` to the raw bytes (git's format —
   tag signatures live in the body, not a header)
5. `git_tag_create_from_buffer` writes the final signed tag AND
   creates the `refs/tags/<name>` reference

The intermediate unsigned annotation object becomes garbage and
gets pruned by `git gc` over time. Not ideal but the alternative
(implementing tag-buffer-create from scratch) is much more risk.

### Internal

- New files:
  - `src/git/SignatureVerifier.{h,cpp}` — verify subprocess wrappers
    for both GPG (`gpg --verify --status-fd=2`) and SSH
    (`ssh-keygen -Y verify` with `allowed_signers`, or
    `-Y check-novalidate` fallback when no allowed_signers).
- New worker method `verifyCommitSignature(path, sha, allowedSigners)`
  and signal `signatureVerified(path, sha, VerifyResult)`.
- New widget method `setSignatureVerifyResult(sha, result)` that
  updates the row in-place by stripping any old badge prefix and
  prepending the new one + updating tooltip.
- `MainWindow::sshAllowedSignersPath()` reads
  `gpg.ssh.allowedSignersFile` from libgit2's default config
  (global git config), expanding leading `~/` to `$HOME`. Empty
  when unset — SSH verifier falls back to check-novalidate then.
- `CommitInfo::hasSignature` populated unconditionally in `log()`
  via `git_commit_extract_signature` — cheap, no subprocess.

### "Signing for revert/cherry-pick/merge" (mini-sprint 3)

**Zero-effort.** The app currently has no revert or cherry-pick
operations, and `pull()` is fast-forward only (no merge commits
created). All commit creation goes through `GitHandler::commit()`,
which already signs since 0.24.0. Nothing to plumb. If we later add
revert/cherry-pick handlers, they'll need the same
`SigningConfig`-forward refactor.

### PL translations
- 6 new strings across `SigningSettingsDialog` (Import button +
  tooltip) and `LocalRepositoryWidget` (verify status tooltip lines).

### Known limitations
- **Verification per-commit cost** — gpg/ssh-keygen subprocesses
  aren't cheap, ~10-30ms each on a warm box. For very large
  histories (thousands of signed commits) the worker takes minutes
  to chew through. The cache helps within a session but cold start
  is slow.
- **No "verify all visible" button** — currently we verify every
  signed commit returned by `log()`, including ones the user
  scrolls past quickly. Could be smarter with viewport tracking.
- **SSH tag signing** — declined up-front with explanation.

## [0.24.0] — 2026-05-14

### Added — commit signing (GPG / SSH)

- **GPG and SSH commit signing.** New Settings → Commit signing dialog
  with three modes:
  - **None** (default) — commits go out unsigned, same as before
  - **GPG** — `gpg --status-fd=2 -bsau <key>` produces the signature
  - **SSH** — `ssh-keygen -Y sign -f <key> -n git` produces it

  When enabled, every commit through the app is signed; if signing
  fails (gpg not installed, no agent, wrong key ID, encrypted key
  without pinentry), the commit fails too. **No silent fallback to
  unsigned.** A "signed-only branch" expectation shouldn't be quietly
  broken by a missing pinentry.

- **GitHub recognises both formats** and shows "Verified" badge on
  signed commits. Pure cosmetic on GitHub's side (still the same
  commit SHA either way), but for projects requiring signing it's
  enforceable via branch protection rules.

### Internal — signing pipeline

libgit2 does NOT sign commits itself. It exposes
`git_commit_create_buffer()` which gives the unsigned commit bytes,
and `git_commit_create_with_signature()` which takes a pre-computed
signature. We spawn `gpg`/`ssh-keygen` as subprocesses between the
two, using `QProcess` with a 30-second timeout per call.

- `src/git/CommitSigner.{h,cpp}` — encapsulates the subprocess
  dance. Two static methods, `signWithGpg(buffer, keyId)` and
  `signWithSsh(buffer, keyPath)`. Each returns a `SignResult` with
  either signature bytes or a human-readable error. No shared state,
  thread-safe by construction.

- `GitHandler::commit` is rewritten with two paths:
  - **Unsigned path**: unchanged — single `git_commit_create()`
    that writes the commit and moves HEAD atomically.
  - **Signed path**: 4 steps:
    1. `git_commit_create_buffer` → unsigned commit bytes
    2. `CommitSigner::signWithGpg/Ssh` → signature
    3. `git_commit_create_with_signature` → write signed commit object
    4. Manually update HEAD (the signed-write call doesn't move it)
       via `git_reference_set_target` (or `git_reference_create`
       for the unborn-HEAD first-commit case)

  The signed path costs roughly 100-500ms more than unsigned (the
  subprocess invocation). Pinentry/passphrase prompts add more.

- The signature header in both modes is "gpgsig" — yes, even SSH
  signatures live there. Git decided not to introduce a new header
  for SSH, for backward-compat with older verifiers that read
  "gpgsig" unconditionally.

### Settings

- `Settings::SigningMode` enum (`None` / `Gpg` / `Ssh`) plus
  `signingKey()` (key ID for GPG, path for SSH). Stored under
  `git/signingMode` and `git/signingKey`.

- `SigningSettingsDialog` (modal) with mode combobox and per-mode
  detail pages (`QStackedWidget`):
  - **None page** — just an info label
  - **GPG page** — key ID line edit with hint about `gpg --list-secret-keys`
  - **SSH page** — key path line edit + Browse… button defaulting to ~/.ssh

  We deliberately don't pre-test the signing setup at OK time (a
  test sign would pop pinentry from a settings change, which is
  intrusive and would block silently on headless hosts). Validation
  happens at commit time with specific error messages.

### Wiring
- `GitHandler::commit` takes an optional `SigningConfig` parameter
  (default empty struct = unsigned, preserves existing callers).
- `GitWorker::commitChanges` accepts the same, forwards to handler.
- `LocalWorkspaceController::commitWithKnownIdentity` reads the
  signing config from `Settings` and passes it through.
- `MainWindow` adds a "Commit signing…" entry under the Settings
  menu (between "Prefer SSH for new clones" and Help menu).

### Deliberately out of scope (for now)
- **Verify signatures on existing commits** — would need a separate
  pass reading the gpgsig header of each commit in History, running
  `gpg --verify` / `ssh-keygen -Y verify` per commit, displaying
  "Verified"/"Unverified" badges. Useful but a separate sprint.
- **Auto-detect key** from git config (`user.signingkey`,
  `commit.gpgsign`) — explicit beats implicit; auto-detection makes
  signing failures harder to diagnose. May add as a one-click
  "Import from git config" button later.
- **Signing for revert/cherry-pick/merge** — only regular commits
  go through the signed path. The other commit-creating ops still
  use unsigned `git_commit_create`. Will plumb when the need arises.
- **Sign tag** (`git tag -s`) — TagsDialog from 0.13 creates
  unsigned tags only. Tag signing is much less commonly used than
  commit signing in practice.

### PL translations
- 20 new strings across `SigningSettingsDialog` and `MainWindow`
  contexts.

## [0.23.1] — 2026-05-14

### Fixed
- Build failure in 0.23.0: `MainWindow.cpp` called
  `confirm.button(QMessageBox::Yes)->setText(...)` to relabel the
  destructive-action button to "Remove". `QMessageBox::button()`
  returns a `QAbstractButton*`, and Qt6's `<QMessageBox>` only
  forward-declares that class. Calling `setText()` on it needs
  the full type, so we now `#include <QAbstractButton>` explicitly.

  Same incomplete-type pattern as the 0.20.1 fix — Qt headers often
  forward-declare related types to keep compile times down, leaving
  the call site responsible for the full include when it actually
  uses methods on returned objects.

## [0.23.0] — 2026-05-14

### Added — submodule add/remove

- **Add submodule.** New "Add submodule…" button at the top of the
  Submodules tab. Opens a dialog with two fields (URL + path within
  the repo) and an optional "Use explicit key file" checkbox that
  appears when the URL looks like SSH. On accept, the parent repo
  gets a new `.gitmodules` entry, the subrepo is cloned into the
  target path, and the gitlink is staged in the parent's index.
  The user still has to commit the `.gitmodules` change and gitlink
  themselves (same as command-line `git submodule add`).

- **Remove submodule.** New "Remove…" button per row (styled in
  muted red). Shows a confirmation dialog explaining the five things
  the operation will do, with the default button set to Cancel.
  On confirm:
  1. Removes the `submodule.<name>.*` keys from `.gitmodules`
  2. Removes them from `.git/config`
  3. Removes the gitlink from the index (staging the deletion)
  4. Deletes `.git/modules/<name>/` (the embedded subrepo)
  5. Deletes the submodule's workdir directory

  libgit2 doesn't expose a single `git submodule rm` call, so all
  five steps are done explicitly. Each step ignores errors from
  steps that don't apply (e.g. `.gitmodules` might not exist if
  it was already partially removed) — so partial-state cleanup is
  forgiving.

### Added — per-submodule explicit SSH credentials

- `SshCredentials` is now plumbed through the submodule update path
  as well as clone. New handler methods
  `initAndUpdateSubmoduleWithCreds` / `updateSubmoduleWithCreds`
  accept an `SshCredentials` parameter; the old no-creds methods
  become thin delegates that pass an empty struct (preserving
  ssh-agent behaviour for callers that don't want to plumb keys).

- **Add submodule** uses this path: when the user checks "Use
  explicit key file" in `AddSubmoduleDialog`, MainWindow then pops
  an `SshKeyDialog` to collect the key path + passphrase before
  dispatching the worker. Same UX as `CloneDialog` from 0.20.

- **Init/update of existing submodules** still uses ssh-agent only
  for now — the Submodules tab buttons don't offer a key dialog.
  Users with encrypted keys can `ssh-add` first, or add the
  submodule fresh through the new "Add submodule…" flow with the
  explicit-key checkbox.

### Internal

- Refactored `initAndUpdateSubmodule` / `updateSubmodule` impls in
  `GitHandler.cpp` to share a private `doSubmoduleInitUpdate()`
  helper. Previously they duplicated the libgit2 dance
  (lookup → init → set up CallbackCtx → set update_options →
  call git_submodule_update). The helper takes a `runInit` bool
  to discriminate the two cases.

- New `src/ui/AddSubmoduleDialog.{h,cpp}` — minimal modal with URL
  + path fields, SSH detection heuristic (matches `git@` or
  `ssh://` prefix), and accessors `url()` / `subPath()` /
  `useExplicitKey()`.

- `MainWindow::onSubmoduleOpFinished` extended to handle "add" and
  "remove" operations. Success messages remind the user to commit
  the resulting changes (since both ops leave staged changes
  rather than auto-committing).

- `MainWindow` gains two new slots: `onSubmoduleAddRequested`
  (spawns AddSubmoduleDialog + optional SshKeyDialog, dispatches
  worker) and `onSubmoduleRemoveRequested` (confirms with
  destructive-action dialog, dispatches worker).

- `LocalRepositoryWidget` gets a third button on the tab header
  (Add submodule…) and a fourth per-row action button (Remove…).
  Two new signals: `submoduleAddRequested(path)` and
  `submoduleRemoveRequested(path, name)`.

### PL translations
- 21 new strings across `AddSubmoduleDialog`,
  `LocalRepositoryWidget`, and `MainWindow` contexts. Confirmation
  dialog text translated in full (multi-line bullet list).

### Still deferred
- **Recursive submodules** — libgit2 has no native recursive
  update; would need manual recursion with depth limit. Niche use
  case. Skipped.
- **Per-submodule explicit keys for init/update** — currently only
  the "Add" flow offers the explicit-key dialog. Init/update buttons
  use ssh-agent. Easy to plumb when needed.
- **Undo for Remove** — the destructive op is genuinely destructive
  (workdir deletion). Recovery requires a fresh clone with the
  original URL. The confirmation dialog says so explicitly.

## [0.22.1] — 2026-05-13

### Fixed
- Build failure in 0.22.0: `GIT_SUBMODULE_STATUS_WD_URL_MISMATCH`
  doesn't exist in libgit2. I hallucinated the constant from
  guessing what should be there. The actual `git_submodule_status_t`
  enum has no URL-mismatch flag — `git_submodule_url()` returns the
  `.gitmodules` value, and the `.git/config` value has to be read
  separately via `git_config_get_entry("submodule.<name>.url")`.

- URL mismatch detection now done manually: for each submodule, we
  open a config snapshot and read the per-submodule URL key. When
  the value differs from `.gitmodules` AND the submodule is
  initialized (uninit submodules legitimately have no config entry),
  we set the status to UrlMismatch. The check is skipped for
  uninitialized submodules so they don't all get falsely flagged.

- `SubmoduleInfo::configUrl` is now actually populated (was an
  unused field in 0.22.0 — I had the right struct definition but
  never wrote to it).

## [0.22.0] — 2026-05-13

### Added
- **Submodule support** (init / update / sync). New "Submodules" tab
  in the local repo view shows every entry from `.gitmodules` with:
  - Path, URL, status (Not initialized / Up to date / Modified /
    URL mismatch / Missing), recorded SHA (from parent index), and
    current SHA (from submodule HEAD)
  - Per-row actions: "Init & Update", "Update", "Sync URL"
  - A top-level "Update all" button that runs init+update for every
    submodule sequentially
  - A "Refresh" button to re-read .gitmodules and re-check statuses

  Status meanings:
  - **Not initialized**: listed in .gitmodules but the workdir doesn't
    have it yet — the typical state after a fresh clone of a parent
    repo. Click "Init & Update" to clone it.
  - **Up to date**: workdir HEAD matches the SHA the parent commit
    recorded. Nothing to do.
  - **Modified**: workdir has uncommitted changes, or its HEAD has
    drifted from the recorded SHA. Either commit inside the submodule
    or "Update" to reset to the recorded SHA.
  - **URL mismatch**: `.gitmodules` URL differs from `.git/config`'s
    cached URL. Happens when the project moves a submodule's
    upstream. Click "Sync URL" to copy the new URL into config.
  - **Missing**: rare — recorded in index but not in `.gitmodules`,
    usually from hand-edits.

- The tab is **always visible** (no dynamic add/remove based on
  presence of `.gitmodules`). When the repo has no submodules, the
  tab shows a centred "No submodules in this repository." message.
  Simpler than maintaining add/remove state, and the tab name itself
  tells the user what's there.

- **Auto-list on folder switch**: when the user clicks a folder in
  the sidebar, `listSubmodules` runs automatically. The Submodules
  tab is already populated by the time the user looks at it.

### Internal (backend)
- New `ghm::git::SubmoduleInfo` struct in `GitHandler.h` with the
  fields listed above plus a `Status` enum. The enum collapses
  libgit2's `git_submodule_status_t` bitmask (which can flag many
  states simultaneously) into a single human-readable status. Order
  of precedence: Missing > UrlMismatch > Modified > UpToDate.
- Four new handler methods: `listSubmodules`,
  `initAndUpdateSubmodule`, `updateSubmodule`, `syncSubmoduleUrl`.
  The network ops (`init+update` and `update`) plumb credentials
  the same way `clone()` does — `credCb` is the same function, so
  ssh-agent works the same way, and PAT-over-HTTPS works the same
  way. Host-key prompts for unknown SSH servers also flow through
  the existing `certificateCheckCb`. `rewriteSshAgentError()`
  wraps the result for friendlier error messages.

### Internal (worker / UI)
- `GitWorker` gets four new async methods mirroring the handler
  ones, plus two signals: `submodulesReady` and
  `submoduleOpFinished`.
- `LocalRepositoryWidget` gets `buildSubmodulesTab()`,
  `setSubmodules()`, four new signals
  (`submodulesListRequested`, `submoduleInit/Update/SyncRequested`),
  and four click slots. The submodule list is cached in
  `submodules_` so "Update all" can iterate without re-querying.
- `MainWindow` wires the widget's submodule signals to the worker
  via lambdas (the workspace controller is skipped because no
  filtering or state is needed there — same shortcut as Tags).
  After every op, `listSubmodules` runs again to refresh the
  status column. New slot `onSubmoduleOpFinished` shows a
  `QMessageBox` on failure (errors are multi-line and need
  reading), or a status-bar message on success.

### Deliberately out of scope
- **Add new submodule**: would need `git_submodule_add`, write to
  `.gitmodules`, stage, commit — multi-step flow that doesn't fit
  the simple "init/update/sync existing" UI. Add later if needed.
- **Remove submodule**: similar reasoning. Multi-step (deinit,
  remove from index, edit `.gitmodules`, commit, delete workdir
  copy). Power users can do it from the terminal for now.
- **Recursive submodules**: libgit2 doesn't have a native recursive
  update; we'd have to recurse manually. Most projects don't use
  nested submodules, so left for later.
- **Per-submodule explicit key path / passphrase**: submodule
  network ops use ssh-agent only (no explicit key path UI like
  the clone dialog has). Users with encrypted keys not in agent
  should `ssh-add` first.

### PL translations
- 30 new strings across `LocalRepositoryWidget` (table headers,
  buttons, status labels, tooltips) and `MainWindow` (status-bar
  messages, error dialog titles).

## [0.21.0] — 2026-05-13

### Added
- **OAuth device flow login.** New "Sign in with GitHub" button at
  the top of the login dialog. Clicking it:
  1. Asks GitHub for a device code via
     `POST https://github.com/login/device/code`
  2. Shows the resulting short user code (e.g. "WDJB-MJHT") in a
     large monospace label, auto-opens the verification URL in the
     user's default browser, and gives them a "Copy code" button
     for convenience
  3. Polls `POST https://github.com/login/oauth/access_token` every
     5 seconds (per RFC 8628; backs off by 5s on `slow_down`)
  4. Receives the access token, validates it against `/user`, and
     finishes the login — same path as PAT after that

- **PAT fallback retained.** Existing PAT users see no change; the
  PAT form is below the OAuth button with the existing "Generate a
  token at github.com/settings/tokens" hint.

### Build configuration
- New CMake cache var `GHM_OAUTH_CLIENT_ID`. When set to an OAuth
  App's client_id (register one at github.com/settings/developers),
  the OAuth button is enabled and `-DGHM_OAUTH_CLIENT_ID="..."` is
  baked into the binary as a `#define`. When unset (default), the
  OAuth button is permanently disabled with an explanatory tooltip,
  and the PAT path is the only option. CMake prints a status line
  on configure either way so you know which mode you got.

  Why not hardcode an Anthropic-owned client_id? Because that would
  tie the public source tree to one upstream OAuth App forever.
  Forks, self-hosters, and downstream packagers each register their
  own and pass it at build time.

### Internal — new files
- `src/github/OAuthResponseParser.h`: pure functions for parsing
  the two JSON shapes returned during the device flow. Returns
  structured `DeviceCodeResponse` and `AccessTokenResponse` with
  an explicit `State` enum (Success / Pending / SlowDown / Error)
  so the caller's state machine stays trivial.
- `src/session/OAuthFlowController.{h,cpp}`: the state machine
  (Idle → FetchingDeviceCode → WaitingForUser → terminal).
  Uses `QTimer` for polling — GUI thread, no worker needed.
  Implements RFC 8628's slow_down handling (adds 5s to the
  interval on each slow_down response) and proactive expiry
  (stops polling once `expires_in` is exceeded, so the user gets
  a clean "code expired" message instead of GitHub's HTTP-level one).
- `src/ui/OAuthLoginDialog.{h,cpp}`: presentation only. Big
  monospace code label, Copy/Open buttons, status label that
  shows controller `statusChanged()` updates. Auto-opens the
  browser when `setUserCode()` is called so the user doesn't have
  to click — but the manual "Open GitHub in browser" button is
  still there in case the default browser is misconfigured.
- `src/github/GitHubClient` gains `startDeviceFlow()` and
  `pollAccessToken()` methods plus five new signals
  (`deviceCodeReceived`, `accessTokenReceived`, `pollPending`,
  `pollSlowDown`, `oauthError`).

### Internal — modified files
- `src/ui/LoginDialog.{h,cpp}`: rewrote constructor to show both
  paths (OAuth at top, PAT below, "or" separator). Added six new
  slots that drive the OAuth dialog and route success back through
  the existing `validateToken()` path — so OAuth and PAT share the
  same success/failure path from `/user` onward. `tokenIsOAuth()`
  accessor lets `SecureStorage` distinguish them.

### Threading
- OAuth runs entirely on the GUI thread. `QTimer::timeout` fires on
  the GUI thread; `QNetworkAccessManager::finished` fires on the GUI
  thread; modal dialogs are obviously GUI thread. No worker thread,
  no BlockingQueuedConnection. Compared to the SSH host-key approval
  sprint (0.20.0) this is straightforward.

### Tests
- 13 new unit tests in `test_OAuthResponseParser.cpp` covering both
  parser functions across happy path, every RFC 8628 error state
  (`authorization_pending`, `slow_down`, `expired_token`,
  `access_denied`), missing required fields, malformed JSON, and
  the interval-default-to-5s edge case.
- Test count: 66 → 79 unit + 45 integration = 124 total.

### Known limitations
- **Refresh token flow not implemented.** OAuth Apps configured with
  long-lived tokens won't be auto-refreshed when their token
  expires; the user will see an auth failure on next API call and
  need to re-sign-in. Adding refresh is a future sprint.
- **No sign-out / revoke.** Today's logout just clears the local
  token. The OAuth grant on GitHub's side stays until the user
  revokes it manually at github.com/settings/applications.
- **No PAT → OAuth migration prompt.** Existing PAT users have to
  notice the OAuth button themselves.
- **SecureStorage doesn't yet persist tokenType metadata.** The
  `LoginDialog::tokenIsOAuth()` accessor exists but the caller
  doesn't currently store it — would be a one-line follow-up.

## [0.20.1] — 2026-05-13

### Fixed
- Build failures in 0.20.0 from incomplete-type usage of cross-namespace
  types:
  - `workspace/GitHubCloneController.h` forward-declared `class GitWorker`
    but the new `startClone()` signature also takes a defaulted
    `SshCredentials` parameter. Defaulted parameters need the full
    type for the implicit default-construct. Added
    `#include "git/GitHandler.h"` (which defines both `GitWorker`
    transitively and `SshCredentials`).
  - `ui/MainWindow.cpp` constructed `new HostKeyApprover(...)` but
    only had a forward-decl from the header — `new` requires complete
    type. Added `#include "ui/HostKeyApprover.h"`.
- Lesson learned: when a public API surface grows a new type from
  another module, the header needs the full include, not just the
  forward-decl of the previously-sufficient pointer type. Same
  pattern as the 0.11.1 PublishController fix.

## [0.20.0] — 2026-05-13

### Added
- **Explicit SSH key file with passphrase support.** When the user
  checks "Use SSH" + "Use explicit key file" in CloneDialog, a
  follow-up `SshKeyDialog` collects the private-key path and (if
  encrypted) passphrase before the worker is dispatched. The dialog
  inspects the key file offline via the new `SshKeyInfo` helper and
  enables/disables the passphrase field based on whether the key is
  actually encrypted — no spurious prompts for unencrypted keys.
- **Interactive host-key approval dialog.** When libgit2 reports an
  unknown SSH host (e.g. fresh installation, no `~/.ssh/known_hosts`
  entry for github.com), a `HostKeyApprovalDialog` shows the
  fingerprint (SHA-256, base64-encoded, same format as
  `ssh-keygen -lf`) and asks whether to trust it. On accept, a
  hashed entry matching OpenSSH's `HashKnownHosts=yes` format is
  appended to `~/.ssh/known_hosts` so subsequent clones to that
  host skip the prompt.

### Added (backend)
- New struct `ghm::git::SshCredentials` (in `GitHandler.h`) carries
  explicit-key state through to libgit2's credential callback.
- `GitHandler::clone()` gains an optional `sshCreds` parameter.
  Worker and `GitHubCloneController::startClone()` plumb it through.
- `credCb` now has two SSH paths: explicit (via
  `git_credential_ssh_key_new`) and agent fallback (existing
  `git_credential_ssh_key_from_agent`). The explicit path is taken
  only when the caller fills `CallbackCtx::useExplicitKey`.
- New free function `certificateCheckCb` registered as libgit2's
  `certificate_check`. For SSH host keys, it routes the decision to
  the GUI thread via `HostKeyApprover::instance()` using
  `Qt::BlockingQueuedConnection`. The bridge pattern is documented
  in `HostKeyApprover.h` with explicit reasoning about why
  BlockingQueuedConnection is safe here.

### Added (UI)
- `src/ui/SshKeyDialog.{h,cpp}`: modal for picking key path and
  entering passphrase. Uses `SshKeyInfo` to live-inspect the file
  as the path changes.
- `src/ui/HostKeyApprovalDialog.{h,cpp}`: read-only modal showing
  fingerprint with explicit "Cancel" defaulting to the safe choice.
- `src/ui/HostKeyApprover.{h,cpp}`: GUI-thread approver, exposes
  `Q_INVOKABLE bool requestApproval()` slot called via
  `BlockingQueuedConnection` from the worker.
- `CloneDialog` gains a second checkbox "Use explicit key file"
  alongside the existing SSH toggle, with proper enable/disable
  coupling (only meaningful when SSH is on).

### Added (helpers)
- `src/git/SshKeyInfo.h`: header-only function `inspectSshKey()`
  that opens the file, reads the first 8KB, and decides whether it's
  an OpenSSH or PEM key, and whether it's encrypted. Used by
  SshKeyDialog. Handles both formats:
  - PEM: matches `Proc-Type: 4,ENCRYPTED` header
  - OpenSSH: decodes the base64 outer container far enough to read
    the cipher name; "none" = unencrypted, anything else = encrypted

### Internal
- Test count: 58 → 66 unit tests (+ 45 integration). New
  `test_SshKeyInfo.cpp` has 8 cases covering empty file, PEM
  encrypted/unencrypted, OpenSSH encrypted/unencrypted, unknown
  format, and a truncated-input edge case. Test fixtures are
  hand-constructed base64 blobs (verified to decode to the expected
  bytes via a Python check) to avoid spawning `ssh-keygen` from the
  test runner.
- PL translations: 19 new strings across SshKeyDialog,
  HostKeyApprovalDialog, CloneDialog, and MainWindow contexts.

### Threading notes
- `BlockingQueuedConnection` is used in exactly one place
  (`certificateCheckCb` → `HostKeyApprover::requestApproval`). The
  decision is documented in `HostKeyApprover.h` with the rationale
  that there's no cyclical wait: the GUI thread doesn't await the
  worker while the modal is open, so deadlock is impossible.
- `CallbackCtx` is filled on the GUI thread BEFORE the worker is
  dispatched. The credential callback running on the worker only
  reads, never writes back. Passphrase bytes are destroyed when
  the operation completes (CallbackCtx goes out of scope).

### Still deferred
- TLS certificate approval (`certificate_check` for HTTPS): we
  currently reject any TLS cert libgit2 couldn't validate against
  the system CA store. Adding a "Trust this fingerprint" dialog for
  TLS is conceptually similar to the SSH host key flow but isn't
  needed for the common GitHub case (their TLS chains to a public CA).
- Explicit key support for pull/push/fetch on existing repos. Today
  those still use ssh-agent only. A future sprint can plumb
  `SshCredentials` through the other network ops if there's demand.

## [0.19.0] — 2026-05-13

### Added
- **Per-repo SSH/HTTPS override.** `CloneDialog` now has a "Use SSH"
  checkbox alongside the target path. It prefills from the
  `Settings → Prefer SSH for new clones` toggle, but the user can
  flip it per-clone — useful when most repos go over SSH but one
  needs HTTPS (or vice versa). Per-clone decision is NOT written
  back to settings.
- **Better SSH error messages.** Two specific failure modes now
  surface targeted guidance instead of libgit2's generic
  "authentication required but no callback set":
  - **ssh-agent empty / wrong key**: error mentions `ssh-add -l`,
    `ssh-add ~/.ssh/id_ed25519`, and `ssh -T git@github.com` as
    diagnostic steps.
  - **Unknown host key**: error tells the user to run
    `ssh -T git@github.com` once in a terminal to register the
    fingerprint in `~/.ssh/known_hosts`.
  Both messages include the original libgit2 error verbatim so
  the diagnosis isn't hidden.

### Internal
- `CallbackCtx` gets two new fields: `sshAttempted` and
  `sshAgentFailed`. They're set inside `credCb` during SSH credential
  lookups, and inspected by a new `rewriteSshAgentError()` helper
  that wraps the return path of every network op (clone, fetch,
  pull, pushBranch). When SSH wasn't involved or the op succeeded,
  the helper is a no-op pass-through.
- The unknown-host detection is heuristic (matches common libssh2
  error strings: "host key verification", "unknown host", "the
  remote host's key", "server's host key"). False positives — i.e.
  rewriting a non-host-key error — are mitigated by always including
  the original libgit2 text in the new message, so power users can
  still see what really happened.

### Still deferred to future sprints
- **Explicit key path + passphrase prompt.** Users with encrypted
  keys that aren't in ssh-agent still can't authenticate through the
  app. Requires careful threading work (modal in GUI thread, cached
  creds passed into worker callback) — deferred to its own sprint.
- **Interactive host-key approval dialog.** Today's MVP only points
  the user to the terminal. A future sprint would intercept libgit2's
  `certificate_check` callback and pop up a "Trust this fingerprint?"
  dialog, then write to `~/.ssh/known_hosts` on accept.

## [0.18.0] — 2026-05-13

### Added
- **SSH transport support** (via libgit2's libssh2 backend).
  - New `Settings → Prefer SSH for new clones` toggle. When enabled,
    the clone-from-GitHub flow rewrites the HTTPS clone URL into its
    SCP-style SSH equivalent (`git@host:owner/repo.git`) before
    handing it to libgit2.
  - Credential callback now handles `GIT_CREDENTIAL_SSH_KEY` by
    asking ssh-agent for a matching key
    (`git_credential_ssh_key_from_agent`). On Linux this talks to the
    same agent your terminal sees via `SSH_AUTH_SOCK`; just
    `ssh-add ~/.ssh/id_ed25519` once before launching the app and
    you're set.
  - Existing remotes are unaffected — the toggle only influences
    new clones initiated from the sidebar. If a repo's `.git/config`
    already has an SSH URL for `origin`, push/pull will use SSH
    regardless of the setting (libgit2 picks the transport from
    the URL, not the setting).

- New header-only helper `github/SshUrlConverter.h` with `httpsToSsh()`
  for the URL transformation. Pure function; covered by 13 unit
  tests including edge cases (already-SSH URLs, missing .git suffix,
  user prefix stripping, custom ports, non-GitHub hosts, malformed
  inputs).

### Known limitations
- **ssh-agent must be running and have the key loaded.** If
  `ssh-add -l` returns "The agent has no identities", clones will
  fail with a generic authentication error. A future sprint will
  add a pre-flight check and a clearer error message.
- **No explicit key-path or passphrase prompt yet.** Users with
  encrypted keys that aren't in the agent can't currently authenticate
  through the app. Workaround: `ssh-add` first, GUI second.
- **No host-key verification UI.** libgit2's default callback rejects
  unknown hosts; for a fresh box you may need to `ssh -T git@github.com`
  once in a terminal to populate `~/.ssh/known_hosts`. A future sprint
  will add an in-app prompt.
- This feature **requires** libgit2 built with libssh2. All major
  distros ship this by default (Arch's `libgit2`, Debian's
  `libgit2-dev`, Fedora's `libgit2-devel`). If your distro doesn't,
  SSH clones will fail at the credential step.

### Internal
- `CallbackCtx::token` is now allowed to be empty — SSH-only flows
  pass an empty token and the credential path branches on
  `allowed_types` rather than token presence.
- Test count: 58 unit tests + 45 git integration tests = 103 total.
- The `MainWindow::onCloneRequested` flow takes one extra branch
  for the URL rewrite. Settings I/O stays in MainWindow as before;
  the controller signature is unchanged.

## [0.17.0] — 2026-05-13

### Added
- **Infinite history scroll.** The History tab is no longer capped at
  200 commits. Initial load still fetches 200 (matching previous
  behaviour for snappy first paint), but a `📂 Load more older
  commits…` sentinel row appears at the bottom whenever there's
  more to fetch. Clicking it requests the next 200, appended to the
  list. The sentinel disappears when we hit the actual end of
  history.
- The "Loading older commits…" feedback shows in place of the
  sentinel during the fetch so the user can tell the click was
  registered. A second click on the same sentinel before the batch
  arrives is silently ignored (guarded by `isLoadingMore_`).

### Added (backend)
- `GitHandler::log()` accepts an optional `afterSha` parameter.
  When non-empty, the revwalk starts at that commit and skips
  itself, returning only OLDER commits. This is the natural git
  pattern (`git log <sha>~`); the alternative — re-walking from
  HEAD and skipping N — would be linear in pages already shown,
  which gets expensive for repos with thousands of commits.
- `GitWorker::loadHistory()` takes the same `afterSha`. The
  `historyReady` signal now carries an `isAppend` flag so the
  receiver knows whether to REPLACE its list (initial) or APPEND
  to it (paginated).
- `LocalRepositoryWidget` exposes a new `appendHistory()` method
  alongside `setHistory()`. The two share a private
  `appendCommitRow()` helper so the line format stays identical.
- `LocalWorkspaceController` forwards the `isAppend` flag through
  its passthrough signal and adds a slot
  `onLoadMoreHistoryRequested(path, afterSha)`.

### Fixed
- Commit filter no longer hides the "Load more…" sentinel.
  Previously the sentinel had an empty `kCommitRole` (it's not a
  commit) so any non-empty filter would hide it, stranding the user
  with no way to fetch potentially-matching older rows. The filter
  now special-cases the sentinel and keeps it visible.

### Internal
- Test count: 43 → 45. New tests verify (1) `afterSha`-based
  pagination returns the right slices across a 6-commit chain,
  including the boundary case of asking for older commits than
  the oldest (returns empty), and (2) invalid SHA rejected with
  clear error.
- New PL translations for the sentinel labels (3 strings).
- Sentinel uses `Qt::UserRole + 10` so it doesn't collide with
  existing per-commit data roles 2-5.

## [0.16.0] — 2026-05-13

### Changed
- **Internal refactor: `GitHubCloneController` extracted from
  `MainWindow`.** The "clone from GitHub" and "open existing local
  copy" flows used to live in MainWindow as 3 inline slots
  (`onCloneRequested`, `onOpenLocallyRequested`, `onCloneFinished`).
  Validation (target-exists, .git presence), worker dispatch, and
  the in-flight state tracking now belong to
  `workspace/GitHubCloneController.{h,cpp}`. MainWindow keeps the
  CloneDialog / QFileDialog handling — the controller takes the
  user's resolved path as a parameter.

### Fixed
- **Cannot start a second clone while one is in flight.**
  Previously, clicking "Clone" on a different repo during an active
  clone would happily kick off a second worker call; the worker can
  only run one at a time, so the second clone silently waited or
  collided. Controller now exposes `isBusy()` and MainWindow refuses
  the second clone with a clear message.
- **Optimistic sidebar badge cleanup on failure.** When a clone
  failed, the sidebar mapping (`localPathByFullName_`) was scrubbed
  by scanning ALL values for a match. Now the rollback matches both
  `fullName` and `localPath`, preventing accidental scrubs if two
  repos somehow ended up mapped to the same path (unusual but
  possible if the user edited settings manually).

### Internal
- New controller signals (`cloneStarted`, `cloned`, `opened`,
  `failed`, `defaultCloneDirectoryChanged`) replace direct manipulation
  of `localPathByFullName_` from inside a worker-callback slot.
  Settings writes happen via host's slot for `defaultCloneDirectoryChanged`,
  keeping `Settings` ownership in MainWindow.
- Worker subscription for `cloneFinished` moved into the controller.
  MainWindow no longer subscribes — controller filters by
  `inFlightLocalPath_`.
- `openExisting` (formerly inline `onOpenLocallyRequested` validation)
  is synchronous, but lives with the controller anyway since it's
  the same conceptual operation (mapping a GitHub repo to a local
  folder). Validation errors surface through the same `failed()`
  signal path as clone failures, giving the host one error-handling
  surface for both.

## [0.15.0] — 2026-05-13

### Changed
- **Internal refactor: `ConflictController` extracted from `MainWindow`.**
  The merge-conflict resolution flow (listConflicts → loadBlobs →
  markResolved loop with auto-close on completion, plus the abort path)
  used to live in MainWindow as 7 slots + the `conflictDialog_` +
  `lastConflictEntries_` cache. It's now a self-contained controller
  in `workspace/ConflictController.{h,cpp}`. The controller owns the
  dialog instance (lazy-created on first `start()`) and the per-flow
  state cache. MainWindow shrinks by ~66 lines.

### Internal
- New ConflictController signals (`statusChanged`, `operationSucceeded`,
  `operationFailed`, `allResolved`, `mergeAborted`, `workingTreeChanged`)
  replace direct manipulation of `lastConflictEntries_` and direct
  `conflictDialog_->setBusy()` calls in MainWindow. UI side-effects
  (status bar, QMessageBox prompts) stay in MainWindow — only the
  state machine and dialog-lifetime moved.
- `MainWindow::onLocalFolderActivated()` now calls `conflict_->reset()`
  when switching active folder; previously it cleared
  `lastConflictEntries_` directly. The controller closes its dialog
  if it's open, avoiding a stale half-resolved conflict view from a
  different repo.
- Worker subscriptions for `conflictsReady` / `conflictBlobsReady` /
  `conflictOpFinished` moved from MainWindow into ConflictController.
  Each handler filters by `activePath_` so events for other paths
  fall through harmlessly.
- `ConflictResolutionDialog` include removed from `MainWindow.cpp` —
  the controller is the only owner now.

## [0.14.0] — 2026-05-13

### Added
- **Reflog viewer + recovery action.** New `Repository → Reflog —
  recover lost commits…` opens a dialog listing HEAD's reflog: every
  commit, checkout, reset, merge and rebase that ever moved HEAD,
  newest first, with relative timestamps and the entry's message.
  Selecting an entry and clicking **Restore HEAD here** runs a soft
  reset to that SHA — useful when you've done something destructive
  (hard reset, force checkout, rebase gone wrong) and want to get
  back to a known state.
- The restore action is always a **soft** reset: HEAD moves but the
  working tree stays untouched. Any changes from "abandoned" commits
  reappear as staged so you can review before re-committing. Hard
  reset isn't exposed from this dialog — too easy to misuse, and
  the soft-reset path is already recoverable.
- Dialog includes prominent warnings about reflog's limits: it's
  local only (not synced to remotes), expires after 90 days by
  default, and a manual `git gc` can reap underlying commits.

### Added (backend)
- `GitHandler::readHeadReflog()` returns a `std::vector<ReflogEntry>`
  with old/new SHA, message, timestamp, and committer per entry.
  Maps to libgit2's `git_reflog_*` API.
- `GitHandler::softResetTo(sha)` validates that the target commit
  is still in the object database (gives a clear error if not —
  the commit may have been garbage-collected) and refuses to operate
  during merge/rebase/cherry-pick, same as `undoLastCommit`.
- `GitWorker::loadReflog()` and `softResetTo()` for async dispatch.
- New `ReflogEntry` struct in `git/GitHandler.h`.

### Internal
- Test count: 36 → 43. New tests cover empty reflog on fresh repo,
  reflog recording commits, maxCount cap, restore-from-reflog
  roundtrip (commit → soft-undo → restore via softResetTo), and
  error paths for invalid/empty SHAs.
- Reflog `restore` path issues a fresh `loadReflog()` after success
  so the dialog stays in sync (the restore itself is a new reflog
  entry).

## [0.13.0] — 2026-05-12

### Added
- **Diff stats inline in the history list.** Each commit now shows
  insertion/deletion line counts and the number of files changed,
  e.g. `abc1234  Fix the bug  (+45 −12 / 3 files)  —  Alice, 2h ago`.
  Lets you scan the history for big or risky commits at a glance
  rather than clicking through each one.
- `CommitInfo` gets three new fields: `filesChanged`, `insertions`,
  `deletions`. Default to -1 ("not computed") so UI can distinguish
  from genuine zero values.
- `GitHandler::log()` accepts an optional `computeStats` parameter.
  When true (worker passes this for the live history view) it runs
  a second pass per commit doing a tree-to-tree diff against the
  first parent (or empty tree for the root commit) and grabs
  `git_diff_get_stats()`. Costs roughly 10–30ms per commit, which on
  the 200-commit cap works out to ~3-6s — but it runs on the worker
  thread so the GUI stays responsive.

### Internal
- Test count: 33 → 36 cases. New tests cover (1) default-off behaviour
  preserves `-1` markers, (2) root-commit stats correctly diff
  against empty tree, (3) modified-file stats correctly account for
  replaced lines as insertion+deletion rather than zero each.
- The stats loop runs after the main revwalk rather than inline.
  This keeps the hot path readable and means a future
  "incremental stats compute" feature (e.g. only compute for the
  top N visible rows) can be added without touching the main loop.
- Merge commits report stats against first parent — matches what
  `git log --shortstat` does by default. Octopus merges undercount,
  but that's consistent with git's behaviour.

## [0.12.0] — 2026-05-12

### Added
- **Transfer progress for clone / pull / push / fetch.** The status-bar
  progress bar now switches from indeterminate "marquee" mode to
  determinate object-count mode while libgit2 is transferring objects
  to or from the remote. The bar text reads "Receiving objects 87/523"
  during fetch/pull/clone or "Pushing objects 12/40" during push,
  giving real feedback for large transfers instead of a stuck spinner.
- The two phase strings are translated; PL: "Pobieranie obiektów" /
  "Wysyłanie obiektów". Other phases would fall back to the source
  language — currently libgit2 reports only these two.

### Fixed
- `fetch` now reports progress. Earlier the worker called
  `handler_.fetch()` without passing a progress callback, so the
  status bar stayed in indeterminate mode the entire time. Same
  three-line fix as the other network ops: handler accepts an
  optional `ProgressFn`, worker wraps it via `makeProgressFn`.
- The progress bar now resets to indeterminate range (0,0) on
  every `setBusy(true)` and `setBusy(false)` call, so the next
  operation doesn't briefly inherit the previous one's stale
  determinate-mode value (e.g. "87/523") before its first transfer
  callback lands.

### Internal
- Consolidated duplicate `onWorkerProgress` slot — there were two
  declarations and two implementations after a partial earlier
  attempt at this feature. The richer implementation (with phase
  translation, INT_MAX overflow guard for very large repos, and
  proper format strings) is kept; the simpler stub is removed.

## [0.11.1] — 2026-05-12

### Fixed
- Build failure in `PublishController.h` introduced in 0.11.0. The
  header relied on forward-declarations of `ghm::git::GitWorker`,
  but the `onLocalStateReady` slot signature uses
  `std::vector<ghm::git::StatusEntry>` and `RemoteInfo` directly —
  moc needs the complete types to generate the signal/slot bridge.
  Added the missing `#include "git/GitHandler.h"` (which defines
  those types), mirroring how `LocalWorkspaceController.h` was
  already structured.

## [0.11.0] — 2026-05-12

### Changed
- **Internal refactor: `PublishController` extracted from `MainWindow`.**
  The 5-step "Publish to GitHub" state machine (`POST /user/repos` →
  `addRemote` → `refresh` → `push`) used to live inline in MainWindow
  as `pendingPublish_` + 5 slots scattered across the file. It's now
  a self-contained controller in `workspace/PublishController.{h,cpp}`,
  owning the state machine and subscribing to the relevant GitHubClient
  and GitWorker signals itself. MainWindow shrinks by ~50 lines and
  publishes via a single `publish_->start(params)` call.

### Fixed
- **Stale publish state on aborted flows.** Previously if a publish
  was interrupted (network drop, app exit mid-flow), `pendingPublish_`
  could remain set in memory and block the next publish attempt with
  "Another publish operation is in progress". The new controller has
  explicit `reset()` and exposes `isActive()`, and it adds a
  **per-step watchdog timer** (15s for local ops, 60s for network) —
  if any step hangs, the controller surfaces a clear "Publish timed
  out" error rather than leaving the user staring at a spinner.

### Internal
- New controller signals (`progress`, `succeeded`, `failed`,
  `repoCreated`, `needNonEmptyBranch`) replace direct manipulation
  of `pendingPublish_` in MainWindow. Side-effects (status bar,
  dialogs, repo cache updates) stay in MainWindow — only the state
  machine moved.
- `onLocalStateReady` slot fully removed from MainWindow. Previously
  it carried both UI-feedback logic (moved to
  `onWorkspaceStateRefreshed` in 0.7.0) and publish-flow detection
  (moved here in 0.11.0); nothing was left worth keeping.
- `onPushFinished` and `onRemoteOpFinished` now have early-out
  guards when a publish flow is active — controller handles the
  publish path, MainWindow handles only manual ops.

## [0.10.0] — 2026-05-12

### Added
- **Integration tests for `GitHandler`** — 33 cases covering init,
  status, commit, branch (create/switch/delete/rename),
  stash (save/list/pop/drop), tag (lightweight/annotated/delete),
  fetch (against a file:// remote), undo last commit, and stage/unstage.
  Each test spins up a fresh `QTemporaryDir`-backed repo, runs the
  operation, and asserts the result on disk or in the libgit2 state.
- New `tests/TempRepo.h` helper — RAII wrapper around `QTemporaryDir`
  that pre-initialises a Git repo, sets a test identity, and offers
  one-liners like `commitFile(path, content, message)` for the
  typical setup boilerplate.
- New CMake helper `ghm_add_git_integration_test()` for tests that
  need libgit2 — compiles the `GitHandler` sources into each test
  binary and links libgit2 via pkg-config.

### Internal
- Test coverage now spans both inline parsers/validators (4 suites,
  79 cases) and the libgit2 wrapper itself (1 suite, 33 cases).
  Total: 5 suites, 112 cases. Tests still opt-in via
  `-DGHM_BUILD_TESTS=ON`.
- The integration tests do real disk I/O so they're ~50–100× slower
  than the pure unit tests, but still complete in well under a
  second total (well within ctest defaults). No network — the
  one network-flavoured op (fetch) uses a second `TempRepo` as a
  local "remote" via libgit2's filesystem-URL support.
- Tests intentionally do not exercise push/pull/clone against real
  GitHub — those would need a stable network and a test PAT.
  Those happy paths get covered indirectly when users smoke-test
  the UI.

## [0.9.2] — 2026-05-12

### Fixed
- **Polish translation now actually applies after restart.** The
  0.9.0 migration to the standard `lupdate`/`lrelease` pipeline
  emitted all 375 translation entries under a single synthetic
  `<context>ghm</context>` block. That worked with the legacy
  hand-rolled `Translator::translate()` (which ignored context),
  but `QTranslator::translate()` keys lookups on
  `(context = staticMetaObject.className(), source)`. For a
  `tr("Refresh")` in `MainWindow`, Qt searched for
  `("ghm::ui::MainWindow", "Refresh")` — and our `.ts` had
  `("ghm", "Refresh")`. Every lookup missed, every string fell
  back to the English source. UI stayed in English even after
  picking Polish from the menu.
- Fix: distribute every translation entry into a separate
  `<context>` named after the fully-qualified Q_OBJECT class it
  could appear in. Done by the new
  `scripts/expand_ts_contexts.py`, which walks `src/` looking
  for `Q_OBJECT` classes inside `namespace ghm::xxx { ... }`
  blocks and clones the `ghm`-context entries into each. Result:
  22 contexts × 375 messages = ~8.2K entries. The `.qm` file is
  binary-compressed so the on-disk size doesn't balloon.
- Added a fallback search in `Translator::setLanguage()` — if
  the primary resource path doesn't have the `.qm`, it now
  tries three alternative paths and dumps the actual resource
  tree to stderr so future path mismatches can be diagnosed.

### Internal
- New `scripts/expand_ts_contexts.py` — re-runnable, idempotent.
  Won't touch contexts that already exist in the `.ts` (e.g.
  hand-curated per-class translations from a real `lupdate` run).
- This is a transitional fix: real `lupdate` would put each
  string into only the context where the corresponding `tr()`
  actually appears, not into all of them. When `lupdate` is run
  in a future sprint to refresh from the source code, it'll
  replace these blanket-cloned contexts with precise ones.

## [0.9.1] — 2026-05-12

### Fixed
- **Polish translation is now complete** (375 of 375 strings). The
  0.9.0 migration left 58 strings from the 0.6.x–0.8.x sprints
  untranslated, plus 9 entries had corrupted `\n\n` paragraph breaks
  collapsed to `\n` by the seed script's `minidom.toprettyxml` pass.
  Both are now fixed: the missing strings have hand-written Polish
  translations, and the by-hand XML writer in
  `scripts/translator_to_ts.py` preserves newline runs verbatim.
- `scripts/translator_to_ts.py` rewritten to be self-contained and
  idempotent — running it now refreshes the `.ts` from `src/` and
  the existing `.ts`, suitable as a fallback for environments
  without Qt's `lupdate` tool.

## [0.9.0] — 2026-05-12

### Changed
- **Translation pipeline migrated to standard Qt Linguist.** Previous
  versions kept an in-memory English→Polish phrasebook of 300+ entries
  hardcoded in `Translator.cpp` — convenient at first but
  incompatible with `lupdate`, Qt Linguist, and any crowdsourcing
  workflow (Weblate, Crowdin, etc). The translation table now lives
  in `translations/github-manager_pl.ts` and gets compiled into `.qm`
  resources at build time.
- `Translator` is now a thin ~50-line wrapper around `QTranslator`
  that loads compiled `.qm` from Qt resources at runtime. No
  user-visible behaviour change; the menu language switch still
  prompts for a restart (live retranslation requires every widget
  to override `changeEvent` — deferred to a future sprint).

### Added
- New `translations/` source directory with the seed `.ts` file
  generated from the legacy phrasebook (336 entries). Run
  `cmake --build build --target github-manager_lupdate` to walk
  the source tree and refresh the `.ts` with new `tr()` strings
  added since the last update; edit in Qt Linguist; rebuild.
- New `scripts/translator_to_ts.py` — one-shot tool that extracted
  the legacy phrasebook to `.ts` format. Kept in-tree for
  reference / as a template for any future format migrations.
- README section explaining the translation workflow and how to
  add a new language.

### Internal
- `find_package(Qt6 ... LinguistTools)` added so the build can find
  `lrelease`/`lupdate`. Existing builds on Arch (qt6-tools) already
  have this; other distros may need a separate package
  (`qt6-tools-dev` on Debian/Ubuntu, etc).
- Strings added in 0.6.x–0.8.x that were never in the legacy
  phrasebook (conflict resolution, fetch, undo last commit, ~70
  entries total) are currently untranslated in PL until the next
  `lupdate` run — they'll display in English until somebody
  translates them in Linguist. This is not a regression: the
  legacy phrasebook also didn't have them, it just fell back to
  the source via the same path.

## [0.8.1] — 2026-05-10

### Fixed
- Build failure in `LinkHeaderParser.h` introduced in 0.7.1. The
  `rel=...` regex contains the `)"` sequence (close-paren of a
  capture group followed by a literal quote), which prematurely
  terminates a `R"(...)"` raw string. Switched to a custom
  delimiter `R"RE(...)RE"` so the regex parses correctly.

## [0.8.0] — 2026-05-06

### Added
- **Fetch from origin** (`Repository → Fetch from origin`,
  Ctrl+Shift+F). Updates `refs/remotes/origin/*` without touching the
  working tree, so you can see whether a teammate has pushed without
  having to pull (which would mutate your branch). After fetch
  finishes, the branch popup's ahead/behind counts reflect the new
  remote state.
- **Undo last commit** (`Repository → Undo last commit…`,
  Ctrl+Shift+Z). Equivalent to `git reset --soft HEAD~1`: the commit's
  changes come back as staged so you can edit and re-commit. The
  confirmation dialog warns about the force-push implications if the
  commit was already pushed.

### Internal
- New `GitHandler::fetch()` and `GitHandler::undoLastCommit()`
  primitives. Fetch reuses the existing `credCb` for HTTPS auth (PAT
  flows through the same path as push/pull/clone). Undo refuses to
  operate during merge/rebase/cherry-pick — the soft-reset would
  leave the repo in an ambiguous state.
- Repository menu reorganised: fetch at the top (most-frequent
  remote action), stash and tags in the middle, undo last commit at
  the bottom (separated to avoid muscle-memory misclicks).

## [0.7.1] — 2026-05-06

### Changed
- **Refactor: Link-header parser extracted to a testable header.**
  `nextPageFromLink` was a private function buried inside
  `GitHubClient.cpp`; it now lives in `github/LinkHeaderParser.h` and
  is exercised by 18 unit tests. The new `parseLinkHeader` function
  also returns all rel-tagged URLs (prev/next/first/last) in one
  pass — useful for any future "jump to last page" UX, though no
  caller uses it yet.
- The new parser is more tolerant of well-formed-but-unusual headers:
  unquoted rel values (`rel=next`), extra whitespace around delimiters,
  case-insensitive rel keyword, and parameters before/after rel.
  GitHub doesn't produce these variants, but parsing them robustly
  protects against the day a CDN or proxy sits between us and
  api.github.com.

### Internal
- Added `tests/test_LinkHeaderParser.cpp` (18 cases) covering empty
  input, garbage input, GitHub's canonical formats, RFC 8288
  tolerance, duplicate-rel deterministic resolution, and the
  QByteArray convenience overload.

## [0.7.0] — 2026-05-06

### Changed
- **Internal refactor: `LocalWorkspaceController` extracted from
  `MainWindow`.** Roughly 14 worker-callback slots and 13 widget-signal
  handlers that had been living in `MainWindow.cpp` moved into a new
  controller class in `src/workspace/`. `MainWindow.cpp` shrunk by
  about 250 lines and is now closer to a UI shell — it routes signals
  to the controller and reacts to its higher-level outcomes (status
  bar updates, error dialogs, view refresh).
- The controller owns path filtering (worker callbacks for other
  folders are dropped before they reach the host) and force-delete
  escalation (detects "not fully merged" failures and asks the host
  to confirm before re-firing with `force=true`). Host MainWindow
  responds to controller-emitted "open this dialog" requests rather
  than driving the dialogs from inside slot bodies.
- `onLocalStateReady` was kept in `MainWindow` but slimmed to the
  publish-flow detection only — UI feedback (conflict banner, status
  bar) moved to the controller's `stateRefreshed` signal.

### Internal
- New folder `src/workspace/` with namespace `ghm::workspace`. Future
  controllers (e.g. `PublishController`) will live here too.
- The controller composes `GitWorker` rather than owning it. `MainWindow`
  still constructs the worker because publish flow and conflict
  resolution also use it.
- No user-visible behaviour changes. This is purely a code-organisation
  release.

## [0.6.1] — 2026-05-06

### Fixed
- Status bar got stuck showing "Loading repositories…" a few seconds
  after the load actually finished. The temporary "Loaded N
  repositories." success message (shown with a 4-second timeout)
  expired and the bar fell back to the stale "Loading…" permanent
  label that `setBusy(true)` had pushed earlier and `setBusy(false)`
  never cleared. Looked like the app was hung mid-fetch when in fact
  it was idle. `setBusy(false)` now clears the permanent label too.

## [0.6.0] — 2026-05-06

### Added
- **Conflict resolution flow.** When a merge / rebase / cherry-pick /
  stash-pop leaves the repository in a conflicted state, a red banner
  appears at the top of the local-folder view summarising the
  situation ("Merge in progress — N conflicted files."). Clicking
  "Resolve conflicts…" opens a 3-pane dialog showing the file's
  ours / base / theirs versions side-by-side with synchronised
  vertical scrolling. The dialog is modeless: edit the working-tree
  file in your preferred editor, alt-tab back, click "Mark resolved"
  to stage the result. When all files are marked resolved the dialog
  auto-closes with a prompt to commit and finish the merge.
- **"Open in editor" / "Show working file"** buttons in the conflict
  dialog. The first launches the user's default editor for the
  conflicted file (via xdg-open associations on Linux); the second
  opens the parent folder so users can run their own merge tool from
  there.
- **"Abort merge"** button. Equivalent to `git merge --abort`:
  cleans up the merge state and hard-resets the working tree to HEAD.
  Confirms before firing because it discards in-progress work.

### Changed
- The conflict banner takes priority over the "Publish to GitHub"
  banner when both could be shown — an in-progress merge is the more
  pressing concern.
- Status bar message after refresh now says "N conflicted file(s)"
  when applicable, replacing the generic "N changed file(s)".

### Internal
- New `GitHandler` primitives: `listConflicts`, `loadConflictBlobs`,
  `markResolved`, `abortMerge`, `isMerging`. Conflict iteration uses
  `git_index_conflict_iterator`; blob contents are loaded on demand
  to keep listing cheap.
- New `ConflictEntry` struct carries the three blob OIDs plus
  optional content. `markResolved` correctly handles the
  delete-resolution case (file remains absent from index after
  resolve).
- `ConflictResolutionDialog` reuses the existing modeless-dialog
  pattern from stash/tags — host (`MainWindow`) keeps a pointer and
  feeds it asynchronous worker results.

## [0.5.0] — 2026-05-05

### Added
- **Stash management.** New "Repository → Stash changes…" entry opens
  a small dialog with an optional message and the two `git stash`
  flag toggles (`-u` for untracked files, `--keep-index` for staged
  contents). "Repository → Manage stashes…" lists the stack with
  Apply, Pop, and Drop buttons. Drop is destructive so it confirms
  before firing. Conflicts during apply/pop produce a clear status
  message — the stash is left on the stack so you can resolve and
  retry.
- **Tag management.** "Repository → Tags…" opens a dialog showing
  every tag in the repo with annotation indicators, plus a create
  form at the bottom. Empty message ⇒ lightweight tag (no signature
  required). Non-empty message ⇒ annotated tag (requires configured
  author identity). Delete operates on the selected tag with
  confirmation.

### Changed
- New top-level "Repository" menu between Account and Settings,
  hosting the stash/tags entries. Items are disabled when no local
  folder is active.

### Internal
- New `GitHandler` primitives: `stashSave`, `stashList`, `stashApply`,
  `stashPop`, `stashDrop`, `listTags`, `createTag`, `deleteTag`. All
  reentrant in line with the rest of the class.
- Tag and branch ref names share the same git-check-ref-format rules,
  so `TagsDialog` reuses the existing `BranchNameValidator` rather
  than duplicating the logic.
- Modeless dialogs (`StashListDialog`, `TagsDialog`) are kept as
  member pointers so the host can refresh their contents asynchronously
  when worker callbacks land — no need to close-and-reopen after each
  operation.

## [0.4.0] — 2026-05-05

### Added
- **Rename branches.** New "Rename current branch…" entry in the
  branch picker popup. Backed by `git branch -m`. The input dialog
  reuses the same name validator as branch creation, so spaces,
  reserved names, and forbidden characters are caught before submit
  rather than producing cryptic libgit2 errors. Renaming the
  currently-checked-out branch works fine — HEAD's symbolic ref tracks
  the new name automatically.
- **Filter commits.** A search field above the History list filters
  live by summary, short SHA, or author name (case-insensitive).
  Active filter is preserved across Refresh and reset on folder
  switch.
- **Compare two commits** (`git diff a..b`). Ctrl+click a second
  commit in the History list to compare it against the currently
  selected one. The lower diff pane shows the per-file changes between
  the two commits, with rename detection just like single-commit
  diffs. The older commit is treated as the base, the newer as the
  target — what you'd see from `git diff <older> <newer>`.

### Changed
- History list now uses `ExtendedSelection` mode, enabling Ctrl+click
  for compare. Single-click behaviour (one commit selected) is
  unchanged.

### Internal
- New `GitHandler::renameBranch()` and `GitHandler::commitDiffBetween()`
  primitives, both reentrant (consistent with the thread-safety
  contract on the rest of the class).
- `GitWorker::loadCommitDiffBetween()` reuses the existing
  `commitDiffReady` signal with a synthetic SHA `"a..b"` so the host
  can distinguish single-commit from compare results without a new
  signal.

## [0.3.0] — 2026-05-05

### Added
- **Unit test suite** powered by QtTest. Opt-in build flag
  `-DGHM_BUILD_TESTS=ON` produces three test binaries that run via
  `ctest --output-on-failure`. No new external dependencies — uses
  the `Qt6::Test` component already shipped with Qt 6.
- Test coverage for the three pure-function parsers most exposed to
  user-supplied input: `parseRemotePaste`, `isValidBranchName`,
  `suggestRepoName`. Roughly 50 cases across the three suites,
  including the prompt-prefix tolerance, SSH URL forms, slash- and
  underscore-allowed branch names, Unicode handling in repo-name
  suggestions, and trailing-punctuation trimming.

### Changed
- **Refactor: testable parsers extracted to header-only files**.
  `AddRemoteDialog::parsePaste` → `ui/AddRemoteParser.h`,
  `CreateBranchDialog::nameLooksValid` → `ui/BranchNameValidator.h`,
  `PublishToGitHubDialog::suggestRepoName` → `ui/RepoNameSuggester.h`.
  Behaviour is unchanged; the dialogs now delegate to the shared
  inline functions.

### Internal
- `parseRemotePaste` accepts a few more syntactic variants the
  original allowed by accident (e.g. trailing tokens after the URL)
  but didn't document — now codified in tests.

## [0.2.0] — 2026-05-05

### Added
- **Visible application version** in the right-hand status bar so users
  can spot what build they're running without opening the About dialog.
- `CHANGELOG.md` for tracking releases.

### Changed
- **Refactor: MainWindow split into controllers.** Session, local
  workspace, and publish-to-GitHub flows are now owned by dedicated
  controller classes; `MainWindow` becomes a thin shell wiring them
  together.
- `Settings` and `SecureStorage` are stack-owned now (no manual
  new/delete), simplifying lifetime management.
- Common `relativeTime()` helper extracted to `core/TimeFormatting.h`,
  removing three near-identical copies across UI files.
- `Settings::defaultInitBranch()` validates the stored branch name
  against ref-format rules and falls back to `master` if the config
  file got corrupted or hand-edited into a bad state.
- Documented thread-safety contract on `GitHandler`.

## [0.1.0] — 2026-05-04

Initial public-feature-complete state. Includes everything below.

### Added
- **GitHub sign-in** via Personal Access Token, validated against the
  REST API and stored exclusively in the system keyring (libsecret).
- **Repository browser** in a sidebar with search, visibility badges,
  and local-clone indicators.
- **Clone / pull / push** through libgit2. Pull is fast-forward-only.
- **Publish a local folder to GitHub** with a single dialog: create a
  new repo or link an existing one, then optional `git push -u origin`
  in one click.
- **Local-folder workflow** with three tabs (Changes / History /
  Remotes), per-file stage/unstage, commit form, lazy-prompted author
  identity, manual remote add/remove, push.
- **Diff viewer** — unified-diff renderer with green/red/blue colour
  coding, line numbers, hunk headers. Used in both Changes (current
  working-tree changes) and History (per-commit diffs, like
  `git show <sha>`).
- **Find in diff (Ctrl+F)** — permanent search bar with live-highlight
  matches, prev/next navigation, case-sensitivity toggle, "X of Y"
  counter.
- **Branch management** via a popup picker in the local-folder header:
  switch branches, create with name validation (`CreateBranchDialog`),
  delete with smart force-delete prompt for unmerged branches.
- **Auto-refresh** through `QFileSystemWatcher` watching `.git/HEAD`,
  `.git/index`, and the working-tree root, debounced to 300 ms.
- **Multilingual UI** (English / Polish), switchable from
  *Settings → Language*. Default picked from system locale.
- **Support / Donate dialog** under *Help → Support / Donate…* with
  bank transfer details and a copy button on each row.
- **Dark theme** by default, toggleable via `--no-dark-mode`.

### Architecture
- Layered structure: `core/` (settings, keyring, translator),
  `github/` (REST client + repo model), `git/` (libgit2 wrapper +
  async worker), `ui/` (widgets and dialogs).
- Async work via `QtConcurrent::run` with `QFutureWatcher` marshalling
  back to the GUI thread.
