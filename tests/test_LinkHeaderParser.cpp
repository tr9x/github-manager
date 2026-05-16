// Unit tests for github/LinkHeaderParser.h.
//
// Targets the parser used to walk GitHub's paginated REST endpoints.
// Coverage focuses on:
//   * the actual header shapes GitHub produces (canonical case)
//   * RFC 8288 tolerance for variations we don't expect from GitHub
//     but should still handle: unquoted rel, extra whitespace, mixed
//     case, multiple parameters per entry
//   * malformed inputs that should produce an empty result rather
//     than crashing or returning garbage URLs

#include <QtTest>
#include <QObject>

#include "github/LinkHeaderParser.h"

using ghm::github::parseLinkHeader;
using ghm::github::nextPageFromLink;

class TestLinkHeaderParser : public QObject {
    Q_OBJECT
private slots:

    // ----- Empty / malformed input ------------------------------------

    void emptyInput()
    {
        QVERIFY(parseLinkHeader(QString()).isEmpty());
        QVERIFY(parseLinkHeader(QStringLiteral("")).isEmpty());
        QVERIFY(parseLinkHeader(QStringLiteral("    ")).isEmpty());
        QVERIFY(!nextPageFromLink(QString()).isValid());
    }

    void garbageInput_returnsEmpty()
    {
        // Not even close to a Link header — must produce empty result,
        // not throw or hand back a half-parsed URL.
        QVERIFY(parseLinkHeader(QStringLiteral("hello world")).isEmpty());
        QVERIFY(parseLinkHeader(QStringLiteral("<no-rel>")).isEmpty());
        QVERIFY(parseLinkHeader(QStringLiteral("rel=\"next\"")).isEmpty());
    }

    void noClosingBracket_returnsEmpty()
    {
        QVERIFY(parseLinkHeader(
            QStringLiteral("<https://example.com; rel=\"next\"")).isEmpty());
    }

    // ----- Canonical GitHub format ------------------------------------

    void singleNextLink()
    {
        // Smallest real-world case: GitHub returns just `next` when
        // we're on a page and not yet at the end.
        const auto m = parseLinkHeader(
            QStringLiteral("<https://api.github.com/user/repos?page=2>; rel=\"next\""));
        QCOMPARE(m.size(), 1);
        QCOMPARE(m.value(QStringLiteral("next")).toString(),
                 QStringLiteral("https://api.github.com/user/repos?page=2"));
    }

    void typicalPaginatedResponse()
    {
        // What GitHub actually sends for a mid-paginated response:
        // both `next` and `last`.
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://api.github.com/user/repos?page=3&per_page=30>; rel=\"next\", "
            "<https://api.github.com/user/repos?page=14&per_page=30>; rel=\"last\""));
        QCOMPARE(m.size(), 2);
        QCOMPARE(m.value(QStringLiteral("next")).toString(),
                 QStringLiteral("https://api.github.com/user/repos?page=3&per_page=30"));
        QCOMPARE(m.value(QStringLiteral("last")).toString(),
                 QStringLiteral("https://api.github.com/user/repos?page=14&per_page=30"));
    }

    void allFourRels()
    {
        // What GitHub sends for a middle page: prev/next/first/last
        // all present.
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://api.github.com/user/repos?page=4>; rel=\"prev\", "
            "<https://api.github.com/user/repos?page=6>; rel=\"next\", "
            "<https://api.github.com/user/repos?page=1>; rel=\"first\", "
            "<https://api.github.com/user/repos?page=14>; rel=\"last\""));
        QCOMPARE(m.size(), 4);
        QVERIFY(m.contains(QStringLiteral("prev")));
        QVERIFY(m.contains(QStringLiteral("next")));
        QVERIFY(m.contains(QStringLiteral("first")));
        QVERIFY(m.contains(QStringLiteral("last")));
    }

    void lastPage_hasNoNext()
    {
        // On the final page GitHub sends only `prev` and `first`. A
        // caller looking for `next` must get an invalid URL back.
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://api.github.com/user/repos?page=13>; rel=\"prev\", "
            "<https://api.github.com/user/repos?page=1>; rel=\"first\""));
        QCOMPARE(m.size(), 2);
        QVERIFY(!m.contains(QStringLiteral("next")));
        QVERIFY(!nextPageFromLink(QStringLiteral(
            "<https://api.github.com/user/repos?page=13>; rel=\"prev\", "
            "<https://api.github.com/user/repos?page=1>; rel=\"first\"")).isValid());
    }

    // ----- RFC 8288 tolerance (things we accept beyond GitHub's strict format) ---

    void unquotedRel()
    {
        // RFC 8288 allows `rel=next` without quotes; we should accept it
        // even if GitHub never sends it in this form.
        const auto m = parseLinkHeader(
            QStringLiteral("<https://example.com/page2>; rel=next"));
        QCOMPARE(m.value(QStringLiteral("next")).toString(),
                 QStringLiteral("https://example.com/page2"));
    }

    void extraWhitespaceAroundDelimiters()
    {
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://example.com/p2>  ;   rel = \"next\"  ,  "
            "<https://example.com/p9>;rel=\"last\""));
        QCOMPARE(m.value(QStringLiteral("next")).toString(),
                 QStringLiteral("https://example.com/p2"));
        QCOMPARE(m.value(QStringLiteral("last")).toString(),
                 QStringLiteral("https://example.com/p9"));
    }

    void caseInsensitiveRelKeyword()
    {
        // The `rel` keyword itself is case-insensitive per RFC.
        const auto m = parseLinkHeader(
            QStringLiteral("<https://example.com/p2>; REL=\"next\""));
        QCOMPARE(m.value(QStringLiteral("next")).toString(),
                 QStringLiteral("https://example.com/p2"));
    }

    void relValueIsLowercased()
    {
        // We normalise rel values to lowercase so callers can look up
        // by `next` regardless of what the server sent.
        const auto m = parseLinkHeader(
            QStringLiteral("<https://example.com/p2>; rel=\"NEXT\""));
        QVERIFY(m.contains(QStringLiteral("next")));
        QVERIFY(!m.contains(QStringLiteral("NEXT")));
    }

    void extraParametersBeforeRel()
    {
        // GitHub doesn't add other parameters, but RFC 8288 permits
        // them. The parser should ignore them and still find the rel.
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://example.com/p2>; type=\"text/html\"; rel=\"next\""));
        QCOMPARE(m.value(QStringLiteral("next")).toString(),
                 QStringLiteral("https://example.com/p2"));
    }

    void extraParametersAfterRel()
    {
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://example.com/p2>; rel=\"next\"; title=\"Page 2\""));
        QCOMPARE(m.value(QStringLiteral("next")).toString(),
                 QStringLiteral("https://example.com/p2"));
    }

    // ----- Convenience overload --------------------------------------

    void nextPageFromLink_byteArrayOverload()
    {
        // The QNetworkReply::rawHeader() side returns QByteArray; the
        // overload should yield identical results.
        const QByteArray hdr =
            "<https://api.github.com/user/repos?page=2>; rel=\"next\"";
        QCOMPARE(nextPageFromLink(hdr).toString(),
                 QStringLiteral("https://api.github.com/user/repos?page=2"));
    }

    void nextPageFromLink_skipsNonNextEntries()
    {
        // The convenience overload must walk past `prev`/`last` to
        // find `next`, regardless of order in the header.
        const QByteArray hdr =
            "<https://example.com/p13>; rel=\"prev\", "
            "<https://example.com/p2>; rel=\"next\", "
            "<https://example.com/p1>; rel=\"first\"";
        QCOMPARE(nextPageFromLink(hdr).toString(),
                 QStringLiteral("https://example.com/p2"));
    }

    // ----- Edge cases ------------------------------------------------

    void duplicateRel_firstWins()
    {
        // Two entries claiming to be `next`. We don't expect this from
        // any real server, but if it happens the parser must be
        // deterministic — first one wins.
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://example.com/first>; rel=\"next\", "
            "<https://example.com/second>; rel=\"next\""));
        QCOMPARE(m.value(QStringLiteral("next")).toString(),
                 QStringLiteral("https://example.com/first"));
    }

    void multipleRelTokensInOneEntry()
    {
        // RFC 8288 §3 allows space-separated rel values:
        //   rel="next prev"
        // We register the URL under each token. GitHub never does
        // this but we don't want a malformed-looking parse if some
        // proxy ever rewrites things.
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://example.com/x>; rel=\"alternate canonical\""));
        QCOMPARE(m.value(QStringLiteral("alternate")).toString(),
                 QStringLiteral("https://example.com/x"));
        QCOMPARE(m.value(QStringLiteral("canonical")).toString(),
                 QStringLiteral("https://example.com/x"));
    }

    void entryWithoutRel_isIgnored()
    {
        // RFC 8288 doesn't strictly require `rel` on every entry, but
        // for our use case (rel-keyed lookups) an entry without rel
        // is uninteresting. Skip silently rather than failing the
        // whole parse.
        const auto m = parseLinkHeader(QStringLiteral(
            "<https://example.com/no-rel>; type=\"text/html\", "
            "<https://example.com/p2>; rel=\"next\""));
        QCOMPARE(m.size(), 1);
        QVERIFY(m.contains(QStringLiteral("next")));
    }
};

QTEST_APPLESS_MAIN(TestLinkHeaderParser)
#include "test_LinkHeaderParser.moc"
