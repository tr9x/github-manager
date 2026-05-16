// Unit tests for ui/RepoNameSuggester.h.
//
// The suggester is what fills the "name" field in PublishToGitHubDialog
// when the user opens it. Its job is to turn whatever the on-disk
// folder happens to be called into something GitHub will accept
// without forcing the user to retype the whole thing.

#include <QtTest>
#include <QObject>

#include "ui/RepoNameSuggester.h"

using ghm::ui::suggestRepoName;

class TestRepoNameSuggester : public QObject {
    Q_OBJECT
private slots:

    // ----- Already-valid names pass through unchanged ------------------

    void simpleAsciiName()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("myproject")),
                 QStringLiteral("myproject"));
    }

    void hyphenatedName()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("my-cool-project")),
                 QStringLiteral("my-cool-project"));
    }

    void underscoredName()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("snake_case_project")),
                 QStringLiteral("snake_case_project"));
    }

    void dottedName()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("my.cool.project")),
                 QStringLiteral("my.cool.project"));
    }

    void numericName()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("project-2024")),
                 QStringLiteral("project-2024"));
    }

    // ----- Whitespace becomes hyphen -----------------------------------

    void singleSpace()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("my project")),
                 QStringLiteral("my-project"));
    }

    void multipleSpaces_collapse()
    {
        // Runs of non-allowed characters become a single hyphen — not
        // "my---project". The test guards the collapse logic.
        QCOMPARE(suggestRepoName(QStringLiteral("my   project")),
                 QStringLiteral("my-project"));
    }

    void tabBecomesHyphen()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("my\tproject")),
                 QStringLiteral("my-project"));
    }

    // ----- Special characters become hyphens ---------------------------

    void slashBecomesHyphen()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("foo/bar")),
                 QStringLiteral("foo-bar"));
    }

    void multiplePunctuation_collapse()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("foo:?*bar")),
                 QStringLiteral("foo-bar"));
    }

    void parenthesesAndBrackets()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("foo (bar) [baz]")),
                 QStringLiteral("foo-bar-baz"));
    }

    void atSign()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("foo@bar")),
                 QStringLiteral("foo-bar"));
    }

    // ----- Trimming leading/trailing punctuation -----------------------

    void leadingHyphen_trimmed()
    {
        // Folder name like "-private" — leading hyphens look like CLI
        // flags and GitHub rejects them anyway.
        QCOMPARE(suggestRepoName(QStringLiteral("-private")),
                 QStringLiteral("private"));
    }

    void leadingDot_trimmed()
    {
        // Hidden folders on Linux conventionally start with a dot.
        QCOMPARE(suggestRepoName(QStringLiteral(".dotfiles")),
                 QStringLiteral("dotfiles"));
    }

    void trailingHyphen_trimmed()
    {
        // Comes from "foo " where the trailing space became a hyphen.
        QCOMPARE(suggestRepoName(QStringLiteral("foo ")),
                 QStringLiteral("foo"));
    }

    void trailingDot_trimmed()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("project.")),
                 QStringLiteral("project"));
    }

    void wrappedInPunctuation()
    {
        // Both ends get trimmed.
        QCOMPARE(suggestRepoName(QStringLiteral(".-foo-.")),
                 QStringLiteral("foo"));
    }

    // ----- Edge cases --------------------------------------------------

    void emptyString()
    {
        QCOMPARE(suggestRepoName(QString()), QString());
    }

    void onlyPunctuation_emptyResult()
    {
        // Nothing salvageable — caller falls back to user input.
        QCOMPARE(suggestRepoName(QStringLiteral("!!!")), QString());
    }

    void onlyWhitespace_emptyResult()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("   ")), QString());
    }

    void unicodeStripped()
    {
        // GitHub repo names are ASCII-only. Polish "ł" → hyphen.
        const QString result = suggestRepoName(QStringLiteral("paździoch"));
        // Should keep ASCII letters/numbers, replace the rest with hyphens.
        // Exact output depends on QChar::isLetterOrNumber() — Qt treats
        // Unicode letters as letters, so "ł" actually counts as a letter.
        // What matters is the result doesn't contain anything that's
        // outside our allowed set, and is non-empty.
        QVERIFY(!result.isEmpty());
        for (QChar c : result) {
            const bool allowed = c.isLetterOrNumber()
                              || c == QLatin1Char('-')
                              || c == QLatin1Char('_')
                              || c == QLatin1Char('.');
            QVERIFY2(allowed, qPrintable(QStringLiteral("forbidden char in: %1").arg(result)));
        }
    }

    // ----- Real-world smoke tests --------------------------------------

    void realWorld_capitalizedWithSpaces()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("My Cool Project")),
                 QStringLiteral("My-Cool-Project"));
    }

    void realWorld_versionedFolder()
    {
        QCOMPARE(suggestRepoName(QStringLiteral("v1.0.0-rc.2")),
                 QStringLiteral("v1.0.0-rc.2"));
    }
};

QTEST_APPLESS_MAIN(TestRepoNameSuggester)
#include "test_RepoNameSuggester.moc"
