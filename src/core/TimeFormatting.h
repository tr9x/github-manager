#pragma once

// TimeFormatting - small helpers for human-friendly time strings.
//
// Centralised here so the same "5 min ago" / "2 d ago" rendering is
// used everywhere (sidebar, history, publish dialog, ...). Inline
// implementation keeps it header-only — these are tiny, no need for
// a separate compilation unit.

#include <QDateTime>
#include <QString>
#include <QLocale>
#include <QObject>

namespace ghm::core {

// Renders a past timestamp as "X seconds/minutes/hours/days ago" or
// the absolute date for older entries. `when` is expected in local
// time; UTC inputs work but the day boundary may be off-by-one near
// midnight.
//
// Strings go through QObject::tr() so they participate in the app's
// translation system.
inline QString relativeTime(const QDateTime& when)
{
    if (!when.isValid()) return QStringLiteral("—");
    const qint64 secs = when.secsTo(QDateTime::currentDateTimeUtc().toLocalTime());

    // Future timestamps (e.g. clock skew, just-pushed commit on
    // a remote with a faster clock) — treat as "just now" rather
    // than rendering negative numbers.
    if (secs < 60)                  return QObject::tr("just now");
    if (secs < 60 * 60)             return QObject::tr("%1 min ago").arg(secs / 60);
    if (secs < 60 * 60 * 24)        return QObject::tr("%1 h ago").arg(secs / 3600);
    if (secs < 60 * 60 * 24 * 30)   return QObject::tr("%1 d ago").arg(secs / 86400);

    return QLocale().toString(when.toLocalTime().date(), QLocale::ShortFormat);
}

} // namespace ghm::core
