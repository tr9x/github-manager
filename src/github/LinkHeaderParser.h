#pragma once

// LinkHeaderParser - parses HTTP Link headers as defined in RFC 8288.
//
// GitHub's REST API uses Link headers to express pagination. The
// header looks like this:
//
//   Link: <https://api.github.com/user/repos?page=2>; rel="next",
//         <https://api.github.com/user/repos?page=14>; rel="last"
//
// We need at minimum the `next` URL to walk paginated endpoints. The
// other rels (`prev`, `first`, `last`) aren't used today but are
// cheap to extract and may be useful for future "jump to end" UX.
//
// Lives in its own header (no QtNetwork dependency; only QString +
// QUrl + QRegularExpression) so unit tests can exercise it without
// spinning up an HTTP client. The parser is robust to:
//   * quoted and unquoted rel values (rel="next" vs rel=next)
//   * extra whitespace around delimiters
//   * lowercase/uppercase rel comparisons
//   * extra parameters between rel and the next link
//   * missing or malformed entries (returns empty URL for that rel)
//
// What we DO NOT handle:
//   * URI-references with literal '>' inside (RFC allows percent-encoding
//     them anyway; in practice GitHub never produces such URLs)
//   * extension parameters with quoted strings containing commas
//
// Both gaps are theoretical for our use case — GitHub's Link headers
// are well-formed enough that the simple comma split below is safe.

#include <QString>
#include <QUrl>
#include <QHash>
#include <QRegularExpression>

namespace ghm::github {

// Map from rel name (lowercased) → URL. Empty map for an empty or
// unparseable header.
inline QHash<QString, QUrl> parseLinkHeader(const QString& header)
{
    QHash<QString, QUrl> out;
    if (header.trimmed().isEmpty()) return out;

    // RFC 8288 §3 says entries are comma-separated, with each entry
    // being `<URI-Reference>` followed by `;parameter` pairs. We split
    // on commas that aren't inside angle brackets to be safe — though
    // GitHub never embeds commas inside URLs in practice.
    //
    // The regex below captures one entry at a time. It demands:
    //   - opening '<', then any non-'>' chars (the URL), then closing '>'
    //   - optional parameters before the next entry boundary
    //
    // We could split on commas first and parse each entry, but a single
    // regex that walks the whole string is more compact and avoids
    // edge cases around commas inside parameter values.
    static const QRegularExpression entryRe(
        QStringLiteral(R"(<([^>]+)>([^,]*))"));

    auto it = entryRe.globalMatch(header);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString url    = m.captured(1).trimmed();
        const QString params = m.captured(2);
        if (url.isEmpty()) continue;

        // Look for `rel=...` inside the parameter section. Tolerate
        // both quoted and unquoted forms.
        //
        // Custom delimiter `RE(...)RE` for the raw string because the
        // regex itself contains the `)"` sequence (the close-paren of
        // a group followed by a literal quote in `"([^"]+)"`), which
        // would otherwise terminate a default `R"(...)"` raw string
        // prematurely.
        static const QRegularExpression relRe(
            QStringLiteral(R"RE(\brel\s*=\s*(?:"([^"]+)"|([^;,\s]+)))RE"),
            QRegularExpression::CaseInsensitiveOption);
        const auto relMatch = relRe.match(params);
        if (!relMatch.hasMatch()) continue;

        QString rel = relMatch.captured(1);
        if (rel.isEmpty()) rel = relMatch.captured(2);
        rel = rel.trimmed().toLower();
        if (rel.isEmpty()) continue;

        // RFC 8288 allows multiple space-separated rel values per
        // entry (e.g. `rel="next prev"`). GitHub doesn't use this,
        // but if someone passes such a header we register the URL
        // against each rel for completeness.
        for (const QString& token : rel.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            // First definition wins — RFC doesn't specify ordering for
            // duplicates, and GitHub never sends duplicates anyway.
            if (!out.contains(token)) out.insert(token, QUrl(url));
        }
    }
    return out;
}

// Convenience: return just the `next` URL, or an empty QUrl if the
// header doesn't define one. Wraps parseLinkHeader for callers that
// only care about pagination's forward direction (which is everyone
// today).
inline QUrl nextPageFromLink(const QString& header)
{
    return parseLinkHeader(header).value(QStringLiteral("next"));
}

// Overload accepting QByteArray for callers reading directly from
// QNetworkReply::rawHeader(), which returns QByteArray.
inline QUrl nextPageFromLink(const QByteArray& header)
{
    return nextPageFromLink(QString::fromUtf8(header));
}

} // namespace ghm::github
