#pragma once

// AddRemoteParser - pure-function parsing of what users paste into the
// "Add Remote" dialog. Lives in its own header (no QtWidgets dep) so it
// can be exercised by unit tests without spinning up a QApplication.
//
// Two forms are recognised:
//   1. `git remote add <name> <url>`     — copy-pasted from GitHub's
//      "create new repo" instruction block. The optional shell prompt
//      prefixes ("$ ", "> ") are tolerated.
//   2. Bare URL — a single token that looks like https://, http://,
//      git://, ssh://, or git@host:path. We assume the user wants
//      to call the remote "origin" (the conventional default).
//
// Returning a Parsed{ok=false} means we couldn't recognise either
// form; the caller should leave the dialog fields untouched and let
// the user fill them in manually.

#include <QString>
#include <QRegularExpression>

namespace ghm::ui {

struct ParsedRemote {
    QString name;          // empty when ok==false; "origin" by default for bare URLs
    QString url;           // empty when ok==false
    bool    ok{false};
};

// The function is inline so it can ship in a header without forcing
// a separate compilation unit. It's small (~30 lines), no perf hit.
inline ParsedRemote parseRemotePaste(const QString& raw)
{
    ParsedRemote out;
    QString s = raw.trimmed();
    if (s.isEmpty()) return out;

    // Tolerate shell prompt prefixes ("$ ", "> ") that some users
    // copy along with the command from terminal screenshots.
    if (s.startsWith(QLatin1String("$ "))) s.remove(0, 2);
    else if (s.startsWith(QLatin1String("> "))) s.remove(0, 2);

    // Form 1: `git remote add <name> <url>`. We accept extra trailing
    // tokens (some users copy "git remote add origin <url> --tracking"
    // or similar). Two captured groups are enough.
    static const QRegularExpression cmdRe(
        QStringLiteral(R"(^\s*git\s+remote\s+add\s+(\S+)\s+(\S+).*$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = cmdRe.match(s); m.hasMatch()) {
        out.name = m.captured(1);
        out.url  = m.captured(2);
        out.ok   = !out.url.isEmpty();
        return out;
    }

    // Form 2: bare URL, recognized via known schemes. The regex avoids
    // matching arbitrary text — we want to fail fast for "this is just
    // a description" rather than guess.
    static const QRegularExpression urlRe(
        QStringLiteral(R"(^(https?://\S+|git@\S+:\S+|ssh://\S+|git://\S+)$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (auto m = urlRe.match(s); m.hasMatch()) {
        out.url  = m.captured(1);
        out.name = QStringLiteral("origin");
        out.ok   = true;
        return out;
    }

    return out;
}

} // namespace ghm::ui
