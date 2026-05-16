#pragma once

// BranchNameValidator - subset of git-check-ref-format rules.
//
// Lives in its own header (no QtWidgets dep) so unit tests can run it
// without a QApplication. The implementation mirrors what
// `git check-ref-format` would reject, but covers only the rules that
// users hit in practice — libgit2 will reject anything else with its
// own error.
//
// Rules enforced:
//   * cannot be empty
//   * cannot contain whitespace, control chars, DEL, or any of
//     ~ ^ : ? * [ (backslash)
//   * cannot start with '-' or '.'
//   * cannot end with '/' or '.'
//   * cannot contain ".." or "@{"
//   * cannot be exactly "HEAD" or "@"
//
// Translation note: rejection messages go through QObject::tr() so
// they participate in the app's translation system. In tests (which
// don't load translations) they come out untranslated, which matches
// what users see on first run.

#include <QString>
#include <QObject>

namespace ghm::ui {

// Returns true if `s` is a syntactically valid local branch name.
// On false, `whyNot` (if non-null) gets a short user-facing reason.
inline bool isValidBranchName(const QString& s, QString* whyNot = nullptr)
{
    auto fail = [&](QString reason) {
        if (whyNot) *whyNot = std::move(reason);
        return false;
    };

    if (s.isEmpty())                       return fail(QObject::tr("Name is empty."));
    if (s.startsWith(QLatin1Char('-')))    return fail(QObject::tr("Cannot start with '-'."));
    if (s.startsWith(QLatin1Char('.')))    return fail(QObject::tr("Cannot start with '.'."));
    if (s.endsWith(QLatin1Char('/')))      return fail(QObject::tr("Cannot end with '/'."));
    if (s.endsWith(QLatin1Char('.')))      return fail(QObject::tr("Cannot end with '.'."));
    if (s == QLatin1String("HEAD") ||
        s == QLatin1String("@"))            return fail(QObject::tr("Reserved name."));
    if (s.contains(QLatin1String("..")))   return fail(QObject::tr("Cannot contain '..'."));
    if (s.contains(QLatin1String("@{")))   return fail(QObject::tr("Cannot contain '@{'."));

    for (QChar c : s) {
        if (c.isSpace())                    return fail(QObject::tr("Cannot contain whitespace."));
        const ushort u = c.unicode();
        if (u == '~' || u == '^' || u == ':' || u == '?' ||
            u == '*' || u == '[' || u == '\\') {
            return fail(QObject::tr("Cannot contain '%1'.").arg(c));
        }
        if (u < 0x20 || u == 0x7F) {
            return fail(QObject::tr("Cannot contain control characters."));
        }
    }
    return true;
}

} // namespace ghm::ui
