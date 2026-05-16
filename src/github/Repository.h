#pragma once

// Repository - plain data model for a GitHub repo.
//
// Held by value, copied freely. UI-side widgets and the GitHub client
// both deal in this type so we don't pass ad-hoc QVariantMaps around.

#include <QString>
#include <QDateTime>
#include <QMetaType>

namespace ghm::github {

struct Repository {
    QString    name;            // e.g. "github-manager"
    QString    fullName;        // e.g. "octocat/github-manager"
    QString    description;
    QString    cloneUrl;        // https URL we'll feed to libgit2
    QString    sshUrl;
    QString    htmlUrl;         // browser-friendly URL (github.com/...)
    QString    defaultBranch;   // "main", "master", etc.
    QString    primaryLanguage; // GitHub's `language` field, e.g. "C++"
    bool       isPrivate{false};
    bool       isFork{false};
    bool       isArchived{false};
    QDateTime  updatedAt;
    QDateTime  pushedAt;        // last push, often more recent than updated_at
    qint64     sizeKb{0};
    int        stargazers{0};
    int        forks{0};
    int        openIssues{0};
    int        watchers{0};
    QStringList topics;         // tags / topics shown on github.com

    // Populated client-side once the user clones/opens the repo locally.
    QString    localPath;

    bool isValid() const { return !fullName.isEmpty() && !cloneUrl.isEmpty(); }
};

// Single entry from /repos/{owner}/{repo}/contents/{path}. Used by
// the remote-files tab in RepositoryDetailWidget to list a directory
// without cloning. We carry the minimum the UI needs:
//
//   * name        — file/folder name (no path prefix)
//   * type        — "file" | "dir" | "symlink" | "submodule" (we
//                   really only render the first two; others get
//                   shown as themselves)
//   * size        — byte count (0 for dirs)
//   * htmlUrl     — github.com URL to open in browser
//   * downloadUrl — raw content URL (files only); empty for dirs
struct ContentEntry {
    QString name;
    QString type;
    qint64  size{0};
    QString htmlUrl;
    QString downloadUrl;
};

} // namespace ghm::github

Q_DECLARE_METATYPE(ghm::github::Repository)
Q_DECLARE_METATYPE(ghm::github::ContentEntry)
