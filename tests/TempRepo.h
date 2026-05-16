#pragma once

// TempRepo — RAII helper for tests that need a real on-disk Git
// repository.
//
// Constructor creates a QTemporaryDir and runs GitHandler::init on it,
// so by the time the constructor returns there's an empty Git repo on
// disk ready for operations. Destructor lets QTemporaryDir clean up
// (auto-removeable=true), so each test starts and ends clean.
//
// Helper methods cover the operations most tests need a few of:
//   * writeFile() — drop a file into the work tree
//   * stage()     — stage one or more paths
//   * commit()    — stage everything + commit, returns the SHA
//
// What we DON'T provide here:
//   * any network ops (push/pull/fetch against real remotes) — tests
//     that need a "remote" can create a second TempRepo and use its
//     local filesystem path as the URL; libgit2 happily accepts
//     /tmp/xyz as a remote URL.
//   * cleanup of detached state — if a test puts the repo into a bad
//     state (mid-merge, mid-rebase) without resolving, the temp dir
//     is still nuked on destruction. That's fine, the next test gets
//     a fresh one.
//
// Tests own their handler instance and pass it in — TempRepo doesn't
// hold a reference. This keeps each test's setup explicit and avoids
// any singleton-flavoured shared state.

#include <QTemporaryDir>
#include <QString>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "git/GitHandler.h"

namespace ghm::tests {

class TempRepo {
public:
    // Create a temp directory and `git init` it.
    // initialBranch defaults to "main" because that's what modern Git
    // uses; previously was "master" but main is the default now and
    // matches what users on recent git versions see.
    explicit TempRepo(ghm::git::GitHandler& handler,
                      const QString& initialBranch = QStringLiteral("main"))
        : handler_(handler)
    {
        tmp_.setAutoRemove(true);
        if (!tmp_.isValid()) {
            failure_ = QStringLiteral("Failed to create QTemporaryDir");
            return;
        }
        path_ = tmp_.path();
        auto r = handler_.init(path_, initialBranch);
        if (!r.ok) {
            failure_ = QStringLiteral("init failed: %1").arg(r.error);
        }
    }

    // True only if construction got us to a usable initialised repo.
    // Tests should QVERIFY(repo.isReady()) before using anything.
    bool    isReady() const { return failure_.isEmpty(); }
    QString failure() const { return failure_; }
    QString path()    const { return path_; }

    // Write `content` into `relativePath` inside the work tree.
    // Creates parent directories as needed. Returns true on success.
    // Does NOT stage — call stage() or commit() after.
    bool writeFile(const QString& relativePath, const QString& content)
    {
        const QString abs = QDir(path_).absoluteFilePath(relativePath);
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QFile f(abs);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(content.toUtf8());
        return true;
    }

    // Stage everything in the work tree. Convenience for tests that
    // don't care about which specific files are staged.
    bool stageAll()
    {
        return handler_.stageAll(path_).ok;
    }

    // Stage specific paths. Same semantics as `git add <paths>`.
    bool stage(const QStringList& paths)
    {
        return handler_.stagePaths(path_, paths).ok;
    }

    // Stage everything and commit with the given message under a
    // test identity. Returns the new commit's SHA on success, empty
    // QString on failure. Tests that need the SHA for later
    // operations can capture it; others can discard.
    QString commit(const QString& message)
    {
        if (!stageAll()) return {};
        QString sha;
        auto r = handler_.commit(path_, message,
                                 QStringLiteral("Test User"),
                                 QStringLiteral("test@example.com"),
                                 &sha);
        return r.ok ? sha : QString();
    }

    // Write `content` to `relativePath`, stage it, and commit.
    // One-liner for the very common "make a commit that changes a file"
    // setup pattern.
    QString commitFile(const QString& relativePath,
                       const QString& content,
                       const QString& message)
    {
        if (!writeFile(relativePath, content)) return {};
        return commit(message);
    }

private:
    ghm::git::GitHandler& handler_;
    QTemporaryDir         tmp_;
    QString               path_;
    QString               failure_;
};

} // namespace ghm::tests
