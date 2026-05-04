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
    QString    defaultBranch;   // "main", "master", etc.
    bool       isPrivate{false};
    bool       isFork{false};
    QDateTime  updatedAt;
    qint64     sizeKb{0};

    // Populated client-side once the user clones/opens the repo locally.
    QString    localPath;

    bool isValid() const { return !fullName.isEmpty() && !cloneUrl.isEmpty(); }
};

} // namespace ghm::github

Q_DECLARE_METATYPE(ghm::github::Repository)
