#pragma once

// Convert HTTPS GitHub URLs to their SCP-style SSH equivalents.
//
// GitHub gives us HTTPS URLs in the REST API:
//   https://github.com/octocat/Hello-World.git
//
// We sometimes need the SSH form:
//   git@github.com:octocat/Hello-World.git
//
// This is a pure function — no network, no IO — so we keep it inline
// and write unit tests for it. Used by CloneDialog (when the user
// prefers SSH) and as a sanity check before kicking off SSH clones.
//
// We support the common shapes:
//   https://github.com/X/Y(.git)?    → git@github.com:X/Y.git
//   https://X@github.com/Y/Z(.git)?  → git@github.com:Y/Z.git   (user prefix stripped)
//   https://gitlab.com/X/Y(.git)?    → git@gitlab.com:X/Y.git    (other hosts work too)
//
// Inputs we DON'T transform (returned as-is):
//   * already-SSH URLs (git@host:..., ssh://...)
//   * file:// URLs (local clones for testing)
//   * malformed / non-https URLs

#include <QString>
#include <QUrl>

namespace ghm::github {

inline QString httpsToSsh(const QString& url)
{
    if (url.isEmpty()) return url;

    // Already SSH? Pass through. We don't try to "improve" existing
    // SSH URLs because there are too many flavours (port, custom user).
    if (url.startsWith(QLatin1String("git@")) ||
        url.startsWith(QLatin1String("ssh://"))) {
        return url;
    }

    // Only transform http/https URLs. file:// and other schemes are
    // pass-through — they're either tests or genuinely-local clones
    // where SSH makes no sense.
    QUrl q(url);
    if (!q.isValid()) return url;
    const QString scheme = q.scheme().toLower();
    if (scheme != QLatin1String("https") && scheme != QLatin1String("http")) {
        return url;
    }

    const QString host = q.host();
    if (host.isEmpty()) return url;

    // QUrl::path() includes leading slash. We strip it; the SCP-style
    // SSH URL uses ":" as the separator and no leading slash.
    QString path = q.path();
    while (path.startsWith(QLatin1Char('/'))) path.remove(0, 1);
    if (path.isEmpty()) return url;

    // GitHub clone URLs end in ".git" but a user-supplied URL might
    // not. Either way, normalise: SSH form should have .git suffix
    // (libgit2 doesn't require it but it's what `git remote -v`
    // shows for cloned repos, so for consistency we add it).
    if (!path.endsWith(QLatin1String(".git"))) {
        path.append(QLatin1String(".git"));
    }

    return QStringLiteral("git@%1:%2").arg(host, path);
}

} // namespace ghm::github
