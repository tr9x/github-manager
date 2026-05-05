#pragma once

// Translator - in-process QTranslator that holds an EN→PL phrasebook
// in memory. No .ts/.qm pipeline required; we just override the
// translate() virtual and look strings up in a QHash.
//
// Source language is English (matches the bare strings in the code).
// When the user selects "en", translate() returns an empty QString,
// which makes Qt fall back to the source text. When the user selects
// "pl", we look the source text up in the phrasebook and return the
// translation if we have one (or empty, falling back to English, if
// we don't — partial translation is fine).

#include <QTranslator>
#include <QHash>
#include <QString>

namespace ghm::core {

class Translator : public QTranslator {
    Q_OBJECT
public:
    explicit Translator(QObject* parent = nullptr);

    // "en" or "pl" (other values fall back to "en"/passthrough).
    void    setLanguage(const QString& code);
    QString language() const { return lang_; }

    // QTranslator overrides --------------------------------------------
    QString translate(const char* context,
                      const char* sourceText,
                      const char* disambiguation = nullptr,
                      int n = -1) const override;

    // Telling Qt the translator is "non-empty" forces it to actually
    // call our translate() method on tr() lookups, even when the
    // hashmap might in practice be empty for some strings.
    bool isEmpty() const override { return false; }

private:
    QString                  lang_;
    QHash<QString, QString>  table_;     // sourceText (UTF-8) -> translated
};

} // namespace ghm::core
