#include "languages.h"

namespace lt {

const QVector<Language> &languages()
{
    static const QVector<Language> table = {
        {QStringLiteral("en"),     QStringLiteral("English")},
        {QStringLiteral("de"),     QStringLiteral("Deutsch (German)")},
        {QStringLiteral("fr"),     QStringLiteral("Français (French)")},
        {QStringLiteral("es"),     QStringLiteral("Español (Spanish)")},
        {QStringLiteral("it"),     QStringLiteral("Italiano (Italian)")},
        {QStringLiteral("pt"),     QStringLiteral("Português (Portuguese)")},
        {QStringLiteral("nl"),     QStringLiteral("Nederlands (Dutch)")},
        {QStringLiteral("pl"),     QStringLiteral("Polski (Polish)")},
        {QStringLiteral("cs"),     QStringLiteral("Čeština (Czech)")},
        {QStringLiteral("sk"),     QStringLiteral("Slovenčina (Slovak)")},
        {QStringLiteral("hu"),     QStringLiteral("Magyar (Hungarian)")},
        {QStringLiteral("ro"),     QStringLiteral("Română (Romanian)")},
        {QStringLiteral("bg"),     QStringLiteral("Български (Bulgarian)")},
        {QStringLiteral("hr"),     QStringLiteral("Hrvatski (Croatian)")},
        {QStringLiteral("sr"),     QStringLiteral("Српски (Serbian)")},
        {QStringLiteral("ru"),     QStringLiteral("Русский (Russian)")},
        {QStringLiteral("uk"),     QStringLiteral("Українська (Ukrainian)")},
        {QStringLiteral("tr"),     QStringLiteral("Türkçe (Turkish)")},
        {QStringLiteral("el"),     QStringLiteral("Ελληνικά (Greek)")},
        {QStringLiteral("ar"),     QStringLiteral("العربية (Arabic)")},
        {QStringLiteral("he"),     QStringLiteral("עברית (Hebrew)")},
        {QStringLiteral("fa"),     QStringLiteral("فارسی (Persian)")},
        {QStringLiteral("hi"),     QStringLiteral("हिन्दी (Hindi)")},
        {QStringLiteral("bn"),     QStringLiteral("বাংলা (Bengali)")},
        {QStringLiteral("th"),     QStringLiteral("ไทย (Thai)")},
        {QStringLiteral("vi"),     QStringLiteral("Tiếng Việt (Vietnamese)")},
        {QStringLiteral("id"),     QStringLiteral("Bahasa Indonesia (Indonesian)")},
        {QStringLiteral("zh-CN"),  QStringLiteral("简体中文 (Chinese, simplified)")},
        {QStringLiteral("zh-TW"),  QStringLiteral("繁體中文 (Chinese, traditional)")},
        {QStringLiteral("ja"),     QStringLiteral("日本語 (Japanese)")},
        {QStringLiteral("ko"),     QStringLiteral("한국어 (Korean)")},
        {QStringLiteral("sv"),     QStringLiteral("Svenska (Swedish)")},
        {QStringLiteral("no"),     QStringLiteral("Norsk (Norwegian)")},
        {QStringLiteral("da"),     QStringLiteral("Dansk (Danish)")},
        {QStringLiteral("fi"),     QStringLiteral("Suomi (Finnish)")},
        {QStringLiteral("et"),     QStringLiteral("Eesti (Estonian)")},
        {QStringLiteral("lv"),     QStringLiteral("Latviešu (Latvian)")},
        {QStringLiteral("lt"),     QStringLiteral("Lietuviu (Lithuanian)")},
        {QStringLiteral("ca"),     QStringLiteral("Catala (Catalan)")},
        {QStringLiteral("eo"),     QStringLiteral("Esperanto")},
    };
    return table;
}

QString languageCodeForLocale(const QString &localeName)
{
    QString wanted = localeName.section(QLatin1Char('_'), 0, 0).toLower();
    wanted.replace(QLatin1Char('-'), QLatin1Char('_'));
    for (const Language &lang : languages()) {
        if (lang.code.compare(wanted, Qt::CaseInsensitive) == 0) {
            return lang.code;
        }
    }
    return QStringLiteral("en");
}

QString languageName(const QString &code)
{
    if (code.isEmpty()) {
        return QString();
    }
    for (const auto &l : languages()) {
        if (l.code == code) {
            return l.name;
        }
    }
    return QString();
}

QString languageNameOrCode(const QString &code)
{
    const QString n = languageName(code);
    return n.isEmpty() ? code : n;
}

} // namespace lt
