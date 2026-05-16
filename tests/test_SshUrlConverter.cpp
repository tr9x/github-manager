// Unit tests for ghm::github::httpsToSsh().
//
// Pure-function tests — no network, no fixtures. Each case is a
// (input, expected) pair, asserting the URL transformation matches
// what the SSH transport in libgit2 will accept.

#include <QtTest>
#include <QObject>

#include "github/SshUrlConverter.h"

using ghm::github::httpsToSsh;

class TestSshUrlConverter : public QObject {
    Q_OBJECT
private slots:

    void empty_passesThrough()
    {
        QCOMPARE(httpsToSsh(QString()), QString());
    }

    void already_ssh_passesThrough()
    {
        QCOMPARE(httpsToSsh(QStringLiteral("git@github.com:owner/repo.git")),
                 QStringLiteral("git@github.com:owner/repo.git"));
    }

    void already_ssh_scheme_passesThrough()
    {
        // ssh:// form is less common but valid.
        QCOMPARE(httpsToSsh(QStringLiteral("ssh://git@github.com/owner/repo.git")),
                 QStringLiteral("ssh://git@github.com/owner/repo.git"));
    }

    void file_url_passesThrough()
    {
        // file:// is used by our integration tests for local "remotes".
        // Must NOT be transformed — there's no SSH for file paths.
        QCOMPARE(httpsToSsh(QStringLiteral("file:///tmp/some/repo")),
                 QStringLiteral("file:///tmp/some/repo"));
    }

    void canonical_github_url()
    {
        QCOMPARE(httpsToSsh(QStringLiteral("https://github.com/octocat/Hello-World.git")),
                 QStringLiteral("git@github.com:octocat/Hello-World.git"));
    }

    void missing_dotgit_is_added()
    {
        // GitHub clone URLs from the REST API always have .git, but
        // a user-typed URL might not. We normalise.
        QCOMPARE(httpsToSsh(QStringLiteral("https://github.com/octocat/Hello-World")),
                 QStringLiteral("git@github.com:octocat/Hello-World.git"));
    }

    void user_prefix_stripped()
    {
        // https://USER@host/... — userinfo is stripped, SSH always
        // uses "git@" as the user.
        QCOMPARE(httpsToSsh(QStringLiteral("https://alice@github.com/octocat/Hello-World.git")),
                 QStringLiteral("git@github.com:octocat/Hello-World.git"));
    }

    void non_github_host_works()
    {
        // The function isn't GitHub-specific; any HTTPS git URL
        // becomes the SCP-style SSH equivalent.
        QCOMPARE(httpsToSsh(QStringLiteral("https://gitlab.com/group/project.git")),
                 QStringLiteral("git@gitlab.com:group/project.git"));
    }

    void self_hosted_with_port()
    {
        // Self-hosted GitLab on a custom port. SCP-style URL doesn't
        // carry the port, but we also don't pretend to support every
        // edge case — the URL still roundtrips for the host:path part
        // and libgit2 will use the default SSH port (22). Custom-port
        // SSH config goes in ~/.ssh/config.
        const auto out = httpsToSsh(QStringLiteral("https://git.example.com:8443/team/repo.git"));
        QCOMPARE(out, QStringLiteral("git@git.example.com:team/repo.git"));
    }

    void http_scheme_also_works()
    {
        // Plain HTTP (not HTTPS) is rare but valid for internal mirrors.
        QCOMPARE(httpsToSsh(QStringLiteral("http://internal.local/x/y.git")),
                 QStringLiteral("git@internal.local:x/y.git"));
    }

    void invalid_url_passesThrough()
    {
        // QUrl::isValid() rejects clearly-broken inputs — we pass
        // them through rather than fabricating a bogus SSH URL.
        QCOMPARE(httpsToSsh(QStringLiteral("not a url")),
                 QStringLiteral("not a url"));
    }

    void empty_path_passesThrough()
    {
        // https://github.com/  with no repo path → return as-is
        // rather than producing "git@github.com:.git".
        QCOMPARE(httpsToSsh(QStringLiteral("https://github.com/")),
                 QStringLiteral("https://github.com/"));
    }

    void nested_path_preserved()
    {
        // Some platforms (GitLab) allow nested groups: /a/b/c.git.
        // The middle slashes stay, only the leading slash is removed.
        QCOMPARE(httpsToSsh(QStringLiteral("https://gitlab.com/a/b/c.git")),
                 QStringLiteral("git@gitlab.com:a/b/c.git"));
    }
};

QTEST_APPLESS_MAIN(TestSshUrlConverter)
#include "test_SshUrlConverter.moc"
