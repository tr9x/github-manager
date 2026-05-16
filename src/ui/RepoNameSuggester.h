#pragma once

// RepoNameSuggester - turns a folder name into a GitHub-friendly
// repo name suggestion.
//
// GitHub's actual rule: name may contain only ASCII letters, digits,
// hyphen, underscore, and dot. We additionally collapse runs of
// hyphens (so "my  project" becomes "my-project", not "my--project")
// and trim leading/trailing punctuation that GitHub rejects at the
// edges.
//
// Empty result means the input had nothing salvageable (e.g. a
// folder name composed entirely of slashes or non-ASCII characters).
// The dialog handles that by falling back to whatever the user types
// manually.

#include <QString>

namespace ghm::ui {

inline QString suggestRepoName(const QString& folderName)
{
    QString out;
    out.reserve(folderName.size());
    bool prevHyphen = false;
    for (QChar c : folderName) {
        if (c.isLetterOrNumber() || c == QLatin1Char('-')
                                 || c == QLatin1Char('_')
                                 || c == QLatin1Char('.')) {
            out.append(c);
            prevHyphen = (c == QLatin1Char('-'));
        } else {
            // Collapse non-allowed chars to a single hyphen, but skip
            // emitting one if the previous char was already a hyphen
            // (avoids "foo bar" → "foo--bar").
            if (!prevHyphen && !out.isEmpty()) out.append(QLatin1Char('-'));
            prevHyphen = true;
        }
    }
    while (!out.isEmpty() && (out.endsWith(QLatin1Char('-')) ||
                              out.endsWith(QLatin1Char('.')))) {
        out.chop(1);
    }
    while (!out.isEmpty() && (out.startsWith(QLatin1Char('-')) ||
                              out.startsWith(QLatin1Char('.')))) {
        out.remove(0, 1);
    }
    return out;
}

} // namespace ghm::ui
