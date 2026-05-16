#pragma once

// Translator — thin wrapper around QTranslator that loads compiled
// .qm files from Qt resources.
//
// Pipeline: translations/github-manager_<lang>.ts → lrelease → .qm,
// the .qm files are baked into the binary as Qt resources at build
// time (see top-level CMakeLists.txt). At runtime we ask
// QTranslator::load to find the right .qm via Qt's standard
// language-matching rules.
//
// Source language is English (matches the bare strings in tr() calls).
// When the user selects "en", we don't install any translator — Qt
// returns the source text directly. When the user selects "pl" (or
// future locales), we load the matching .qm and let QTranslator do
// its job.
//
// Historical note: earlier versions of this class held an in-memory
// EN→PL phrasebook of 300+ entries baked into Translator.cpp. That
// kept builds simple but locked us out of `lupdate`, Qt Linguist,
// and any crowdsourcing workflow (Weblate, Crowdin). 0.9.0 migrated
// to the standard .ts pipeline.

#include <QTranslator>
#include <QString>

namespace ghm::core {

class Translator : public QTranslator {
    Q_OBJECT
public:
    explicit Translator(QObject* parent = nullptr);

    // Load the .qm matching this language code. "en" is a no-op
    // (clears any previously-loaded translation, returns to source
    // strings). Returns true on success — false means we couldn't
    // find a .qm for the requested code, which is non-fatal:
    // QTranslator falls back to the source text.
    bool    setLanguage(const QString& code);
    QString language() const { return lang_; }

private:
    QString lang_;
};

} // namespace ghm::core
