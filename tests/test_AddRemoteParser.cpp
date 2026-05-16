// Unit tests for ui/AddRemoteParser.h.
//
// Targets the parser users rely on when pasting GitHub's "create new
// repo" instructions. Coverage focuses on the cases we've actually
// seen go wrong:
//   * shell prompt prefixes ("$ ") that copy-paste from GitHub docs
//   * SSH-style URLs (git@host:path)
//   * malformed inputs that should produce ok==false rather than
//     half-populated fields

#include <QtTest>
#include <QObject>

#include "ui/AddRemoteParser.h"

using ghm::ui::ParsedRemote;
using ghm::ui::parseRemotePaste;

class TestAddRemoteParser : public QObject {
    Q_OBJECT
private slots:

    // ----- Empty / whitespace-only input -------------------------------

    void emptyInput()
    {
        const auto p = parseRemotePaste(QString());
        QVERIFY(!p.ok);
        QVERIFY(p.url.isEmpty());
        QVERIFY(p.name.isEmpty());
    }

    void whitespaceOnly()
    {
        const auto p = parseRemotePaste(QStringLiteral("    \t\n  "));
        QVERIFY(!p.ok);
    }

    // ----- `git remote add <name> <url>` form --------------------------

    void fullCommand_https()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("git remote add origin https://github.com/me/x.git"));
        QVERIFY(p.ok);
        QCOMPARE(p.name, QStringLiteral("origin"));
        QCOMPARE(p.url,  QStringLiteral("https://github.com/me/x.git"));
    }

    void fullCommand_customRemoteName()
    {
        // Some users alias the upstream repo as "upstream" instead of
        // "origin". We must respect their choice, not silently rename.
        const auto p = parseRemotePaste(
            QStringLiteral("git remote add upstream https://github.com/them/y.git"));
        QVERIFY(p.ok);
        QCOMPARE(p.name, QStringLiteral("upstream"));
        QCOMPARE(p.url,  QStringLiteral("https://github.com/them/y.git"));
    }

    void fullCommand_withDollarPromptPrefix()
    {
        // GitHub's UI shows commands as "$ git remote add ..." in
        // documentation blocks; users copy the dollar too.
        const auto p = parseRemotePaste(
            QStringLiteral("$ git remote add origin https://github.com/me/x.git"));
        QVERIFY(p.ok);
        QCOMPARE(p.name, QStringLiteral("origin"));
    }

    void fullCommand_withGreaterThanPromptPrefix()
    {
        // PowerShell-style prompt — same idea, different glyph.
        const auto p = parseRemotePaste(
            QStringLiteral("> git remote add origin https://github.com/me/x.git"));
        QVERIFY(p.ok);
        QCOMPARE(p.name, QStringLiteral("origin"));
    }

    void fullCommand_caseInsensitive()
    {
        // Some IDEs auto-capitalise; the parser shouldn't care.
        const auto p = parseRemotePaste(
            QStringLiteral("Git Remote Add origin https://github.com/me/x.git"));
        QVERIFY(p.ok);
        QCOMPARE(p.name, QStringLiteral("origin"));
    }

    void fullCommand_withTrailingTokens()
    {
        // We ignore any tokens after <url>. Real CLI git wouldn't, but
        // for the dialog's "smart paste" it's better to extract what
        // we can than to require the user to delete trailing junk.
        const auto p = parseRemotePaste(
            QStringLiteral("git remote add origin https://github.com/me/x.git --tracking"));
        QVERIFY(p.ok);
        QCOMPARE(p.url, QStringLiteral("https://github.com/me/x.git"));
    }

    void fullCommand_withLeadingWhitespace()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("    git remote add origin https://github.com/me/x.git"));
        QVERIFY(p.ok);
    }

    // ----- Bare URL form -----------------------------------------------

    void bareUrl_https()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("https://github.com/me/x.git"));
        QVERIFY(p.ok);
        // Bare URL → conventional default name.
        QCOMPARE(p.name, QStringLiteral("origin"));
        QCOMPARE(p.url,  QStringLiteral("https://github.com/me/x.git"));
    }

    void bareUrl_http()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("http://example.com/repo.git"));
        QVERIFY(p.ok);
    }

    void bareUrl_ssh()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("git@github.com:me/x.git"));
        QVERIFY(p.ok);
        QCOMPARE(p.name, QStringLiteral("origin"));
        QCOMPARE(p.url,  QStringLiteral("git@github.com:me/x.git"));
    }

    void bareUrl_sshScheme()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("ssh://git@host.example/path/to/repo.git"));
        QVERIFY(p.ok);
    }

    void bareUrl_gitProtocol()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("git://github.com/me/x.git"));
        QVERIFY(p.ok);
    }

    // ----- Negative cases ----------------------------------------------

    void unknownScheme_isRejected()
    {
        // We don't handle file:// or arbitrary schemes — push will fail
        // anyway, so reject early.
        const auto p = parseRemotePaste(
            QStringLiteral("file:///home/me/repo"));
        QVERIFY(!p.ok);
    }

    void plainText_isRejected()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("This is just a description, not a URL."));
        QVERIFY(!p.ok);
    }

    void incompleteCommand_isRejected()
    {
        // Missing URL after the name.
        const auto p = parseRemotePaste(
            QStringLiteral("git remote add origin"));
        QVERIFY(!p.ok);
    }

    void wrongCommand_isRejected()
    {
        const auto p = parseRemotePaste(
            QStringLiteral("git pull origin master"));
        QVERIFY(!p.ok);
    }
};

QTEST_APPLESS_MAIN(TestAddRemoteParser)
#include "test_AddRemoteParser.moc"
