// What a cell's words become in a spreadsheet: a number or a date when the cell
// says one, text when it says anything else -- and the sheet names Excel allows.
//
// The rule is deliberately narrow. A PDF cell that reads "1,234.5" is a number
// someone will want to sum, so it goes in as one with a format that shows it the
// way the PDF did. A cell that reads "00123" is an identifier whose leading zeros
// are the point, and a 16-digit number is a card number that a double cannot hold
// exactly; both stay text, because turning either into a number would change what
// the reader sees. Everything not on the list stays text too -- guessing is how a
// spreadsheet starts lying.
#pragma once

#include <QString>

struct ExcelValue
{
    enum class Kind
    {
        Text,
        Number,
        Date,
    };

    Kind kind = Kind::Text;
    // Text: the cell's words, unchanged.
    QString text;
    // Number and Date: the value stored in the cell. Dates are Excel's own serial
    // number (days since 1899-12-30), which is what a date format expects.
    double number = 0;
    // Number and Date: the number format that shows the value as the PDF wrote it.
    QString format;

    bool isNumeric() const { return kind != Kind::Text; }
};

// "1,234.5" -> 1234.5 / "#,##0.0"; "(12)" -> -12 / "0;(0)"; "12%" -> 0.12 / "0%";
// "¥1,200" -> 1200 / "\"¥\"#,##0"; "2026年10月25日" -> a date serial /
// "yyyy\"年\"m\"月\"d\"日\"". "00123" and anything longer than 15 digits stay text.
ExcelValue classifyExcelValue(const QString &text);

// The sheet names a table's export uses, in the reader's language:
// "第 3 页 表 2" / "Page 3 Table 2", and "第 3 页 文字" / "Page 3 Text" for a page
// with no table. Both go through sanitisedSheetName().
QString sheetNameForTable(int page, int table);
QString sheetNameForText(int page);

// A name Excel accepts: at most 31 characters, none of []:*?/\ (replaced with
// '_'), no leading or trailing apostrophe, and never empty.
QString sanitisedSheetName(const QString &name);
