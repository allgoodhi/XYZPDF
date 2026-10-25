#include "excelvalue.h"

#include <QCoreApplication>
#include <QDate>
#include <QRegularExpression>
#include <QtMath>

namespace {

// The epoch Excel's serial dates count from. 1900-01-01 is 2, and the leap day
// Excel believes 1900 had is already inside this number, which is why every date
// this program writes (all of them modern) lands on the day Excel shows.
const QDate kExcelEpoch(1899, 12, 30);

QString translated(const char *text)
{
    return QCoreApplication::translate("ExcelExport", text);
}

struct ParsedNumber
{
    bool ok = false;
    bool negative = false;
    bool grouped = false;
    int decimals = 0;
    QString digits; // every digit, separators and sign removed
    QString integerDigits;
};

ParsedNumber parseNumber(QString text)
{
    ParsedNumber parsed;
    text = text.trimmed();
    if (text.startsWith(QLatin1Char('+')))
        text.remove(0, 1);
    else if (text.startsWith(QLatin1Char('-'))) {
        parsed.negative = true;
        text.remove(0, 1);
    }
    if (text.isEmpty())
        return parsed;

    static const QRegularExpression plain(QStringLiteral(R"(^\d+(?:\.\d+)?$)"));
    static const QRegularExpression grouped(QStringLiteral(R"(^\d{1,3}(?:,\d{3})+(?:\.\d+)?$)"));
    const bool hasGrouping = text.contains(QLatin1Char(','));
    if (!(hasGrouping ? grouped : plain).match(text).hasMatch())
        return parsed;

    const int dot = text.indexOf(QLatin1Char('.'));
    parsed.integerDigits = dot < 0 ? text : text.left(dot);
    if (dot >= 0)
        parsed.decimals = int(text.size() - dot - 1);
    QString digits = text;
    digits.remove(QLatin1Char(','));
    digits.remove(QLatin1Char('.'));
    parsed.digits = digits;
    parsed.grouped = hasGrouping;
    parsed.ok = true;
    return parsed;
}

QString numberPattern(bool grouped, int decimals)
{
    QString pattern = grouped ? QStringLiteral("#,##0") : QStringLiteral("0");
    if (decimals > 0)
        pattern += QLatin1Char('.') + QString(decimals, QLatin1Char('0'));
    return pattern;
}

ExcelValue numericValue(double value, const QString &format)
{
    ExcelValue result;
    result.kind = ExcelValue::Kind::Number;
    result.number = value;
    result.format = format;
    return result;
}

// A value the PDF wrote as an identifier rather than a quantity. A leading zero
// or a digit count a double cannot hold exactly makes the text the value.
ExcelValue asText(const QString &text)
{
    ExcelValue result;
    result.kind = ExcelValue::Kind::Text;
    result.text = text;
    return result;
}

} // namespace

ExcelValue classifyExcelValue(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return asText(text);

    // ---- percentages ----
    if (trimmed.endsWith(QLatin1Char('%')) || trimmed.endsWith(QChar(0xFF05))) {
        ParsedNumber parsed = parseNumber(trimmed.left(trimmed.size() - 1));
        if (parsed.ok && parsed.digits.size() <= 15
            && !(parsed.integerDigits.size() > 1 && parsed.integerDigits.startsWith(QLatin1Char('0'))))
        {
            const double value = parsed.digits.toDouble() / qPow(10.0, parsed.decimals) / 100.0;
            return numericValue(parsed.negative ? -value : value,
                                numberPattern(false, parsed.decimals) + QLatin1Char('%'));
        }
        return asText(text);
    }

    // ---- currency ----
    // One leading symbol, then a number. The symbol is kept in the format so the
    // cell still reads the way the PDF did; it is a literal in the format code,
    // quoted, because an unquoted § is a locale's currency token in some readers.
    const QChar first = trimmed.at(0);
    if (first == QChar(0xA5) || first == QChar(0xFFE5) || first == QLatin1Char('$')) {
        ParsedNumber parsed = parseNumber(trimmed.mid(1));
        if (parsed.ok && parsed.digits.size() <= 15
            && !(parsed.integerDigits.size() > 1 && parsed.integerDigits.startsWith(QLatin1Char('0'))))
        {
            const QString pattern = QStringLiteral("\"") + QString(first)
                + QStringLiteral("\"") + numberPattern(parsed.grouped, parsed.decimals);
            const double value = parsed.digits.toDouble() / qPow(10.0, parsed.decimals);
            return numericValue(parsed.negative ? -value : value, pattern);
        }
        return asText(text);
    }

    // ---- parentheses around a negative ----
    if (trimmed.startsWith(QLatin1Char('(')) && trimmed.endsWith(QLatin1Char(')'))) {
        ParsedNumber parsed = parseNumber(trimmed.mid(1, trimmed.size() - 2));
        if (parsed.ok && parsed.digits.size() <= 15
            && !(parsed.integerDigits.size() > 1 && parsed.integerDigits.startsWith(QLatin1Char('0'))))
        {
            const QString pattern = numberPattern(parsed.grouped, parsed.decimals);
            const double value = parsed.digits.toDouble() / qPow(10.0, parsed.decimals);
            // The PDF wrote the negative in brackets, so the cell must keep them:
            // both sides of the format are named, and the negative side is
            // parenthesised.
            return numericValue(-value, pattern + QStringLiteral(";(") + pattern + QLatin1Char(')'));
        }
        return asText(text);
    }

    // ---- dates ----
    // The three spellings the export has to recognise. Only a real calendar date
    // becomes one: "2026-13-40" is text, because there is no date it could mean.
    static const QRegularExpression dashed(QStringLiteral(R"(^(\d{4})-(\d{1,2})-(\d{1,2})$)"));
    static const QRegularExpression slashed(QStringLiteral(R"(^(\d{4})/(\d{1,2})/(\d{1,2})$)"));
    static const QRegularExpression chinese(
        QString::fromUtf8(R"(^(\d{4})年(\d{1,2})月(\d{1,2})日$)"));
    struct DateForm
    {
        const QRegularExpression *pattern;
        const char *format;
    };
    const DateForm forms[] = {
        { &dashed, "yyyy-mm-dd" },
        { &slashed, "yyyy/m/d" },
        { &chinese, "yyyy\"年\"m\"月\"d\"日\"" },
    };
    for (const DateForm &form : forms) {
        const QRegularExpressionMatch match = form.pattern->match(trimmed);
        if (!match.hasMatch())
            continue;
        const QDate date(match.captured(1).toInt(), match.captured(2).toInt(),
                         match.captured(3).toInt());
        if (!date.isValid())
            return asText(text);
        ExcelValue result;
        result.kind = ExcelValue::Kind::Date;
        result.number = double(kExcelEpoch.daysTo(date));
        // A number format, not UI text: the Chinese one carries its literals in
        // quotes so the reader does not need a Chinese locale to show the day.
        result.format = QString::fromUtf8(form.format);
        return result;
    }

    // ---- plain numbers ----
    ParsedNumber parsed = parseNumber(trimmed);
    if (parsed.ok) {
        if (parsed.digits.size() > 15)
            return asText(text);
        if (parsed.integerDigits.size() > 1 && parsed.integerDigits.startsWith(QLatin1Char('0')))
            return asText(text);
        const double value = parsed.digits.toDouble() / qPow(10.0, parsed.decimals);
        return numericValue(parsed.negative ? -value : value,
                            numberPattern(parsed.grouped, parsed.decimals));
    }

    return asText(text);
}

QString sheetNameForTable(int page, int table)
{
    return sanitisedSheetName(
        translated(QT_TRANSLATE_NOOP("ExcelExport", "第 %1 页 表 %2")).arg(page).arg(table));
}

QString sheetNameForText(int page)
{
    return sanitisedSheetName(
        translated(QT_TRANSLATE_NOOP("ExcelExport", "第 %1 页 文字")).arg(page));
}

QString sanitisedSheetName(const QString &name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        const ushort u = c.unicode();
        if (u < 0x20)
            continue;
        if (QStringLiteral("[]:*?/\\").contains(c))
            out += QLatin1Char('_');
        else
            out += c;
    }
    out = out.trimmed();
    while (out.startsWith(QLatin1Char('\'')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('\'')))
        out.chop(1);
    // Excel's limit is 31 characters. Truncating can leave an apostrophe at the
    // end, which Excel also refuses, so it is stripped again.
    if (out.size() > 31)
        out.truncate(31);
    while (out.endsWith(QLatin1Char('\'')))
        out.chop(1);
    out = out.trimmed();
    if (out.isEmpty())
        out = QStringLiteral("Sheet");
    return out;
}
