#include "core/Translator.h"

namespace ghm::core {

namespace {

// English → Polish phrasebook.
//
// Keys MUST match the source-language string exactly as it appears in
// tr() calls (no leading/trailing whitespace, exact punctuation,
// including HTML when the source has HTML). Mismatches mean Qt falls
// back to the English source — which is acceptable but loses the
// translation.
//
// New strings get added here as they show up. Keeping entries grouped
// by where they appear in the UI helps spot gaps when scanning.
QHash<QString, QString> polishTable()
{
    QHash<QString, QString> t;

    // ── Top-level shell ──────────────────────────────────────────────
    t[QStringLiteral("Main")]                          = QStringLiteral("Główny");
    t[QStringLiteral("Refresh")]                       = QStringLiteral("Odśwież");
    t[QStringLiteral("Sign out")]                      = QStringLiteral("Wyloguj");
    t[QStringLiteral("&File")]                         = QStringLiteral("&Plik");
    t[QStringLiteral("&Quit")]                         = QStringLiteral("Za&kończ");
    t[QStringLiteral("&Account")]                      = QStringLiteral("&Konto");
    t[QStringLiteral("&Help")]                         = QStringLiteral("Pomo&c");
    t[QStringLiteral("&About")]                        = QStringLiteral("&O programie");
    t[QStringLiteral("Add Local Folder…")]             = QStringLiteral("Dodaj lokalny katalog…");
    t[QStringLiteral("Add local folder")]              = QStringLiteral("Dodaj lokalny katalog");
    t[QStringLiteral("&Settings")]                     = QStringLiteral("&Ustawienia");
    t[QStringLiteral("&Language")]                     = QStringLiteral("&Język");
    t[QStringLiteral("&Support / Donate…")]            = QStringLiteral("&Wesprzyj rozwój…");
    t[QStringLiteral("Support / Donate")]              = QStringLiteral("Wesprzyj rozwój");
    t[QStringLiteral("Language changed")]              = QStringLiteral("Zmieniono język");
    t[QStringLiteral("Restart the application to apply the new language.")]
        = QStringLiteral("Uruchom program ponownie, aby zastosować nowy język.");

    // ── About box ────────────────────────────────────────────────────
    t[QStringLiteral("About GitHub Manager")]          = QStringLiteral("O programie GitHub Manager");

    // ── Sign-in / login ──────────────────────────────────────────────
    t[QStringLiteral("Validating saved credentials…")]
        = QStringLiteral("Sprawdzanie zapisanych poświadczeń…");
    t[QStringLiteral("Keyring unavailable: %1")]
        = QStringLiteral("Brak dostępu do pęku kluczy: %1");
    t[QStringLiteral("Sign out of %1? Your local clones and folders will be left intact.")]
        = QStringLiteral("Wylogować z %1? Lokalne klony i katalogi zostaną nietknięte.");
    t[QStringLiteral("Could not store credential")]    = QStringLiteral("Nie udało się zapisać tokena");
    t[QStringLiteral("Your token could not be saved to the system keyring:\n\n%1\n\n"
                     "You'll be asked to sign in again next time.")]
        = QStringLiteral("Nie udało się zapisać tokena w pęku kluczy systemu:\n\n%1\n\n"
                         "Przy następnym uruchomieniu zostaniesz poproszony o ponowne logowanie.");
    t[QStringLiteral("Authentication failed")]         = QStringLiteral("Uwierzytelnianie nie powiodło się");
    t[QStringLiteral("GitHub rejected the stored token:\n\n%1\n\nPlease sign in again.")]
        = QStringLiteral("GitHub odrzucił zapisany token:\n\n%1\n\nZaloguj się ponownie.");
    t[QStringLiteral("Signed out.")]                   = QStringLiteral("Wylogowano.");
    t[QStringLiteral("Signed in as %1")]               = QStringLiteral("Zalogowany jako %1");

    // ── Repository listing / sidebar ─────────────────────────────────
    t[QStringLiteral("Loading repositories…")]         = QStringLiteral("Ładowanie repozytoriów…");
    t[QStringLiteral("Loaded %1 repositories.")]       = QStringLiteral("Załadowano %1 repozytoriów.");
    t[QStringLiteral("Network error")]                 = QStringLiteral("Błąd sieci");
    t[QStringLiteral("Search repositories…")]          = QStringLiteral("Szukaj repozytoriów…");
    t[QStringLiteral("GitHub")]                        = QStringLiteral("GitHub");
    t[QStringLiteral("Local Folders")]                 = QStringLiteral("Lokalne katalogi");
    t[QStringLiteral("+ Add local folder…")]           = QStringLiteral("+ Dodaj lokalny katalog…");
    t[QStringLiteral("just now")]                      = QStringLiteral("przed chwilą");
    t[QStringLiteral("%1 min ago")]                    = QStringLiteral("%1 min temu");
    t[QStringLiteral("%1 h ago")]                      = QStringLiteral("%1 godz. temu");
    t[QStringLiteral("%1 d ago")]                      = QStringLiteral("%1 dni temu");
    t[QStringLiteral("private")]                       = QStringLiteral("prywatne");
    t[QStringLiteral("public")]                        = QStringLiteral("publiczne");
    t[QStringLiteral("Remove from sidebar")]           = QStringLiteral("Usuń z paska bocznego");
    t[QStringLiteral("Remove '%1' from the sidebar? The folder on disk is not affected.")]
        = QStringLiteral("Usunąć '%1' z paska bocznego? Katalog na dysku zostanie nietknięty.");

    // ── Clone / open / pull / push (GitHub flow) ─────────────────────
    t[QStringLiteral("Cannot clone")]                  = QStringLiteral("Nie można klonować");
    t[QStringLiteral("'%1' already exists. Choose a different folder.")]
        = QStringLiteral("'%1' już istnieje. Wybierz inny katalog.");
    t[QStringLiteral("Cloning %1…")]                   = QStringLiteral("Klonowanie %1…");
    t[QStringLiteral("Cloned to %1")]                  = QStringLiteral("Sklonowano do %1");
    t[QStringLiteral("Clone failed")]                  = QStringLiteral("Klonowanie nieudane");
    t[QStringLiteral("Choose existing local clone of %1")]
        = QStringLiteral("Wybierz istniejący lokalny klon %1");
    t[QStringLiteral("Not a git repository")]          = QStringLiteral("To nie jest repozytorium git");
    t[QStringLiteral("'%1' does not contain a .git directory.")]
        = QStringLiteral("'%1' nie zawiera katalogu .git.");
    t[QStringLiteral("Pulling %1…")]                   = QStringLiteral("Pobieranie %1…");
    t[QStringLiteral("Pull failed")]                   = QStringLiteral("Pull nie powiódł się");
    t[QStringLiteral("Pull complete.")]                = QStringLiteral("Pull zakończony.");
    t[QStringLiteral("Pushing %1…")]                   = QStringLiteral("Wysyłanie %1…");
    t[QStringLiteral("Push failed")]                   = QStringLiteral("Push nie powiódł się");
    t[QStringLiteral("Push complete.")]                = QStringLiteral("Push zakończony.");
    t[QStringLiteral("Switching to %1…")]              = QStringLiteral("Przełączanie na %1…");
    t[QStringLiteral("Branch switch failed")]          = QStringLiteral("Zmiana gałęzi nie powiodła się");
    t[QStringLiteral("Now on %1")]                     = QStringLiteral("Teraz na %1");

    // ── Local folder workflow ────────────────────────────────────────
    t[QStringLiteral("Folder is already in the sidebar.")]
        = QStringLiteral("Katalog jest już w pasku bocznym.");
    t[QStringLiteral("Initializing %1…")]              = QStringLiteral("Inicjalizacja %1…");
    t[QStringLiteral("Initialize failed")]             = QStringLiteral("Inicjalizacja nie powiodła się");
    t[QStringLiteral("Initialized empty repository in %1")]
        = QStringLiteral("Zainicjowano puste repozytorium w %1");
    t[QStringLiteral("Folder is not a Git repository — initialize to start.")]
        = QStringLiteral("Katalog nie jest repozytorium Git — zainicjuj, aby rozpocząć.");
    t[QStringLiteral("On %1 — %2")]                    = QStringLiteral("Na %1 — %2");
    t[QStringLiteral("Working tree clean")]            = QStringLiteral("Drzewo robocze czyste");
    t[QStringLiteral("%1 changed file(s)")]            = QStringLiteral("%1 zmienionych plików");
    t[QStringLiteral("Stage failed")]                  = QStringLiteral("Dodawanie do indeksu nieudane");
    t[QStringLiteral("Unstage failed")]                = QStringLiteral("Wycofanie z indeksu nieudane");
    t[QStringLiteral("Committing…")]                   = QStringLiteral("Commitowanie…");
    t[QStringLiteral("Commit failed")]                 = QStringLiteral("Commit nieudany");
    t[QStringLiteral("Committed %1")]                  = QStringLiteral("Zatwierdzono %1");
    t[QStringLiteral("Commit cancelled — author identity is required.")]
        = QStringLiteral("Commit anulowany — wymagane są dane autora.");
    t[QStringLiteral("Remove remote")]                 = QStringLiteral("Usuń remote");
    t[QStringLiteral("Remove the '%1' remote? This only affects the local "
                     "configuration.")]
        = QStringLiteral("Usunąć remote '%1'? Wpłynie tylko na lokalną konfigurację.");
    t[QStringLiteral("Remote operation failed")]       = QStringLiteral("Operacja na remote nieudana");
    t[QStringLiteral("Sign in required")]              = QStringLiteral("Wymagane logowanie");
    t[QStringLiteral("Push uses your GitHub personal access token. Please sign in first.")]
        = QStringLiteral("Push używa twojego tokena GitHub. Zaloguj się najpierw.");
    t[QStringLiteral("Pushing %1 → %2…")]              = QStringLiteral("Wysyłanie %1 → %2…");
    t[QStringLiteral("Pushing %1 → origin…")]          = QStringLiteral("Wysyłanie %1 → origin…");

    // ── LocalRepositoryWidget UI ─────────────────────────────────────
    t[QStringLiteral("This folder is not a Git repository yet.")]
        = QStringLiteral("Ten katalog nie jest jeszcze repozytorium Git.");
    t[QStringLiteral("Initializing creates a hidden <code>.git</code> directory and prepares the "
                     "folder for tracking changes. Pick the name of the initial branch — "
                     "<b>master</b> matches GitHub's default 'git push origin master' instructions; "
                     "<b>main</b> is the modern default for new GitHub repos.")]
        = QStringLiteral("Inicjalizacja tworzy ukryty katalog <code>.git</code> i przygotowuje katalog "
                         "do śledzenia zmian. Wybierz nazwę głównej gałęzi — <b>master</b> pasuje "
                         "do domyślnych instrukcji GitHuba 'git push origin master'; <b>main</b> "
                         "to nowoczesny domyślny wybór dla nowych repo na GitHubie.");
    t[QStringLiteral("Initial branch:")]                = QStringLiteral("Pierwsza gałąź:");
    t[QStringLiteral("Initialize Repository")]          = QStringLiteral("Zainicjuj repozytorium");
    t[QStringLiteral("Changes")]                        = QStringLiteral("Zmiany");
    t[QStringLiteral("History")]                        = QStringLiteral("Historia");
    t[QStringLiteral("Remotes")]                        = QStringLiteral("Remote'y");
    t[QStringLiteral("Stage selected")]                 = QStringLiteral("Dodaj zaznaczone");
    t[QStringLiteral("Unstage selected")]               = QStringLiteral("Wycofaj zaznaczone");
    t[QStringLiteral("Stage all")]                      = QStringLiteral("Dodaj wszystkie");
    t[QStringLiteral("Stage")]                          = QStringLiteral("Dodaj do indeksu");
    t[QStringLiteral("Unstage")]                        = QStringLiteral("Wycofaj z indeksu");
    t[QStringLiteral("Commit message — first line is the summary, blank line, then optional details.")]
        = QStringLiteral("Treść commita — pierwsza linia to streszczenie, pusta linia, "
                         "potem opcjonalne szczegóły.");
    t[QStringLiteral("Commit")]                         = QStringLiteral("Commit");
    t[QStringLiteral("Will be committed as <b>%1</b> &lt;%2&gt;.")]
        = QStringLiteral("Zostanie zatwierdzony jako <b>%1</b> &lt;%2&gt;.");
    t[QStringLiteral("Author identity not set — you'll be asked for your name and email "
                     "when you commit.")]
        = QStringLiteral("Dane autora nie są ustawione — zostaniesz o nie zapytany przy commicie.");
    t[QStringLiteral("✓ Working tree is clean — nothing to commit.")]
        = QStringLiteral("✓ Drzewo robocze jest czyste — nic do zatwierdzenia.");
    t[QStringLiteral("Author: not configured")]         = QStringLiteral("Autor: nie skonfigurowano");
    t[QStringLiteral("Author: %1 <%2>")]                = QStringLiteral("Autor: %1 <%2>");
    t[QStringLiteral("Edit")]                           = QStringLiteral("Edytuj");
    t[QStringLiteral("Select a commit to see its full message.")]
        = QStringLiteral("Wybierz commit, aby zobaczyć pełną wiadomość.");
    t[QStringLiteral("No commits yet — make your first commit in the Changes tab.")]
        = QStringLiteral("Brak commitów — utwórz pierwszy w zakładce Zmiany.");
    t[QStringLiteral("Add remote…")]                    = QStringLiteral("Dodaj remote…");
    t[QStringLiteral("Remove")]                         = QStringLiteral("Usuń");
    t[QStringLiteral("No remotes — click 'Add remote…' and paste your "
                     "'git remote add origin …' command.")]
        = QStringLiteral("Brak remote'ów — kliknij 'Dodaj remote…' i wklej swoją komendę "
                         "'git remote add origin …'.");
    t[QStringLiteral("Push")]                           = QStringLiteral("Wyślij");
    t[QStringLiteral("Remote:")]                        = QStringLiteral("Remote:");
    t[QStringLiteral("Branch:")]                        = QStringLiteral("Gałąź:");
    t[QStringLiteral("Set upstream (-u)")]              = QStringLiteral("Ustaw upstream (-u)");
    t[QStringLiteral("Equivalent to 'git push -u origin <branch>'. Recommended for the first push.")]
        = QStringLiteral("Odpowiednik 'git push -u origin <gałąź>'. Zalecane dla pierwszego push'a.");

    // Status entry tooltips
    t[QStringLiteral("Staged: new file")]               = QStringLiteral("W indeksie: nowy plik");
    t[QStringLiteral("Staged: modified")]               = QStringLiteral("W indeksie: zmodyfikowany");
    t[QStringLiteral("Staged: deleted")]                = QStringLiteral("W indeksie: usunięty");
    t[QStringLiteral("Staged: renamed")]                = QStringLiteral("W indeksie: przemianowany");
    t[QStringLiteral("Staged: type changed")]           = QStringLiteral("W indeksie: zmiana typu");
    t[QStringLiteral("Unstaged: modified")]             = QStringLiteral("Niezaindeksowane: zmodyfikowany");
    t[QStringLiteral("Unstaged: deleted")]              = QStringLiteral("Niezaindeksowane: usunięty");
    t[QStringLiteral("Unstaged: renamed")]              = QStringLiteral("Niezaindeksowane: przemianowany");
    t[QStringLiteral("Unstaged: type changed")]         = QStringLiteral("Niezaindeksowane: zmiana typu");
    t[QStringLiteral("Untracked (new file, not added)")]= QStringLiteral("Nieśledzony (nowy, niedodany)");
    t[QStringLiteral("⚠ Merge conflict")]               = QStringLiteral("⚠ Konflikt scalania");

    // ── Diff viewer ──────────────────────────────────────────────────
    t[QStringLiteral("Select a file in the list above to see its diff.")]
        = QStringLiteral("Wybierz plik z listy powyżej, aby zobaczyć diff.");
    t[QStringLiteral("Loading diff for %1…")]
        = QStringLiteral("Ładowanie diffa dla %1…");
    t[QStringLiteral("Could not load diff: %1")]
        = QStringLiteral("Nie udało się załadować diffa: %1");
    t[QStringLiteral("%1 is a binary file — diff is not displayed.")]
        = QStringLiteral("%1 to plik binarny — diff nie jest wyświetlany.");
    t[QStringLiteral("No changes for %1 in this scope.")]
        = QStringLiteral("Brak zmian w %1 w tym zakresie.");

    // ── Search bar (Ctrl+F in diff) ──────────────────────────────────
    t[QStringLiteral("Find in diff  (Ctrl+F)")]       = QStringLiteral("Szukaj w diffie  (Ctrl+F)");
    t[QStringLiteral("Search the diff. Press Enter for next match, "
                     "Shift+Enter for previous. Esc to clear.")]
        = QStringLiteral("Szukaj w diffie. Enter — następne trafienie, "
                         "Shift+Enter — poprzednie. Esc czyści.");
    t[QStringLiteral("Previous match (Shift+Enter)")] = QStringLiteral("Poprzednie trafienie (Shift+Enter)");
    t[QStringLiteral("Next match (Enter)")]           = QStringLiteral("Następne trafienie (Enter)");
    t[QStringLiteral("Match case")]                   = QStringLiteral("Uwzględniaj wielkość liter");
    t[QStringLiteral("Clear search (Esc)")]           = QStringLiteral("Wyczyść wyszukiwanie (Esc)");
    t[QStringLiteral("no matches")]                   = QStringLiteral("brak trafień");
    t[QStringLiteral("%1 of %2")]                     = QStringLiteral("%1 z %2");
    t[QStringLiteral("%1 matches")]                   = QStringLiteral("%1 trafień");

    // ── Branch management ────────────────────────────────────────────
    t[QStringLiteral("(no branches yet — make a commit first)")]
        = QStringLiteral("(brak gałęzi — utwórz najpierw commit)");
    t[QStringLiteral("Create new branch…")]
        = QStringLiteral("Utwórz nową gałąź…");
    t[QStringLiteral("Delete branch…")]
        = QStringLiteral("Usuń gałąź…");
    t[QStringLiteral("Create branch")]                = QStringLiteral("Utwórz gałąź");
    t[QStringLiteral("Switch to the new branch after creating")]
        = QStringLiteral("Przełącz na nową gałąź po utworzeniu");
    t[QStringLiteral("Equivalent to 'git checkout -b <name>'. Uncheck to create the branch "
                     "without switching to it (like 'git branch <name>').")]
        = QStringLiteral("Odpowiednik 'git checkout -b <nazwa>'. Odznacz, aby utworzyć "
                         "gałąź bez przełączania (jak 'git branch <nazwa>').");
    t[QStringLiteral("The new branch will point at the current HEAD.")]
        = QStringLiteral("Nowa gałąź będzie wskazywać na bieżący HEAD.");
    t[QStringLiteral("The new branch will be created from <b>%1</b> "
                     "(your current branch).")]
        = QStringLiteral("Nowa gałąź zostanie utworzona z <b>%1</b> "
                         "(twoja bieżąca gałąź).");
    t[QStringLiteral("Name is empty.")]                = QStringLiteral("Nazwa jest pusta.");
    t[QStringLiteral("Cannot start with '-'.")]        = QStringLiteral("Nie może zaczynać się od '-'.");
    t[QStringLiteral("Cannot start with '.'.")]        = QStringLiteral("Nie może zaczynać się od '.'.");
    t[QStringLiteral("Cannot end with '/'.")]          = QStringLiteral("Nie może kończyć się '/'.");
    t[QStringLiteral("Cannot end with '.'.")]          = QStringLiteral("Nie może kończyć się '.'.");
    t[QStringLiteral("Reserved name.")]                = QStringLiteral("Zarezerwowana nazwa.");
    t[QStringLiteral("Cannot contain '..'.")]          = QStringLiteral("Nie może zawierać '..'.");
    t[QStringLiteral("Cannot contain '@{'.")]          = QStringLiteral("Nie może zawierać '@{'.");
    t[QStringLiteral("Cannot contain whitespace.")]    = QStringLiteral("Nie może zawierać spacji.");
    t[QStringLiteral("Cannot contain '%1'.")]          = QStringLiteral("Nie może zawierać '%1'.");
    t[QStringLiteral("Cannot contain control characters.")]
        = QStringLiteral("Nie może zawierać znaków sterujących.");
    t[QStringLiteral("A branch named '%1' already exists.")]
        = QStringLiteral("Gałąź o nazwie '%1' już istnieje.");
    t[QStringLiteral("Delete branch")]                 = QStringLiteral("Usuń gałąź");
    t[QStringLiteral("Delete the local branch <b>%1</b>?<br><br>"
                     "This is a local-only operation; nothing on GitHub is affected.")]
        = QStringLiteral("Usunąć lokalną gałąź <b>%1</b>?<br><br>"
                         "To operacja czysto lokalna; niczego nie zmienia na GitHubie.");
    t[QStringLiteral("Branch is not merged")]
        = QStringLiteral("Gałąź nie jest scalona");
    t[QStringLiteral("<b>%1</b> contains commits that aren't reachable from "
                     "your current branch.<br><br>%2<br><br>"
                     "Force-delete anyway? <b>The unique commits will be lost</b> "
                     "unless they're referenced from another branch.")]
        = QStringLiteral("<b>%1</b> zawiera commity nieosiągalne z twojej bieżącej gałęzi."
                         "<br><br>%2<br><br>"
                         "Wymusić usunięcie? <b>Unikalne commity zostaną utracone</b>, "
                         "chyba że są wskazane przez inną gałąź.");
    t[QStringLiteral("Create branch failed")]          = QStringLiteral("Tworzenie gałęzi nieudane");
    t[QStringLiteral("Delete branch failed")]          = QStringLiteral("Usuwanie gałęzi nieudane");
    t[QStringLiteral("Created branch %1.")]            = QStringLiteral("Utworzono gałąź %1.");
    t[QStringLiteral("Deleted branch %1.")]            = QStringLiteral("Usunięto gałąź %1.");
    t[QStringLiteral("Creating branch %1…")]           = QStringLiteral("Tworzenie gałęzi %1…");
    t[QStringLiteral("Deleting branch %1…")]           = QStringLiteral("Usuwanie gałęzi %1…");
    t[QStringLiteral("Force-deleting branch %1…")]     = QStringLiteral("Wymuszanie usunięcia gałęzi %1…");
    t[QStringLiteral("(no upstream)")]                 = QStringLiteral("(brak upstreama)");

    // ── Identity dialog ──────────────────────────────────────────────
    t[QStringLiteral("Git author identity")]            = QStringLiteral("Tożsamość autora Git");
    t[QStringLiteral("Set git author identity")]        = QStringLiteral("Ustaw tożsamość autora Git");
    t[QStringLiteral("These appear in every commit you create. They're stored only on this "
                     "computer (Settings) and never sent to GitHub directly.")]
        = QStringLiteral("Pojawiają się przy każdym tworzonym commicie. Przechowywane tylko na tym "
                         "komputerze (Ustawienia) i nigdy nie są wysyłane bezpośrednio do GitHuba.");
    t[QStringLiteral("Name:")]                          = QStringLiteral("Imię i nazwisko:");
    t[QStringLiteral("Email:")]                         = QStringLiteral("Email:");

    // ── Add Remote dialog ────────────────────────────────────────────
    t[QStringLiteral("Add Git remote")]                 = QStringLiteral("Dodaj remote Git");
    t[QStringLiteral("Paste 'git remote add origin https://…' or just the URL")]
        = QStringLiteral("Wklej 'git remote add origin https://…' lub sam URL");
    t[QStringLiteral("Set as upstream when I push (-u)")]
        = QStringLiteral("Ustaw jako upstream przy push (-u)");
    t[QStringLiteral("Set as upstream when I push (git push -u %1 %2)")]
        = QStringLiteral("Ustaw jako upstream przy push (git push -u %1 %2)");
    t[QStringLiteral("Tip: GitHub shows you a 'git remote add origin …' command on a freshly "
                     "created empty repo. You can paste the whole line above and we'll fill "
                     "the fields in for you.")]
        = QStringLiteral("Wskazówka: GitHub pokazuje komendę 'git remote add origin …' przy "
                         "świeżo utworzonym pustym repo. Wklej całą linię u góry, a sami "
                         "wypełnimy pola.");
    t[QStringLiteral("Paste:")]                         = QStringLiteral("Wklej:");
    t[QStringLiteral("URL:")]                           = QStringLiteral("URL:");
    t[QStringLiteral("⚠ This is an SSH URL. The app authenticates with your GitHub "
                     "personal access token over HTTPS. SSH URLs require a configured "
                     "ssh-agent or key — push may fail. Consider using the HTTPS URL "
                     "instead (https://github.com/owner/repo.git).")]
        = QStringLiteral("⚠ To jest URL SSH. Aplikacja uwierzytelnia się tokenem GitHub "
                         "przez HTTPS. Adresy SSH wymagają skonfigurowanego ssh-agenta lub "
                         "klucza — push może się nie powieść. Rozważ użycie URL HTTPS "
                         "(https://github.com/owner/repo.git).");
    t[QStringLiteral("⚠ Unrecognised URL scheme. Push is supported for HTTPS GitHub "
                     "URLs (https://github.com/owner/repo.git).")]
        = QStringLiteral("⚠ Nierozpoznany schemat URL. Push działa z URL-ami HTTPS GitHuba "
                         "(https://github.com/owner/repo.git).");

    // ── Publish to GitHub dialog ─────────────────────────────────────
    t[QStringLiteral("Publish to GitHub")]              = QStringLiteral("Opublikuj na GitHubie");
    t[QStringLiteral("Publish to GitHub…")]             = QStringLiteral("Opublikuj na GitHubie…");
    t[QStringLiteral("Publish \"%1\" to GitHub")]       = QStringLiteral("Opublikuj \"%1\" na GitHubie");
    t[QStringLiteral("Sets up the <code>origin</code> remote on this folder and, if you want, "
                     "pushes your commits in one go.")]
        = QStringLiteral("Ustawia remote <code>origin</code> dla tego katalogu i opcjonalnie "
                         "wysyła commity jednym kliknięciem.");
    t[QStringLiteral("Create a new GitHub repository")]
        = QStringLiteral("Utwórz nowe repozytorium na GitHubie");
    t[QStringLiteral("Link to an existing repository")]
        = QStringLiteral("Połącz z istniejącym repozytorium");
    t[QStringLiteral("Owner:")]                         = QStringLiteral("Właściciel:");
    t[QStringLiteral("(not signed in)")]                = QStringLiteral("(niezalogowany)");
    t[QStringLiteral("Description:")]                   = QStringLiteral("Opis:");
    t[QStringLiteral("Optional description shown on GitHub")]
        = QStringLiteral("Opcjonalny opis widoczny na GitHubie");
    t[QStringLiteral("Visibility:")]                    = QStringLiteral("Widoczność:");
    t[QStringLiteral("Public")]                         = QStringLiteral("Publiczne");
    t[QStringLiteral("Private")]                        = QStringLiteral("Prywatne");
    t[QStringLiteral("Search your repositories…")]      = QStringLiteral("Szukaj swoich repozytoriów…");
    t[QStringLiteral("Pick the empty GitHub repository you just created. "
                     "If your list is out of date, close this dialog and click "
                     "Refresh in the toolbar.")]
        = QStringLiteral("Wybierz puste repozytorium GitHub, które właśnie utworzyłeś. "
                         "Jeśli lista jest nieaktualna, zamknij ten dialog i kliknij "
                         "Odśwież na pasku narzędzi.");
    t[QStringLiteral("Push my commits after publishing")]
        = QStringLiteral("Wyślij moje commity po opublikowaniu");
    t[QStringLiteral("Equivalent to running 'git push -u origin <branch>' immediately after "
                     "wiring up the remote. Recommended for an empty GitHub repository.")]
        = QStringLiteral("Odpowiednik 'git push -u origin <gałąź>' bezpośrednio po podpięciu "
                         "remote'a. Zalecane dla pustego repo na GitHubie.");
    t[QStringLiteral("Uses your saved GitHub token over HTTPS. The token is never written "
                     "to <code>.git/config</code>.")]
        = QStringLiteral("Używa zapisanego tokena GitHub przez HTTPS. Token nigdy nie jest "
                         "zapisywany w <code>.git/config</code>.");
    t[QStringLiteral("Create && Publish")]              = QStringLiteral("Utwórz i opublikuj");
    t[QStringLiteral("Link && Publish")]                = QStringLiteral("Połącz i opublikuj");
    t[QStringLiteral("⚠ You already have a repository called \"%1\". "
                     "GitHub will reject this name — choose a different one, "
                     "or switch to \"Link to an existing repository\".")]
        = QStringLiteral("⚠ Masz już repozytorium o nazwie \"%1\". GitHub odrzuci tę nazwę "
                         "— wybierz inną lub przełącz na \"Połącz z istniejącym repozytorium\".");
    t[QStringLiteral("⚠ GitHub repository names may only contain letters, "
                     "digits, hyphens, underscores, and dots.")]
        = QStringLiteral("⚠ Nazwy repozytoriów GitHub mogą zawierać tylko litery, cyfry, "
                         "myślniki, podkreślenia i kropki.");
    t[QStringLiteral(" • already cloned")]              = QStringLiteral(" • już sklonowane");
    t[QStringLiteral("Already publishing")]             = QStringLiteral("Trwa publikowanie");
    t[QStringLiteral("Another publish operation is in progress — please wait for it to finish.")]
        = QStringLiteral("Trwa już publikowanie — poczekaj na zakończenie.");
    t[QStringLiteral("Publishing requires being signed in to GitHub. Please sign in first.")]
        = QStringLiteral("Publikowanie wymaga zalogowania do GitHuba. Zaloguj się najpierw.");
    t[QStringLiteral("Creating GitHub repository \"%1\"…")]
        = QStringLiteral("Tworzenie repozytorium GitHub \"%1\"…");
    t[QStringLiteral("Linking %1 → %2…")]               = QStringLiteral("Łączenie %1 → %2…");
    t[QStringLiteral("Created %1 — wiring up origin…")] = QStringLiteral("Utworzono %1 — podpinanie origin…");
    t[QStringLiteral("Created %1.")]                    = QStringLiteral("Utworzono %1.");
    t[QStringLiteral("Connected to %1.")]               = QStringLiteral("Podłączono do %1.");
    t[QStringLiteral("Pushing to GitHub…")]             = QStringLiteral("Wysyłanie na GitHuba…");
    t[QStringLiteral("Published to GitHub.")]           = QStringLiteral("Opublikowano na GitHubie.");
    t[QStringLiteral("Publish failed")]                 = QStringLiteral("Publikowanie nie powiodło się");
    t[QStringLiteral("Publish failed at push")]         = QStringLiteral("Publikowanie nie powiodło się na etapie push");
    t[QStringLiteral("Nothing to push yet")]            = QStringLiteral("Nic do wysłania");
    t[QStringLiteral("The remote is connected, but this branch has no commits. "
                     "Make a commit and then push from the Remotes tab.")]
        = QStringLiteral("Remote jest podłączony, ale ta gałąź nie ma commitów. "
                         "Utwórz commit, a potem wyślij z zakładki Remote'y.");
    t[QStringLiteral("The GitHub repository was created (or selected), but wiring up "
                     "the local 'origin' remote failed:\n\n%1\n\n"
                     "You can add it manually with:\n  git remote add origin %2")]
        = QStringLiteral("Repozytorium GitHub zostało utworzone (lub wybrane), ale podpięcie "
                         "lokalnego remote'a 'origin' nie powiodło się:\n\n%1\n\n"
                         "Możesz dodać go ręcznie:\n  git remote add origin %2");
    t[QStringLiteral("The selected repository has no clone URL. Try refreshing the list.")]
        = QStringLiteral("Wybrane repozytorium nie ma adresu klonowania. Spróbuj odświeżyć listę.");
    t[QStringLiteral("The remote is connected, but pushing your commits failed:\n\n%1\n\n"
                     "You can retry from the Remotes tab.")]
        = QStringLiteral("Remote jest podłączony, ale wysłanie commitów nie powiodło się:\n\n%1\n\n"
                         "Możesz spróbować ponownie z zakładki Remote'y.");
    t[QStringLiteral("Ready to publish?")]              = QStringLiteral("Gotowy do publikacji?");
    t[QStringLiteral("<b>Ready to publish?</b><br>"
                     "This folder isn't connected to a GitHub repository yet. "
                     "Create a new one or link to an existing repo to push your commits.")]
        = QStringLiteral("<b>Gotowy do publikacji?</b><br>"
                         "Ten katalog nie jest jeszcze podłączony do repozytorium GitHub. "
                         "Utwórz nowe lub połącz z istniejącym, aby wysłać swoje commity.");

    // ── Support / Donate dialog ──────────────────────────────────────
    t[QStringLiteral("Support GitHub Manager")]
        = QStringLiteral("Wesprzyj GitHub Manager");
    t[QStringLiteral("Hi! I'm %1, the author of this app.")]
        = QStringLiteral("Cześć! Jestem %1, autorem tego programu.");
    t[QStringLiteral("GitHub Manager is something I build in my spare time. Every donation helps "
                     "me keep the project alive — fixing bugs, adding features, and supporting "
                     "Linux as a first-class platform for developers. Thank you for any support — "
                     "it genuinely makes a difference. ❤")]
        = QStringLiteral("GitHub Manager rozwijam w wolnym czasie. Każda wpłata pomaga mi utrzymać "
                         "projekt — naprawiać błędy, dodawać nowe funkcje i wspierać Linuksa jako "
                         "pierwszorzędną platformę dla programistów. Z góry dziękuję za każde "
                         "wsparcie — naprawdę robi różnicę. ❤");
    t[QStringLiteral("Bank transfer details")]          = QStringLiteral("Dane do przelewu");
    t[QStringLiteral("Bank")]                           = QStringLiteral("Bank");
    t[QStringLiteral("Account number")]                 = QStringLiteral("Numer konta");
    t[QStringLiteral("Title")]                          = QStringLiteral("Tytuł przelewu");
    t[QStringLiteral("Recipient")]                      = QStringLiteral("Odbiorca");
    t[QStringLiteral("Copy")]                           = QStringLiteral("Kopiuj");
    t[QStringLiteral("Copied!")]                        = QStringLiteral("Skopiowano!");
    t[QStringLiteral("Close")]                          = QStringLiteral("Zamknij");
    t[QStringLiteral("Author")]                         = QStringLiteral("Autor");
    t[QStringLiteral("This account is in PLN. International transfers are also welcome — "
                     "please contact me first for the IBAN/SWIFT format.")]
        = QStringLiteral("To konto prowadzone jest w PLN. Przelewy międzynarodowe również są "
                         "mile widziane — proszę o kontakt po format IBAN/SWIFT.");

    // ── Misc messageboxes ────────────────────────────────────────────
    t[QStringLiteral("Sign out")]                       = QStringLiteral("Wyloguj");
    t[QStringLiteral("Quit")]                           = QStringLiteral("Zakończ");
    t[QStringLiteral("Yes")]                            = QStringLiteral("Tak");
    t[QStringLiteral("No")]                             = QStringLiteral("Nie");
    t[QStringLiteral("Cancel")]                         = QStringLiteral("Anuluj");
    t[QStringLiteral("OK")]                             = QStringLiteral("OK");

    return t;
}

} // namespace

Translator::Translator(QObject* parent)
    : QTranslator(parent)
    , lang_(QStringLiteral("en"))
{
}

void Translator::setLanguage(const QString& code)
{
    lang_ = code;
    if (lang_ == QLatin1String("pl")) {
        if (table_.isEmpty()) table_ = polishTable();
    } else {
        // For "en" (and any unknown code) we leave the table populated
        // but check the code in translate() to decide whether to consult
        // it. Keeping it warm avoids a rebuild if the user toggles back.
    }
}

QString Translator::translate(const char* /*context*/,
                              const char* sourceText,
                              const char* /*disambiguation*/,
                              int         /*n*/) const
{
    if (lang_ != QLatin1String("pl") || !sourceText) return {};

    const QString key = QString::fromUtf8(sourceText);
    auto it = table_.constFind(key);
    if (it == table_.constEnd()) {
        // No translation — return empty so Qt falls back to the source.
        return {};
    }
    return *it;
}

} // namespace ghm::core
