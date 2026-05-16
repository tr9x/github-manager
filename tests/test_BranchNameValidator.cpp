// Unit tests for ui/BranchNameValidator.h.
//
// Mirrors the rules in `git check-ref-format` for the cases users hit
// in practice. Anything we miss libgit2 will reject anyway — these
// tests just make sure the dialog rejects-early so the user gets
// immediate feedback rather than a cryptic libgit2 error after submit.

#include <QtTest>
#include <QObject>

#include "ui/BranchNameValidator.h"

using ghm::ui::isValidBranchName;

class TestBranchNameValidator : public QObject {
    Q_OBJECT
private slots:

    // ----- Valid names -------------------------------------------------

    void simpleNames()
    {
        QVERIFY(isValidBranchName(QStringLiteral("master")));
        QVERIFY(isValidBranchName(QStringLiteral("main")));
        QVERIFY(isValidBranchName(QStringLiteral("develop")));
    }

    void slashSeparatedNames()
    {
        // The convention everyone uses for organising branches.
        QVERIFY(isValidBranchName(QStringLiteral("feature/login-form")));
        QVERIFY(isValidBranchName(QStringLiteral("user/alice/wip")));
        QVERIFY(isValidBranchName(QStringLiteral("release/2024.10")));
    }

    void numericAndMixedNames()
    {
        QVERIFY(isValidBranchName(QStringLiteral("v1.2.3")));
        QVERIFY(isValidBranchName(QStringLiteral("issue-1234")));
        QVERIFY(isValidBranchName(QStringLiteral("123")));
    }

    void underscoreAllowed()
    {
        QVERIFY(isValidBranchName(QStringLiteral("snake_case_branch")));
    }

    // ----- Empty / whitespace ------------------------------------------

    void empty()
    {
        QString reason;
        QVERIFY(!isValidBranchName(QString(), &reason));
        QVERIFY(!reason.isEmpty());
    }

    void containsSpace()
    {
        QString reason;
        QVERIFY(!isValidBranchName(QStringLiteral("feature branch"), &reason));
        QVERIFY(reason.contains(QStringLiteral("whitespace"), Qt::CaseInsensitive));
    }

    void containsTab()
    {
        QVERIFY(!isValidBranchName(QStringLiteral("feature\tbranch")));
    }

    // ----- Boundary characters -----------------------------------------

    void startsWithDash()
    {
        // Would otherwise be parsed as a CLI flag by raw `git`.
        QString reason;
        QVERIFY(!isValidBranchName(QStringLiteral("-bad"), &reason));
        QVERIFY(reason.contains(QLatin1Char('-')));
    }

    void startsWithDot()
    {
        QVERIFY(!isValidBranchName(QStringLiteral(".hidden")));
    }

    void endsWithSlash()
    {
        QVERIFY(!isValidBranchName(QStringLiteral("foo/")));
    }

    void endsWithDot()
    {
        // Trailing dot creates ambiguity with refs/heads/foo.lock files
        // git uses internally during ref updates.
        QVERIFY(!isValidBranchName(QStringLiteral("foo.")));
    }

    // ----- Forbidden character classes ---------------------------------

    void forbiddenSpecials()
    {
        // Characters git uses in ref syntax (~^:?*[\\) all reserved.
        QVERIFY(!isValidBranchName(QStringLiteral("foo~bar")));
        QVERIFY(!isValidBranchName(QStringLiteral("foo^bar")));
        QVERIFY(!isValidBranchName(QStringLiteral("foo:bar")));
        QVERIFY(!isValidBranchName(QStringLiteral("foo?bar")));
        QVERIFY(!isValidBranchName(QStringLiteral("foo*bar")));
        QVERIFY(!isValidBranchName(QStringLiteral("foo[bar")));
        QVERIFY(!isValidBranchName(QStringLiteral("foo\\bar")));
    }

    void controlCharacters()
    {
        // \x01 (start of header) — sneaks in through bad copy-paste.
        QString s = QStringLiteral("foo");
        s.append(QChar(0x01));
        s.append(QStringLiteral("bar"));
        QVERIFY(!isValidBranchName(s));
    }

    void deleteCharacter()
    {
        QString s = QStringLiteral("foo");
        s.append(QChar(0x7F));
        QVERIFY(!isValidBranchName(s));
    }

    // ----- Forbidden substrings ----------------------------------------

    void doubleDot()
    {
        // Git uses ".." for revision ranges (HEAD..main) — can't be
        // part of a branch name without ambiguity.
        QVERIFY(!isValidBranchName(QStringLiteral("foo..bar")));
    }

    void atBrace()
    {
        // Git uses "@{N}" syntax for reflog references.
        QVERIFY(!isValidBranchName(QStringLiteral("foo@{0}")));
    }

    // ----- Reserved names ----------------------------------------------

    void reservedHEAD()
    {
        QVERIFY(!isValidBranchName(QStringLiteral("HEAD")));
    }

    void reservedAt()
    {
        QVERIFY(!isValidBranchName(QStringLiteral("@")));
    }

    // ----- Reason messages on failure ----------------------------------

    void failureProvidesReason()
    {
        // Any failure should populate `whyNot` if the caller asked for
        // one. We don't test exact strings (translations may shift)
        // but verify the field is non-empty.
        QString reason;
        QVERIFY(!isValidBranchName(QStringLiteral(""), &reason));
        QVERIFY(!reason.isEmpty());

        reason.clear();
        QVERIFY(!isValidBranchName(QStringLiteral(".x"), &reason));
        QVERIFY(!reason.isEmpty());

        reason.clear();
        QVERIFY(!isValidBranchName(QStringLiteral("foo bar"), &reason));
        QVERIFY(!reason.isEmpty());
    }

    void successDoesNotTouchReason()
    {
        // On success, whyNot must not be modified — a stale reason from
        // a prior validation could leak into the UI.
        QString reason = QStringLiteral("untouched");
        QVERIFY(isValidBranchName(QStringLiteral("master"), &reason));
        QCOMPARE(reason, QStringLiteral("untouched"));
    }
};

QTEST_APPLESS_MAIN(TestBranchNameValidator)
#include "test_BranchNameValidator.moc"
