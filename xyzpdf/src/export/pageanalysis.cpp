#include "pageanalysis.h"

#include <QBuffer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <climits>
#include <cmath>
#include <numeric>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

// ---------------------------------------------------------------------------
// Small geometry
// ---------------------------------------------------------------------------

constexpr double kRuleThickness = 2.6;   // thicker than this, a filled box is a panel
constexpr double kRuleJoin = 3.0;        // pieces of one line this close are one line
constexpr double kParallelMerge = 2.6;   // two parallel lines this close are one border
constexpr double kGridSnap = 3.0;        // grid lines this close are one grid line
constexpr int kDrawingRules = 6;        // this many touching rules are a drawing, not a frame
constexpr double kPi = 3.14159265358979323846;

template <typename G> QRectF advance(const G &glyph)
{
    return glyph.loose.isNull() ? glyph.box : glyph.loose;
}

double median(QVector<double> values)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    return values.at(values.size() / 2);
}

bool isCjk(char32_t code)
{
    return (code >= 0x2E80 && code <= 0x9FFF) || (code >= 0xAC00 && code <= 0xD7AF)
        || (code >= 0xF900 && code <= 0xFAFF) || (code >= 0xFF00 && code <= 0xFFEF)
        || (code >= 0x3000 && code <= 0x303F) || (code >= 0x20000 && code <= 0x2FA1F);
}

// Arabic, Hebrew and their presentation forms: text a reader reads from right to left.
bool isRightToLeft(char32_t code)
{
    return (code >= 0x0590 && code <= 0x08FF) || (code >= 0xFB1D && code <= 0xFDFF) || (code >= 0xFE70 && code <= 0xFEFF);
}

bool isSpace(char32_t code)
{
    return code == 0x20 || code == 0xA0 || code == 0x3000 || code == 0x09;
}

bool nearWhite(const QColor &color)
{
    return !color.isValid() || color.alpha() < 16
        || (color.red() > 245 && color.green() > 245 && color.blue() > 245);
}

// An axis-aligned rectangle drawn as one closed polygon, or nothing.
bool asRectangle(const QPolygonF &polygon, QRectF *rect)
{
    // PDFium stores a rectangle as a move, three lines and a line back to the start
    // with a close flag, so the start can appear twice more at the end; repeated
    // points say nothing about the shape and are dropped before counting corners.
    QVector<QPointF> points;
    for (const QPointF &point : polygon) {
        if (points.isEmpty() || QLineF(points.last(), point).length() >= 0.01)
            points.append(point);
    }
    while (points.size() >= 2 && QLineF(points.first(), points.last()).length() < 0.01)
        points.removeLast();
    if (points.size() != 4)
        return false;
    for (int i = 0; i < 4; ++i) {
        const QPointF a = points.at(i);
        const QPointF b = points.at((i + 1) % 4);
        if (std::fabs(a.x() - b.x()) > 0.05 && std::fabs(a.y() - b.y()) > 0.05)
            return false;
    }
    *rect = polygon.boundingRect();
    return true;
}

QByteArray pngOfColour(const QColor &colour, QSize pixels)
{
    QImage image(pixels.expandedTo(QSize(1, 1)), QImage::Format_ARGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QByteArray encodedPicture(const QImage &image, QString *extension)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    // A drawing is line art and flat colour: PNG keeps its edges, and at the sizes
    // a page's figure comes to it is not much larger than a JPEG.
    if (image.save(&buffer, "PNG")) {
        *extension = QStringLiteral("png");
        return bytes;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

QString decodedFontName(const QString &raw)
{
    // "#CB#CE#CC#E5" is how some writers spell GBK bytes in a name.
    QByteArray bytes;
    for (int i = 0; i < raw.size(); ++i) {
        if (raw.at(i) == QLatin1Char('#') && i + 2 < raw.size()) {
            bool ok = false;
            const int value = raw.mid(i + 1, 2).toInt(&ok, 16);
            if (ok) {
                bytes.append(char(value));
                i += 2;
                continue;
            }
        }
        bytes.append(char(raw.at(i).unicode() & 0xFF));
    }
    bool high = false;
    for (char c : std::as_const(bytes))
        high = high || (uchar(c) >= 0x80);
    if (!high)
        return QString::fromLatin1(bytes);
    // GBK, which is what a Chinese name in a PDF almost always is when it is not
    // ASCII. Decoded by Windows rather than guessed at: Qt's own converters do not
    // carry code page 936.
    const int needed = MultiByteToWideChar(936, MB_ERR_INVALID_CHARS, bytes.constData(),
                                           int(bytes.size()), nullptr, 0);
    if (needed <= 0)
        return QString::fromLatin1(bytes);
    std::wstring wide(size_t(needed), L'\0');
    MultiByteToWideChar(936, 0, bytes.constData(), int(bytes.size()), wide.data(), needed);
    return QString::fromStdWString(wide);
}

// A PDF font's family as the PDF names it: no subset prefix, no Qt weight suffix
// ("Arial-0-700"), no PostScript style part ("Arial-BoldMT", "Arial,Bold").
QString baseFontName(const QString &pdfFont)
{
    QString name = decodedFontName(pdfFont.trimmed());
    if (name.size() > 7 && name.at(6) == QLatin1Char('+'))
        name = name.mid(7);
    static const QRegularExpression qtSuffix(QStringLiteral("-\\d+-\\d+$"));
    name.remove(qtSuffix);
    const int cut = name.indexOf(QRegularExpression(QStringLiteral("[-,]")));
    if (cut > 0)
        name = name.left(cut);
    return name;
}

// How wide a reader sets a character: the advance width Windows gives the font
// Word will use, at a 2048-pixel em so hinting has nothing left to round. Word
// lays a line out with these same unkerned advances, so a line measured here and
// given the character spacing that makes it as wide as the PDF's line is the
// PDF's line in Word too -- not one that comes out a few points wider and wraps
// its last word onto a line of its own (a real book, page after page).
class AdvanceWidths
{
public:
    static AdvanceWidths &instance()
    {
        static AdvanceWidths widths;
        return widths;
    }

    // In points, for `style`'s family, weight, slant and size.
    double of(const RunStyle &style, char32_t code)
    {
        QMutexLocker lock(&mutex);
        const QString key = style.font + QLatin1Char(style.bold ? 'b' : '-') + QLatin1Char(style.italic ? 'i' : '-');
        Face &face = faces[key];
        if (!face.font) {
            LOGFONTW description{};
            description.lfHeight = -kEm;
            description.lfWeight = style.bold ? FW_BOLD : FW_NORMAL;
            description.lfItalic = style.italic ? TRUE : FALSE;
            description.lfCharSet = DEFAULT_CHARSET;
            description.lfQuality = ANTIALIASED_QUALITY;
            const std::wstring name = style.font.left(LF_FACESIZE - 1).toStdWString();
            wcsncpy_s(description.lfFaceName, name.c_str(), _TRUNCATE);
            face.font = CreateFontIndirectW(&description);
        }
        const auto known = face.widths.constFind(code);
        double em = 0;
        if (known != face.widths.constEnd()) {
            em = known.value();
        } else {
            if (!dc)
                dc = CreateCompatibleDC(nullptr);
            const HGDIOBJ before = SelectObject(dc, face.font);
            const QString text = QString::fromUcs4(&code, 1);
            SIZE extent{};
            if (GetTextExtentPoint32W(dc, reinterpret_cast<LPCWSTR>(text.utf16()), int(text.size()), &extent))
                em = double(extent.cx) / kEm;
            SelectObject(dc, before);
            face.widths.insert(code, em);
        }
        const double size = (style.superscript || style.subscript) ? style.size * 0.65 : style.size;
        return em * size;
    }

private:
    static constexpr int kEm = 2048;
    struct Face
    {
        HFONT font = nullptr;
        QHash<char32_t, double> widths;
    };
    QMutex mutex;
    HDC dc = nullptr;
    QHash<QString, Face> faces;
};

// A PDF font as one face: no subset prefix and no Qt weight suffix (Qt names each
// weight of a family "Arial-0-400", "Arial-0-700"), but its style part kept.
QString weightKey(const QString &pdfFont)
{
    QString name = pdfFont.trimmed();
    if (name.size() > 7 && name.at(6) == QLatin1Char('+'))
        name = name.mid(7);
    static const QRegularExpression qtSuffix(QStringLiteral("-\\d+-\\d+$"));
    name.remove(qtSuffix);
    return name;
}

// Whether Windows has a family by this name, which is what decides whether a
// reader on this machine shows it rather than something it substitutes.
bool fontInstalled(const QString &family)
{
    static QMutex mutex;
    static QHash<QString, bool> known;
    QMutexLocker lock(&mutex);
    const QString key = family.toLower();
    const auto cached = known.constFind(key);
    if (cached != known.constEnd())
        return cached.value();
    bool found = false;
    if (!family.isEmpty() && family.size() < LF_FACESIZE) {
        LOGFONTW description{};
        description.lfCharSet = DEFAULT_CHARSET;
        const std::wstring name = family.toStdWString();
        wcsncpy_s(description.lfFaceName, name.c_str(), _TRUNCATE);
        const HDC dc = GetDC(nullptr);
        EnumFontFamiliesExW(
            dc, &description,
            [](const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM found) -> int {
                *reinterpret_cast<bool *>(found) = true;
                return 0;
            },
            reinterpret_cast<LPARAM>(&found), 0);
        ReleaseDC(nullptr, dc);
    }
    known.insert(key, found);
    return found;
}

QString familyFromName(const QString &pdfFont, bool *sure);

} // namespace

QString wordFontFamily(const QString &pdfFont, const QString &embeddedFamily)
{
    // The PDF's name first when it says what the font is; then what the embedded
    // font program calls itself -- a writer that names its fonts "CIDFont+F1" and
    // "CIDFont+F2" (a real manual) still embeds SimSun and Calibri, and Word given
    // "CIDFont+F2" sets the text in whatever it substitutes, wider, and wraps.
    bool sure = false;
    const QString guessed = familyFromName(pdfFont, &sure);
    if (sure)
        return guessed;
    if (!embeddedFamily.isEmpty()) {
        bool embeddedSure = false;
        const QString mapped = familyFromName(embeddedFamily, &embeddedSure);
        if (fontInstalled(mapped))
            return mapped;
        if (fontInstalled(embeddedFamily))
            return embeddedFamily;
    }
    if (fontInstalled(guessed) || embeddedFamily.isEmpty())
        return guessed;
    return embeddedFamily;
}

namespace {

QString familyFromName(const QString &pdfFont, bool *sure)
{
    *sure = true;
    const QString name = baseFontName(pdfFont);
    QString key = name.toLower();
    key.remove(QLatin1Char(' '));
    for (const char *suffix : {"psmt", "mt", "ps"}) {
        if (key.endsWith(QLatin1String(suffix)) && key.size() > int(strlen(suffix)) + 2)
            key.chop(int(strlen(suffix)));
    }
    static const QHash<QString, QString> known = {
        {QStringLiteral("arial"), QStringLiteral("Arial")},
        {QStringLiteral("helvetica"), QStringLiteral("Arial")},
        {QStringLiteral("helveticaneue"), QStringLiteral("Arial")},
        {QStringLiteral("liberationsans"), QStringLiteral("Arial")},
        {QStringLiteral("timesnewroman"), QStringLiteral("Times New Roman")},
        {QStringLiteral("times"), QStringLiteral("Times New Roman")},
        {QStringLiteral("timesroman"), QStringLiteral("Times New Roman")},
        {QStringLiteral("liberationserif"), QStringLiteral("Times New Roman")},
        {QStringLiteral("couriernew"), QStringLiteral("Courier New")},
        {QStringLiteral("courier"), QStringLiteral("Courier New")},
        {QStringLiteral("liberationmono"), QStringLiteral("Courier New")},
        {QStringLiteral("symbol"), QStringLiteral("Symbol")},
        {QStringLiteral("zapfdingbats"), QStringLiteral("Wingdings")},
        {QStringLiteral("wingdings"), QStringLiteral("Wingdings")},
        {QStringLiteral("calibri"), QStringLiteral("Calibri")},
        {QStringLiteral("calibrilight"), QStringLiteral("Calibri Light")},
        {QStringLiteral("cambria"), QStringLiteral("Cambria")},
        {QStringLiteral("cambriamath"), QStringLiteral("Cambria Math")},
        {QStringLiteral("verdana"), QStringLiteral("Verdana")},
        {QStringLiteral("tahoma"), QStringLiteral("Tahoma")},
        {QStringLiteral("georgia"), QStringLiteral("Georgia")},
        {QStringLiteral("garamond"), QStringLiteral("Garamond")},
        {QStringLiteral("segoeui"), QStringLiteral("Segoe UI")},
        {QStringLiteral("consolas"), QStringLiteral("Consolas")},
        {QStringLiteral("trebuchetms"), QStringLiteral("Trebuchet MS")},
        {QStringLiteral("bookantiqua"), QStringLiteral("Book Antiqua")},
        {QStringLiteral("centurygothic"), QStringLiteral("Century Gothic")},
        {QStringLiteral("simsun"), QStringLiteral("SimSun")},
        {QStringLiteral("nsimsun"), QStringLiteral("NSimSun")},
        {QStringLiteral("songti"), QStringLiteral("SimSun")},
        {QStringLiteral("stsong"), QStringLiteral("SimSun")},
        {QStringLiteral("adobesongstd"), QStringLiteral("SimSun")},
        {QStringLiteral("simhei"), QStringLiteral("SimHei")},
        {QStringLiteral("stheiti"), QStringLiteral("SimHei")},
        {QStringLiteral("adobeheitistd"), QStringLiteral("SimHei")},
        {QStringLiteral("kaiti"), QStringLiteral("KaiTi")},
        {QStringLiteral("kaiti_gb2312"), QStringLiteral("KaiTi")},
        {QStringLiteral("stkaiti"), QStringLiteral("KaiTi")},
        {QStringLiteral("fangsong"), QStringLiteral("FangSong")},
        {QStringLiteral("fangsong_gb2312"), QStringLiteral("FangSong")},
        {QStringLiteral("stfangsong"), QStringLiteral("FangSong")},
        {QStringLiteral("microsoftyahei"), QStringLiteral("Microsoft YaHei")},
        {QStringLiteral("microsoftyaheiui"), QStringLiteral("Microsoft YaHei UI")},
        {QStringLiteral("dengxian"), QStringLiteral("DengXian")},
        {QStringLiteral("mingliu"), QStringLiteral("MingLiU")},
        {QStringLiteral("pmingliu"), QStringLiteral("PMingLiU")},
        {QStringLiteral("msmincho"), QStringLiteral("MS Mincho")},
        {QStringLiteral("msgothic"), QStringLiteral("MS Gothic")},
        {QStringLiteral("malgungothic"), QStringLiteral("Malgun Gothic")},
        {QStringLiteral("宋体"), QStringLiteral("SimSun")},
        {QStringLiteral("新宋体"), QStringLiteral("NSimSun")},
        {QStringLiteral("黑体"), QStringLiteral("SimHei")},
        {QStringLiteral("楷体"), QStringLiteral("KaiTi")},
        {QStringLiteral("楷体_gb2312"), QStringLiteral("KaiTi")},
        {QStringLiteral("仿宋"), QStringLiteral("FangSong")},
        {QStringLiteral("仿宋_gb2312"), QStringLiteral("FangSong")},
        {QStringLiteral("微软雅黑"), QStringLiteral("Microsoft YaHei")},
        {QStringLiteral("等线"), QStringLiteral("DengXian")},
    };
    const auto found = known.constFind(key);
    if (found != known.constEnd())
        return found.value();
    if (name.isEmpty()) {
        *sure = false;
        return QStringLiteral("Arial");
    }

    // A Chinese face no Windows install carries under its PostScript name --
    // FounderType "FZSSJW--GB1-0", Hanyi "HYc1gj", SinoType "STZhongsong", a CID
    // font "...-UniGB-UCS2-H" -- goes to the installed face of its kind, which is
    // what the name's own hint says it is (书宋 is a Song, 黑体 a Hei). Left as it is,
    // Word would substitute whatever it likes.
    const QString lowerRaw = pdfFont.toLower();
    bool cjkName = key.startsWith(QLatin1String("fz")) || key.startsWith(QLatin1String("hy"))
        || key.startsWith(QLatin1String("st")) || key.startsWith(QLatin1String("df"))
        || lowerRaw.contains(QLatin1String("gb1")) || lowerRaw.contains(QLatin1String("gbk"))
        || lowerRaw.contains(QLatin1String("gb2312")) || lowerRaw.contains(QLatin1String("unigb"));
    for (const QChar c : name)
        cjkName = cjkName || (c.unicode() >= 0x4E00 && c.unicode() <= 0x9FFF);
    if (cjkName) {
        const auto has = [&](std::initializer_list<const char *> hints) {
            for (const char *hint : hints) {
                if (key.contains(QString::fromUtf8(hint)))
                    return true;
            }
            return false;
        };
        if (has({"kai", "kt", "楷"}))
            return QStringLiteral("KaiTi");
        if (has({"fang", "fs", "仿"}))
            return QStringLiteral("FangSong");
        if (has({"hei", "ht", "黑", "gothic"}))
            return QStringLiteral("SimHei");
        if (has({"song", "ss", "sung", "宋", "ming"}))
            return QStringLiteral("SimSun");
        *sure = false;
        return QStringLiteral("Microsoft YaHei");
    }
    *sure = false;
    // "MyriadPro" -> "Myriad Pro": the family a font menu lists, as near as the
    // PostScript name lets it be guessed.
    QString spaced;
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        if (i > 0 && c.isUpper() && name.at(i - 1).isLower())
            spaced += QLatin1Char(' ');
        spaced += c;
    }
    return spaced;
}


// ---------------------------------------------------------------------------
// The working set of a page
// ---------------------------------------------------------------------------

struct Glyph
{
    char32_t code = 0;
    QRectF box;      // ink box, or the slot a space occupies
    QRectF loose;    // ascent..descent
    double baseline = 0;
    double size = 0;
    RunStyle style;
    QString uri;
    int targetPage = -1;
    bool used = false;

    double centreX() const { return box.center().x(); }
    double centreY() const { return loose.isNull() ? box.center().y() : loose.center().y(); }
};

struct Fill
{
    QRectF box;
    QColor color;
    bool used = false;
};

struct Art
{
    QRectF box;
    bool used = false;
};

struct Picture
{
    QRectF box;
    QByteArray bytes;
    QString extension;
    bool used = false;
};

struct Row
{
    QVector<int> glyphs; // indexes, left to right
    double baseline = 0;
    double size = 0;     // the row's main size
    QRectF box;
    // Positions inside the row where it splits into pieces that sit apart: a
    // borderless table, a tab, a column. Glyph index positions in `glyphs`.
    QVector<int> breaks;
};

struct Page
{
    QSizeF size;
    QVector<Glyph> glyphs;
    QVector<RuleLine> rules;
    QVector<bool> ruleUsed;
    QVector<Fill> fills;
    QVector<Art> art;          // drawn shapes that are neither rules nor plain panels
    QVector<Picture> pictures;
    double bodySize = 11;
};

// ---------------------------------------------------------------------------
// Reading the content
// ---------------------------------------------------------------------------

Page prepared(const PageContent &content)
{
    Page page;
    page.size = content.size;

    // ---- glyphs ----
    // Bold is a property of a font family's weights, not of a number: a document's
    // regular may be 365 and its bold 520 (both measured in one Qt PDF), so a
    // weight counts as bold when it is clearly heavier than the lightest weight
    // the same family is used at on this page, or heavy on its own.
    // Keyed by the PDF's own font, style part and all, not the Word family it maps
    // to: two unrelated CJK fonts that land on one fallback family must not make
    // each other bold (a 310 header font made a 465 chapter title bold in a real
    // book), and neither must a font's italic face its upright one (the same book's
    // KaiTi header, ",Italic" at 310, made the KaiTi in its body text bold).
    QHash<QString, int> lightest;
    for (const ContentChar &ch : content.chars) {
        const QString family = weightKey(ch.font);
        auto it = lightest.find(family);
        if (it == lightest.end())
            lightest.insert(family, ch.weight);
        else
            it.value() = qMin(it.value(), ch.weight);
    }
    // Text something opaque is painted over afterwards is not on the page a reader
    // sees (a panel drawn over its own words, a white-out box over a line).
    const auto covered = [&content](const ContentChar &ch) {
        if (ch.z <= 0)
            return false;
        const QPointF centre = ch.box.isEmpty() ? ch.loose.center() : ch.box.center();
        for (const ContentPath &path : content.paths) {
            if (path.z <= ch.z || !path.fill || path.fillColor.alpha() < 250 || !path.bounds.contains(centre))
                continue;
            for (const QPolygonF &polygon : path.polygons) {
                if (polygon.containsPoint(centre, Qt::OddEvenFill))
                    return true;
            }
        }
        for (const ContentImage &image : content.images) {
            // Only a JPEG is known to be opaque all over.
            if (image.z > ch.z && image.extension == QLatin1String("jpeg") && image.box.contains(centre))
                return true;
        }
        return false;
    };
    for (const ContentChar &ch : content.chars) {
        if (ch.code == 0x0D || ch.code == 0x0A || ch.code == 0x02 || ch.code == 0xFFFE)
            continue;
        if (covered(ch))
            continue;
        if (ch.generated && !isSpace(ch.code))
            continue;
        const double angle = std::fmod(std::fabs(ch.angle), 2 * kPi);
        if (angle > 0.05 && angle < 2 * kPi - 0.05) {
            // Rotated text belongs to a drawing (an axis label, a stamp); it goes in
            // with the drawing as a picture.
            if (!ch.box.isEmpty())
                page.art.append({ch.box, false});
            continue;
        }
        Glyph glyph;
        glyph.code = ch.code;
        glyph.box = ch.box;
        glyph.loose = ch.loose;
        glyph.baseline = ch.baseline;
        glyph.size = ch.size > 0 ? ch.size : qMax(1.0, ch.loose.height() / 1.2);
        glyph.style.font = wordFontFamily(ch.font, ch.family);
        glyph.style.eastAsian = isCjk(ch.code);
        glyph.style.size = std::round(glyph.size * 2.0) / 2.0;
        const int least = lightest.value(weightKey(ch.font), ch.weight);
        glyph.style.bold = ch.weight >= 600 || (ch.weight >= least + 120 && ch.weight >= 450);
        glyph.style.italic = ch.italic;
        glyph.style.color = (ch.color.isValid() && (ch.color.red() > 40 || ch.color.green() > 40
                                                    || ch.color.blue() > 40))
                                ? ch.color
                                : QColor();
        if (glyph.box.isEmpty() && isSpace(glyph.code)) {
            // A space PDFium inserted between two words has no geometry of its own
            // (its box is the page corner). It belongs right after the character
            // before it in the content stream, so that is where it is put -- a
            // sliver at that character's right edge, on its baseline.
            if (page.glyphs.isEmpty())
                continue;
            const Glyph &before = page.glyphs.last();
            // After it means on its left when it is read right to left.
            const double at = isRightToLeft(before.code) ? before.box.left() - 0.01 : before.box.right();
            glyph.box = QRectF(at, before.box.top(), 0.01, qMax(0.01, before.box.height()));
            glyph.loose = QRectF(at, before.loose.top(), 0.01, qMax(0.01, before.loose.height()));
            glyph.baseline = before.baseline;
            glyph.size = before.size;
            glyph.style = before.style;
        }
        if (glyph.box.isNull() && glyph.loose.isNull())
            continue;
        if (glyph.box.isEmpty())
            glyph.box = glyph.loose;
        page.glyphs.append(glyph);
    }
    // A space the next character is then moved back over is not a word gap: old
    // typesetters spaced digits and letters out like that ("1 9 8 3", "Ve r i l o g"
    // in a real book -- the space's slot is 2.5pt and the next glyph starts 0.3pt
    // into it). A real word gap leaves the next glyph past the middle of the slot.
    for (qsizetype i = page.glyphs.size() - 2; i >= 0; --i) {
        const Glyph &space = page.glyphs.at(i);
        const Glyph &next = page.glyphs.at(i + 1);
        // Right-to-left text moves the pen the other way: its next character is meant to be on
        // the left, and the rule below would take every word gap out of an Arabic line.
        if (!isSpace(space.code) || space.box.width() < 0.5 || isSpace(next.code) || isRightToLeft(next.code)
            || std::fabs(next.baseline - space.baseline) > 0.3 * space.size)
            continue;
        // The next character's pen position, not its ink: bold Courier "`define"
        // spaced out the same way has its ink 0.02pt past the middle of the slot
        // and its pen 0.4pt short of it.
        const double nextPen = next.loose.isNull() ? next.box.left() : next.loose.left();
        if (nextPen < space.box.left() + 0.5 * space.box.width())
            page.glyphs.removeAt(i);
    }

    // ---- links ----
    for (const ContentLink &link : content.links) {
        for (Glyph &glyph : page.glyphs) {
            if (link.box.contains(glyph.box.center())) {
                glyph.uri = link.uri;
                glyph.targetPage = link.targetPage;
            }
        }
    }

    // ---- paths ----
    for (const ContentPath &path : content.paths) {
        if (path.curved) {
            if (!(path.fill && nearWhite(path.fillColor) && !path.stroke))
                page.art.append({path.bounds, false});
            continue;
        }
        bool consumed = false;
        if (path.fill && !nearWhite(path.fillColor)) {
            bool allRectangles = true;
            QVector<QRectF> rects;
            for (const QPolygonF &polygon : path.polygons) {
                QRectF rect;
                if (!asRectangle(polygon, &rect)) {
                    allRectangles = false;
                    break;
                }
                rects.append(rect);
            }
            if (allRectangles) {
                for (const QRectF &rect : std::as_const(rects)) {
                    const double thin = qMin(rect.width(), rect.height());
                    const double length = qMax(rect.width(), rect.height());
                    if (thin <= kRuleThickness && length >= 2.0) {
                        RuleLine line;
                        line.horizontal = rect.width() >= rect.height();
                        line.position = line.horizontal ? rect.center().y() : rect.center().x();
                        line.from = line.horizontal ? rect.left() : rect.top();
                        line.to = line.horizontal ? rect.right() : rect.bottom();
                        line.width = qMax(0.25, thin);
                        line.color = path.fillColor;
                        page.rules.append(line);
                    } else if (thin > kRuleThickness) {
                        page.fills.append({rect, path.fillColor, false});
                    }
                }
                consumed = true;
            } else {
                page.art.append({path.bounds, false});
                continue;
            }
        }
        if (path.stroke && !nearWhite(path.strokeColor)) {
            if (path.strokeWidth > 4.0) {
                page.art.append({path.bounds, false});
                continue;
            }
            for (const QPolygonF &polygon : path.polygons) {
                for (int i = 0; i + 1 < polygon.size(); ++i) {
                    const QPointF a = polygon.at(i);
                    const QPointF b = polygon.at(i + 1);
                    const double dx = std::fabs(a.x() - b.x());
                    const double dy = std::fabs(a.y() - b.y());
                    if (dx < 0.05 && dy < 0.05)
                        continue;
                    RuleLine line;
                    if (dy <= 0.5 && dx >= 1.0) {
                        line.horizontal = true;
                        line.position = (a.y() + b.y()) / 2;
                        line.from = qMin(a.x(), b.x());
                        line.to = qMax(a.x(), b.x());
                    } else if (dx <= 0.5 && dy >= 1.0) {
                        line.horizontal = false;
                        line.position = (a.x() + b.x()) / 2;
                        line.from = qMin(a.y(), b.y());
                        line.to = qMax(a.y(), b.y());
                    } else {
                        // A diagonal: part of a drawing.
                        page.art.append({QRectF(a, b).normalized().adjusted(-0.5, -0.5, 0.5, 0.5), false});
                        continue;
                    }
                    line.width = qMax(0.25, path.strokeWidth);
                    line.color = path.strokeColor;
                    page.rules.append(line);
                }
            }
            consumed = true;
        }
        Q_UNUSED(consumed)
    }

    // ---- pictures ----
    for (const ContentImage &image : content.images) {
        if (image.bytes.isEmpty() || image.box.isEmpty())
            continue;
        page.pictures.append({image.box, image.bytes, image.extension, false});
    }

    page.rules = normalisedRules(page.rules);
    page.ruleUsed = QVector<bool>(page.rules.size(), false);

    // The size most of the page's characters are set in.
    QHash<int, int> bySize;
    for (const Glyph &glyph : std::as_const(page.glyphs)) {
        if (!isSpace(glyph.code))
            ++bySize[int(std::lround(glyph.style.size * 2))];
    }
    int best = 0;
    int bestCount = -1;
    for (auto it = bySize.constBegin(); it != bySize.constEnd(); ++it) {
        if (it.value() > bestCount) {
            best = it.key();
            bestCount = it.value();
        }
    }
    page.bodySize = best > 0 ? best / 2.0 : 11.0;
    return page;
}

} // namespace

// ---------------------------------------------------------------------------
// Rules and tables
// ---------------------------------------------------------------------------

QVector<RuleLine> normalisedRules(const QVector<RuleLine> &input)
{
    QVector<RuleLine> result;
    for (const bool horizontal : {true, false}) {
        QVector<RuleLine> lines;
        for (const RuleLine &line : input) {
            if (line.horizontal == horizontal)
                lines.append(line);
        }
        std::sort(lines.begin(), lines.end(), [](const RuleLine &a, const RuleLine &b) {
            return a.position < b.position;
        });
        // Group by position: lines within kParallelMerge of the group's first one.
        // A table drawn with a frame around each cell puts two lines a point apart
        // between neighbours, and they are one border to a reader.
        int start = 0;
        while (start < lines.size()) {
            int end = start;
            while (end + 1 < lines.size()
                   && lines.at(end + 1).position - lines.at(start).position <= kParallelMerge)
                ++end;
            QVector<RuleLine> group(lines.begin() + start, lines.begin() + end + 1);
            std::sort(group.begin(), group.end(), [](const RuleLine &a, const RuleLine &b) {
                return a.from < b.from;
            });
            // Join along the line: overlapping or nearly touching pieces.
            QVector<RuleLine> joined;
            for (const RuleLine &line : std::as_const(group)) {
                if (!joined.isEmpty() && line.from <= joined.last().to + kRuleJoin) {
                    RuleLine &last = joined.last();
                    const double lengthA = last.to - last.from;
                    const double lengthB = line.to - line.from;
                    last.position = (last.position * lengthA + line.position * lengthB)
                        / qMax(0.01, lengthA + lengthB);
                    last.to = qMax(last.to, line.to);
                    last.width = qMax(last.width, line.width);
                    if (qGray(line.color.rgb()) < qGray(last.color.rgb()))
                        last.color = line.color;
                } else {
                    joined.append(line);
                }
            }
            result += joined;
            start = end + 1;
        }
    }
    return result;
}

namespace {

QVector<double> snapped(QVector<double> values)
{
    std::sort(values.begin(), values.end());
    QVector<double> out;
    int start = 0;
    while (start < values.size()) {
        int end = start;
        while (end + 1 < values.size() && values.at(end + 1) - values.at(start) <= kGridSnap)
            ++end;
        double sum = 0;
        for (int i = start; i <= end; ++i)
            sum += values.at(i);
        out.append(sum / (end - start + 1));
        start = end + 1;
    }
    return out;
}

// How much of [from, to] the lines at `position` (within kGridSnap) cover.
double coverage(const QVector<RuleLine> &lines, bool horizontal, double position, double from,
                double to, LayoutBorder *border)
{
    if (to <= from)
        return 1.0;
    QVector<QPair<double, double>> pieces;
    for (const RuleLine &line : lines) {
        if (line.horizontal != horizontal || std::fabs(line.position - position) > kGridSnap)
            continue;
        const double a = qMax(from, line.from);
        const double b = qMin(to, line.to);
        if (b > a) {
            pieces.append({a, b});
            if (border) {
                border->width = line.width;
                border->color = line.color;
            }
        }
    }
    std::sort(pieces.begin(), pieces.end());
    double covered = 0;
    double reach = from;
    for (const auto &piece : std::as_const(pieces)) {
        const double a = qMax(reach, piece.first);
        if (piece.second > a) {
            covered += piece.second - a;
            reach = piece.second;
        }
    }
    return covered / (to - from);
}

} // namespace

QVector<std::shared_ptr<LayoutTable>> tablesFromRules(const QVector<RuleLine> &lines,
                                                      QVector<bool> *used)
{
    QVector<std::shared_ptr<LayoutTable>> tables;
    const int count = lines.size();
    if (used)
        used->fill(false, count);

    // Connected groups: a horizontal and a vertical line are connected when they
    // cross or touch.
    QVector<int> parent(count);
    std::iota(parent.begin(), parent.end(), 0);
    const std::function<int(int)> root = [&](int i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };
    for (int h = 0; h < count; ++h) {
        if (!lines.at(h).horizontal)
            continue;
        for (int v = 0; v < count; ++v) {
            if (lines.at(v).horizontal)
                continue;
            const RuleLine &a = lines.at(h);
            const RuleLine &b = lines.at(v);
            if (b.position >= a.from - kGridSnap && b.position <= a.to + kGridSnap
                && a.position >= b.from - kGridSnap && a.position <= b.to + kGridSnap)
                parent[root(h)] = root(v);
        }
    }
    QHash<int, QVector<int>> groups;
    for (int i = 0; i < count; ++i)
        groups[root(i)].append(i);

    for (const QVector<int> &group : std::as_const(groups)) {
        QVector<double> xs;
        QVector<double> ys;
        QVector<RuleLine> members;
        for (int index : group) {
            members.append(lines.at(index));
            (lines.at(index).horizontal ? ys : xs).append(lines.at(index).position);
        }
        xs = snapped(xs);
        ys = snapped(ys);
        if (xs.size() < 2 || ys.size() < 2)
            continue;
        const int columns = xs.size() - 1;
        const int rows = ys.size() - 1;
        if (xs.last() - xs.first() < 8 || ys.last() - ys.first() < 6)
            continue;

        // Every grid cell starts as its own set; a missing inner line joins the two
        // cells either side of it. That is a merged cell, read off the drawing.
        QVector<int> set(rows * columns);
        std::iota(set.begin(), set.end(), 0);
        const std::function<int(int)> top = [&](int i) {
            while (set[i] != i)
                i = set[i] = set[set[i]];
            return i;
        };
        const auto at = [columns](int r, int c) { return r * columns + c; };
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < columns; ++c) {
                if (c + 1 < columns
                    && coverage(members, false, xs.at(c + 1), ys.at(r), ys.at(r + 1), nullptr) < 0.5)
                    set[top(at(r, c))] = top(at(r, c + 1));
                if (r + 1 < rows
                    && coverage(members, true, ys.at(r + 1), xs.at(c), xs.at(c + 1), nullptr) < 0.5)
                    set[top(at(r, c))] = top(at(r + 1, c));
            }
        }
        QHash<int, QVector<int>> spans;
        for (int i = 0; i < rows * columns; ++i)
            spans[top(i)].append(i);

        auto table = std::make_shared<LayoutTable>();
        table->columns = xs;
        table->rows = ys;
        table->box = QRectF(QPointF(xs.first(), ys.first()), QPointF(xs.last(), ys.last()));
        QVector<bool> placed(rows * columns, false);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < columns; ++c) {
                if (placed.at(at(r, c)))
                    continue;
                const QVector<int> &members_ = spans.value(top(at(r, c)));
                int r1 = r, c1 = c;
                for (int index : members_) {
                    r1 = qMax(r1, index / columns);
                    c1 = qMax(c1, index % columns);
                }
                // A span that is not a rectangle (an L-shape, from a grid drawn
                // with an inner line missing in a way no table means) is split back
                // into its grid cells rather than guessed at.
                bool rectangle = members_.size() == (r1 - r + 1) * (c1 - c + 1);
                for (int rr = r; rectangle && rr <= r1; ++rr)
                    for (int cc = c; rectangle && cc <= c1; ++cc)
                        rectangle = top(at(rr, cc)) == top(at(r, c)) && !placed.at(at(rr, cc));
                if (!rectangle) {
                    r1 = r;
                    c1 = c;
                }
                LayoutCell cell;
                cell.row = r;
                cell.column = c;
                cell.rowSpan = r1 - r + 1;
                cell.columnSpan = c1 - c + 1;
                cell.box = QRectF(QPointF(xs.at(c), ys.at(r)), QPointF(xs.at(c1 + 1), ys.at(r1 + 1)));
                cell.top.present = coverage(members, true, ys.at(r), xs.at(c), xs.at(c1 + 1), &cell.top) >= 0.5;
                cell.bottom.present
                    = coverage(members, true, ys.at(r1 + 1), xs.at(c), xs.at(c1 + 1), &cell.bottom) >= 0.5;
                cell.left.present = coverage(members, false, xs.at(c), ys.at(r), ys.at(r1 + 1), &cell.left) >= 0.5;
                cell.right.present
                    = coverage(members, false, xs.at(c1 + 1), ys.at(r), ys.at(r1 + 1), &cell.right) >= 0.5;
                for (int rr = r; rr <= r1; ++rr)
                    for (int cc = c; cc <= c1; ++cc)
                        placed[at(rr, cc)] = true;
                table->cells.append(cell);
            }
        }
        if (used) {
            for (int index : group)
                (*used)[index] = true;
        }
        tables.append(table);
    }
    std::sort(tables.begin(), tables.end(),
              [](const std::shared_ptr<LayoutTable> &a, const std::shared_ptr<LayoutTable> &b) {
                  return a->box.top() < b->box.top();
              });
    return tables;
}

namespace {

// ---------------------------------------------------------------------------
// Rows of text
// ---------------------------------------------------------------------------

// The glyphs given, gathered into rows (one visual line each), top to bottom.
QVector<Row> rowsOf(const QVector<Glyph> &glyphs, const QVector<int> &indexes)
{
    QVector<int> order = indexes;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return glyphs.at(a).baseline < glyphs.at(b).baseline;
    });
    QVector<Row> rows;
    for (int index : std::as_const(order)) {
        const Glyph &glyph = glyphs.at(index);
        if (!rows.isEmpty()) {
            Row &row = rows.last();
            const double tolerance = 0.35 * qMax(glyph.size, row.size);
            if (std::fabs(glyph.baseline - row.baseline) <= tolerance) {
                row.glyphs.append(index);
                if (!isSpace(glyph.code))
                    row.size = qMax(row.size, glyph.size);
                continue;
            }
        }
        Row row;
        row.glyphs.append(index);
        row.baseline = glyph.baseline;
        row.size = glyph.size;
        rows.append(row);
    }

    // A superscript or subscript sits on its own baseline but inside its line:
    // a row of small glyphs whose band overlaps a neighbour's goes into that row.
    for (int i = 0; i < rows.size(); ++i) {
        const auto band = [&](const Row &row) {
            QRectF box;
            for (int index : row.glyphs)
                box = box.isNull() ? glyphs.at(index).loose : box.united(glyphs.at(index).loose);
            return box;
        };
        bool merged = false;
        for (int j : {i - 1, i + 1}) {
            if (j < 0 || j >= rows.size())
                continue;
            if (rows.at(i).size >= rows.at(j).size * 0.85)
                continue;
            const QRectF mine = band(rows.at(i));
            const QRectF theirs = band(rows.at(j));
            const double overlap = qMin(mine.bottom(), theirs.bottom()) - qMax(mine.top(), theirs.top());
            if (overlap > 0.4 * mine.height() && mine.left() >= theirs.left() - 2
                && mine.right() <= theirs.right() + rows.at(j).size * 3) {
                rows[j].glyphs += rows.at(i).glyphs;
                rows.removeAt(i);
                --i;
                merged = true;
                break;
            }
        }
        Q_UNUSED(merged)
    }

    for (Row &row : rows) {
        std::sort(row.glyphs.begin(), row.glyphs.end(), [&](int a, int b) {
            return glyphs.at(a).box.left() < glyphs.at(b).box.left();
        });
        // The row's own baseline is that of its main-size glyphs.
        QVector<double> baselines;
        row.box = QRectF();
        for (int index : std::as_const(row.glyphs)) {
            const Glyph &glyph = glyphs.at(index);
            if (isSpace(glyph.code))
                continue;
            if (glyph.size >= row.size * 0.85)
                baselines.append(glyph.baseline);
            const QRectF extent = glyph.loose.isNull() ? glyph.box : glyph.loose;
            row.box = row.box.isNull() ? extent : row.box.united(extent);
        }
        if (!baselines.isEmpty())
            row.baseline = median(baselines);
        // Pieces of the row that stand apart.
        row.breaks.clear();
        int previous = -1;
        for (int k = 0; k < row.glyphs.size(); ++k) {
            const Glyph &glyph = glyphs.at(row.glyphs.at(k));
            if (isSpace(glyph.code))
                continue;
            if (previous >= 0) {
                const Glyph &before = glyphs.at(row.glyphs.at(previous));
                const double gap = advance(glyph).left() - advance(before).right();
                if (gap > qMax(2.2 * row.size, 18.0))
                    row.breaks.append(k);
            }
            previous = k;
        }
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const Row &row) { return row.box.isNull(); }),
               rows.end());
    return rows;
}

// Consecutive rows that share an empty vertical channel -- a gap in the same place
// on every line -- are a table without lines (or a list of label/value pairs, or a
// table of contents): each row is split at the channel and laid out with tab stops
// there. A paragraph's word gaps never line up like that over three lines.
void splitAtChannels(QVector<Row> &rows, const QVector<Glyph> &glyphs)
{
    int start = 0;
    while (start < rows.size()) {
        // A run of rows close enough to be one block.
        int end = start;
        while (end + 1 < rows.size()) {
            const Row &a = rows.at(end);
            const Row &b = rows.at(end + 1);
            if (b.baseline - a.baseline > 2.6 * qMax(a.size, b.size))
                break;
            ++end;
        }
        // Try every window of at least two rows inside the run, longest first.
        int i = start;
        while (i <= end) {
            int bestJ = -1;
            QVector<QPair<double, double>> bestChannels;
            for (int j = end; j >= i + 1; --j) {
                const double left = [&] {
                    double v = 1e9;
                    for (int r = i; r <= j; ++r)
                        v = qMin(v, rows.at(r).box.left());
                    return v;
                }();
                const double right = [&] {
                    double v = -1e9;
                    for (int r = i; r <= j; ++r)
                        v = qMax(v, rows.at(r).box.right());
                    return v;
                }();
                const double unit = 0.5;
                const int bins = int((right - left) / unit) + 1;
                if (bins <= 2 || bins > 20000)
                    continue;
                QVector<int> covered(bins, 0);
                double sizeSum = 0;
                for (int r = i; r <= j; ++r) {
                    sizeSum += rows.at(r).size;
                    for (int index : rows.at(r).glyphs) {
                        const Glyph &glyph = glyphs.at(index);
                        if (isSpace(glyph.code))
                            continue;
                        const int a = qBound(0, int((glyph.box.left() - left) / unit), bins - 1);
                        const int b = qBound(0, int((glyph.box.right() - left) / unit), bins - 1);
                        for (int x = a; x <= b; ++x)
                            covered[x] = 1;
                    }
                }
                const double size = sizeSum / (j - i + 1);
                // Three rows sharing an empty strip half an em wide is already no
                // accident; two rows have to show a wider one.
                const double least = (j - i + 1) >= 3 ? 0.55 * size : 1.2 * size;
                QVector<QPair<double, double>> channels;
                int x = 0;
                while (x < bins) {
                    if (covered.at(x)) {
                        ++x;
                        continue;
                    }
                    int y = x;
                    while (y + 1 < bins && !covered.at(y + 1))
                        ++y;
                    const double from = left + x * unit;
                    const double to = left + (y + 1) * unit;
                    // A channel has text on both sides on every row.
                    bool bothSides = to - from >= least;
                    for (int r = i; bothSides && r <= j; ++r) {
                        bool before = false, after = false;
                        for (int index : rows.at(r).glyphs) {
                            const Glyph &glyph = glyphs.at(index);
                            if (isSpace(glyph.code))
                                continue;
                            before = before || glyph.box.right() <= from + unit;
                            after = after || glyph.box.left() >= to - unit;
                        }
                        bothSides = before && after;
                    }
                    if (bothSides)
                        channels.append({from, to});
                    x = y + 1;
                }
                if (!channels.isEmpty()) {
                    bestJ = j;
                    bestChannels = channels;
                    break;
                }
            }
            if (bestJ < 0) {
                ++i;
                continue;
            }
            for (int r = i; r <= bestJ; ++r) {
                Row &row = rows[r];
                QSet<int> breaks(row.breaks.begin(), row.breaks.end());
                for (const auto &channel : std::as_const(bestChannels)) {
                    for (int k = 0; k < row.glyphs.size(); ++k) {
                        const Glyph &glyph = glyphs.at(row.glyphs.at(k));
                        if (!isSpace(glyph.code) && glyph.box.left() >= channel.second - 0.5) {
                            breaks.insert(k);
                            break;
                        }
                    }
                }
                row.breaks = QVector<int>(breaks.begin(), breaks.end());
                std::sort(row.breaks.begin(), row.breaks.end());
            }
            i = bestJ + 1;
        }
        start = end + 1;
    }
}

// ---------------------------------------------------------------------------
// Runs
// ---------------------------------------------------------------------------

void appendText(QVector<LayoutRun> &runs, const QString &text, const RunStyle &style,
                const QString &uri, int targetPage)
{
    if (text.isEmpty())
        return;
    if (!runs.isEmpty() && runs.last().style == style && runs.last().uri == uri
        && runs.last().targetPage == targetPage && runs.last().field.isEmpty()) {
        runs.last().text += text;
        return;
    }
    LayoutRun run;
    run.text = text;
    run.style = style;
    run.uri = uri;
    run.targetPage = targetPage;
    runs.append(run);
}

// One row's glyphs [from, to) as runs, with the spaces a reader would type where
// the PDF only moved the pen.
// How far apart two glyphs are, whichever of them is on the left.
double gapBetween(const Glyph &a, const Glyph &b)
{
    return qMax(b.box.left() - a.box.right(), a.box.left() - b.box.right());
}

void appendRowRuns(QVector<LayoutRun> &runs, const QVector<Glyph> &glyphs, const Row &row, int from,
                   int to)
{
    // The order the characters are written in. A row is sorted left to right, which is reading
    // order except for Arabic and Hebrew: those are put back the right way round, a stretch at a
    // time (the spaces between two right-to-left words go with them). Left as sorted, an Arabic
    // line came out with its letters reversed and every one of them standing alone, because
    // joining runs in reading order.
    QVector<int> order;
    for (int k = from; k < to; ++k)
        order.append(k);
    for (int n = 0; n < order.size();) {
        if (!isRightToLeft(glyphs.at(row.glyphs.at(order.at(n))).code)) {
            ++n;
            continue;
        }
        int end = n;
        for (int m = n + 1; m < order.size(); ++m) {
            const char32_t code = glyphs.at(row.glyphs.at(order.at(m))).code;
            if (isRightToLeft(code))
                end = m;
            else if (!isSpace(code))
                break;
        }
        std::reverse(order.begin() + n, order.begin() + end + 1);
        n = end + 1;
    }

    QVector<LayoutRun> piece;
    int previous = -1;
    double inkLeft = 0, inkRight = 0;
    for (int n = 0; n < order.size(); ++n) {
        const int k = order.at(n);
        const Glyph &glyph = glyphs.at(row.glyphs.at(k));
        RunStyle style = glyph.style;
        style.rightToLeft = isRightToLeft(glyph.code);
        if (glyph.size < row.size * 0.8) {
            if (glyph.baseline < row.baseline - 0.15 * row.size)
                style.superscript = true;
            else if (glyph.baseline > row.baseline + 0.08 * row.size)
                style.subscript = true;
        }
        if (isSpace(glyph.code)) {
            // A space -- in the stream or inserted by PDFium -- is a space only where
            // the characters either side of it are actually apart. A real Chinese
            // book positions each Latin letter and digit of its body text by hand,
            // and PDFium's inserted spaces then turned "1983" into "1 9 8 3" and
            // "Verilog" into "V e r i l o g"; the ink of those letters is no further
            // apart than any other two letters of a word.
            int next = n + 1;
            while (next < order.size() && isSpace(glyphs.at(row.glyphs.at(order.at(next))).code))
                ++next;
            if (previous < 0 || next >= order.size() || piece.isEmpty() || piece.last().text.endsWith(QLatin1Char(' ')))
                continue;
            const Glyph &before = glyphs.at(row.glyphs.at(previous));
            const Glyph &after = glyphs.at(row.glyphs.at(order.at(next)));
            const double gap = gapBetween(before, after);
            const bool cjk = isCjk(before.code) && isCjk(after.code);
            if (gap >= (cjk ? 0.3 : 0.08) * row.size)
                appendText(piece, QStringLiteral(" "), piece.last().style, piece.last().uri, piece.last().targetPage);
            continue;
        }
        if (previous >= 0) {
            const Glyph &before = glyphs.at(row.glyphs.at(previous));
            // The spaces between words are PDFium's: the ones in the content stream
            // and the ones it inserts where the pen moved by a word's width. Its own
            // judgement of that is better than a gap read off ink boxes, which stop
            // short of a letter's width and put a space inside every other word. A
            // space is added here only for a gap no word spacing explains.
            const double gap = gapBetween(before, glyph);
            const bool cjkPair = isCjk(glyph.code) && isCjk(before.code);
            const double threshold = cjkPair ? 0.9 * row.size : 0.6 * row.size;
            if (gap > threshold && !piece.isEmpty() && !piece.last().text.endsWith(QLatin1Char(' ')))
                appendText(piece, QStringLiteral(" "), piece.last().style, piece.last().uri,
                           piece.last().targetPage);
        }
        appendText(piece, QString::fromUcs4(&glyph.code, 1), style, glyph.uri, glyph.targetPage);
        const QRectF pen = glyph.loose.isNull() ? glyph.box : glyph.loose;
        inkLeft = previous < 0 ? pen.left() : qMin(inkLeft, pen.left());
        inkRight = previous < 0 ? pen.right() : qMax(inkRight, pen.right());
        previous = k;
    }
    if (previous >= 0) {
        // As wide as the PDF set it: from the leftmost character's pen position to the
        // rightmost one's advance, less a little so Word's own rounding cannot tip it.
        const double target = inkRight - inkLeft - 0.3;
        double natural = 0;
        int count = 0;
        bool shaped = false;
        for (const LayoutRun &run : std::as_const(piece)) {
            shaped = shaped || run.style.rightToLeft;
            for (const char32_t code : run.text.toUcs4()) {
                natural += AdvanceWidths::instance().of(run.style, code);
                ++count;
            }
        }
        // Not for Arabic or Hebrew: a letter measured on its own is its isolated form, wider than
        // the joined one Word sets, and the "fit" squeezed a line until its word gaps vanished.
        if (!shaped && count > 0 && natural > 0 && target > 0) {
            const double limit = 0.25 * row.size;
            // Word takes it in twentieths of a point; rounded down, never wider.
            const double spacing = std::floor(qBound(-limit, (target - natural) / count, limit) * 20.0) / 20.0;
            if (std::fabs(spacing) >= 0.05) {
                for (LayoutRun &run : piece)
                    run.style.spacing = spacing;
            }
        }
    }
    for (const LayoutRun &run : std::as_const(piece))
        appendText(runs, run.text, run.style, run.uri, run.targetPage);
}

void trimRuns(QVector<LayoutRun> &runs)
{
    while (!runs.isEmpty() && runs.last().text.endsWith(QLatin1Char(' '))) {
        runs.last().text.chop(1);
        if (runs.last().text.isEmpty())
            runs.removeLast();
    }
    while (!runs.isEmpty() && runs.first().text.startsWith(QLatin1Char(' '))) {
        runs.first().text.remove(0, 1);
        if (runs.first().text.isEmpty())
            runs.removeFirst();
    }
}

// ---------------------------------------------------------------------------
// Paragraphs
// ---------------------------------------------------------------------------

bool endsSentence(const QString &text)
{
    static const QString enders = QStringLiteral("。．.！!？?；;：:");
    static const QString closers = QStringLiteral("”’」』》）)]】〕〉…\"'");
    for (int index = text.size() - 1; index >= 0; --index) {
        if (closers.contains(text.at(index)))
            continue;
        return enders.contains(text.at(index));
    }
    return false;
}

// Where a character's pen position and advance put it, rather than its ink: Word
// starts a line's first character at the indent and ends its last one's advance at
// the right indent, and a CJK glyph's ink stops short of its advance by most of a
// point either side -- enough, on a line fitted to the PDF's width, to wrap its last
// character.
QRectF penBox(const Glyph &glyph)
{
    return glyph.loose.isNull() ? glyph.box : QRectF(glyph.loose.left(), glyph.box.top(), glyph.loose.width(), glyph.box.height());
}

double rowLeft(const QVector<Glyph> &glyphs, const Row &row)
{
    for (int index : row.glyphs) {
        if (!isSpace(glyphs.at(index).code))
            return penBox(glyphs.at(index)).left();
    }
    return row.box.left();
}

double rowRight(const QVector<Glyph> &glyphs, const Row &row)
{
    for (int k = row.glyphs.size() - 1; k >= 0; --k) {
        if (!isSpace(glyphs.at(row.glyphs.at(k)).code))
            return penBox(glyphs.at(row.glyphs.at(k))).right();
    }
    return row.box.right();
}

QString rowText(const QVector<Glyph> &glyphs, const Row &row)
{
    QString text;
    for (int index : row.glyphs)
        text += QString::fromUcs4(&glyphs.at(index).code, 1);
    return text.trimmed();
}

// Whether a line starts a list item: a bullet, "1." / "1)" / "1、", "(1)", "a.",
// "（一）", "一、", "第三章". Such a line starts a paragraph of its own, however
// closely it follows the line above -- numbered exercises set at the body's line
// pitch were joined into one paragraph, and then rewrapped as one.
bool startsListItem(const QString &text)
{
    static const QRegularExpression marker(QStringLiteral(
        "^\\s*(?:[•·▪■◆◇●○◦‣⁃\\-–—*]\\s|"
        "\\(?\\d{1,3}[\\.\\)、．](?!\\d)|"
        "[\\(（]\\d{1,3}[\\)）]|"
        "[a-zA-Z][\\.\\)]\\s|"
        "[\\(（][一二三四五六七八九十]+[\\)）]|"
        "[一二三四五六七八九十]+、|"
        "第[一二三四五六七八九十百\\d]+[章节条部篇])"));
    return marker.match(text).hasMatch();
}

struct Region
{
    QRectF box;     // where blocks may be
    double left = 0;
    double right = 0;
    double top = 0;
    // Where the flow must end (the page's bottom margin); 0 for a cell, which grows.
    double bottom = 0;
};

// Rows grouped into paragraphs, each laid out against the region.
QVector<LayoutParagraph> paragraphsOf(const QVector<Glyph> &glyphs, const QVector<Row> &rows,
                                      const Region &region, double bodySize,
                                      const QVector<double> &barriers)
{
    QVector<LayoutParagraph> paragraphs;
    int i = 0;
    while (i < rows.size()) {
        const Row &first = rows.at(i);
        int j = i;
        if (first.breaks.isEmpty()) {
            double bodyLeft = rowLeft(glyphs, first);
            double maxRight = rowRight(glyphs, first);
            double pitch = 0;
            while (j + 1 < rows.size()) {
                const Row &a = rows.at(j);
                const Row &b = rows.at(j + 1);
                if (!b.breaks.isEmpty())
                    break;
                if (startsListItem(rowText(glyphs, b)))
                    break;
                // A table or a picture between them ends the paragraph.
                bool barrier = false;
                for (double y : barriers)
                    barrier = barrier || (y > a.baseline && y < b.baseline);
                if (barrier)
                    break;
                if (std::fabs(b.size - a.size) > 0.12 * qMax(a.size, b.size))
                    break;
                const double step = b.baseline - a.baseline;
                if (step <= 0 || step > 1.7 * b.size)
                    break;
                if (pitch > 0 && std::fabs(step - pitch) > 0.2 * pitch)
                    break;
                const double aLeft = rowLeft(glyphs, a);
                const double bLeft = rowLeft(glyphs, b);
                const double aRight = rowRight(glyphs, a);
                const double bRight = rowRight(glyphs, b);
                const bool firstPair = j == i;
                const double tolerance = 2.5;
                const bool sameLeft = std::fabs(bLeft - (firstPair ? bLeft : bodyLeft)) <= tolerance
                    && (firstPair ? true : true);
                // Centred lines stay together when their centres agree.
                const bool centred = std::fabs((aLeft + aRight) / 2 - (bLeft + bRight) / 2) <= 3.0
                    && std::fabs(aLeft - bLeft) > tolerance;
                if (firstPair) {
                    // The second line sets the body edge: a first line set further
                    // in is an indent, one set further out is a hanging line.
                    if (std::fabs(aLeft - bLeft) > 4 * a.size && !centred)
                        break;
                } else if (!sameLeft && !centred) {
                    break;
                }
                // A short line that finished its sentence finished its paragraph.
                const double width = qMax(maxRight, aRight) - qMin(bodyLeft, aLeft);
                if (aRight < qMax(maxRight, bRight) - 0.15 * width
                    && endsSentence(rowText(glyphs, a)))
                    break;
                if (firstPair)
                    bodyLeft = bLeft;
                maxRight = qMax(maxRight, bRight);
                pitch = step;
                ++j;
            }
        }

        LayoutParagraph paragraph;
        paragraph.lineCount = j - i + 1;
        paragraph.firstBaseline = rows.at(i).baseline;
        paragraph.lastBaseline = rows.at(j).baseline;
        QVector<double> sizes;
        QVector<double> lefts;
        QVector<double> rights;
        for (int r = i; r <= j; ++r) {
            paragraph.box = paragraph.box.isNull() ? rows.at(r).box : paragraph.box.united(rows.at(r).box);
            sizes.append(rows.at(r).size);
            lefts.append(rowLeft(glyphs, rows.at(r)));
            rights.append(rowRight(glyphs, rows.at(r)));
        }
        const double size = median(sizes);
        if (paragraph.lineCount > 1) {
            paragraph.lineSpacing = (paragraph.lastBaseline - paragraph.firstBaseline) / (paragraph.lineCount - 1);
        } else {
            // One line: as tall as the text needs, and never taller than the step to
            // the next line. A height taken from the font's whole ascent-to-descent
            // box was larger than the pitch of a list set one item per line, and every
            // item then pushed the rest of the page down by the difference.
            paragraph.lineSpacing = size * 1.17;
            if (j + 1 < rows.size()) {
                const double pitch = rows.at(j + 1).baseline - rows.at(j).baseline;
                if (pitch > size * 0.8)
                    paragraph.lineSpacing = qMin(paragraph.lineSpacing, pitch);
            }
        }

        // ---- runs ----
        for (int r = i; r <= j; ++r) {
            const Row &row = rows.at(r);
            if (r > i && !paragraph.runs.isEmpty()) {
                // The PDF's own line break, kept as a line break inside the
                // paragraph. A paragraph left to wrap by itself breaks wherever the
                // reader's glyph widths say, and one extra line anywhere on a page
                // pushes everything below it -- on a page filled to the bottom, onto a
                // page of its own (a 172-page book came back as 303). Kept breaks make
                // every line end where the PDF ends it; the paragraph is still one
                // paragraph, and a justified one is still justified to its margin.
                trimRuns(paragraph.runs);
                appendText(paragraph.runs, QStringLiteral("\n"), paragraph.runs.last().style, QString(), -1);
            }
            if (row.breaks.isEmpty()) {
                appendRowRuns(paragraph.runs, glyphs, row, 0, row.glyphs.size());
            } else {
                // A row in pieces: each piece after the first starts at a tab stop.
                QVector<int> cuts = row.breaks;
                cuts.prepend(0);
                cuts.append(row.glyphs.size());
                const double origin = rowLeft(glyphs, row);
                for (int p = 0; p + 1 < cuts.size(); ++p) {
                    if (p > 0) {
                        trimRuns(paragraph.runs);
                        appendText(paragraph.runs, QStringLiteral("\t"),
                                   paragraph.runs.isEmpty() ? glyphs.at(row.glyphs.at(cuts.at(p))).style
                                                            : paragraph.runs.last().style,
                                   QString(), -1);
                        double pieceLeft = 0;
                        for (int k = cuts.at(p); k < cuts.at(p + 1); ++k) {
                            if (!isSpace(glyphs.at(row.glyphs.at(k)).code)) {
                                pieceLeft = penBox(glyphs.at(row.glyphs.at(k))).left();
                                break;
                            }
                        }
                        paragraph.tabs.append({pieceLeft - origin, false});
                    }
                    appendRowRuns(paragraph.runs, glyphs, row, cuts.at(p), cuts.at(p + 1));
                }
            }
        }
        trimRuns(paragraph.runs);
        if (paragraph.runs.isEmpty()) {
            i = j + 1;
            continue;
        }

        // ---- alignment and indents ----
        const double bodyLeft = paragraph.lineCount > 1 ? lefts.at(1) : lefts.at(0);
        const double blockLeft = *std::min_element(lefts.begin(), lefts.end());
        const double blockRight = *std::max_element(rights.begin(), rights.end());
        const double width = blockRight - blockLeft;
        const double regionWidth = region.right - region.left;
        const double slack = qMax(3.0, width * 0.04);
        auto allNear = [](const QVector<double> &values, int count, double target, double tol) {
            for (int k = 0; k < count; ++k) {
                if (std::fabs(values.at(k) - target) > tol)
                    return false;
            }
            return true;
        };
        if (paragraph.lineCount > 1) {
            const bool leftsAgree = allNear(lefts.mid(1), lefts.size() - 1, bodyLeft, 2.5);
            const bool rightsAgree = allNear(rights, rights.size() - 1, blockRight, 2.5);
            QVector<double> centres;
            for (int k = 0; k < lefts.size(); ++k)
                centres.append((lefts.at(k) + rights.at(k)) / 2);
            const bool centresAgree = allNear(centres, centres.size(), median(centres), 3.0)
                && !allNear(lefts, lefts.size(), lefts.at(0), 2.5);
            if (centresAgree)
                paragraph.align = LayoutAlign::Center;
            else if (leftsAgree && rightsAgree)
                paragraph.align = LayoutAlign::Justify;
            else if (rightsAgree && allNear(rights, rights.size(), blockRight, 2.5) && !leftsAgree)
                paragraph.align = LayoutAlign::Right;
        } else if (paragraph.tabs.isEmpty()) {
            const double centre = (blockLeft + blockRight) / 2;
            const double regionCentre = (region.left + region.right) / 2;
            if (std::fabs(centre - regionCentre) <= 4.0 && blockLeft > region.left + 12)
                paragraph.align = LayoutAlign::Center;
            else if (std::fabs(blockRight - region.right) <= 3.0 && blockLeft > region.left + regionWidth * 0.35)
                paragraph.align = LayoutAlign::Right;
        }

        switch (paragraph.align) {
        case LayoutAlign::Left:
            paragraph.left = bodyLeft - region.left;
            paragraph.firstLine = lefts.at(0) - bodyLeft;
            paragraph.right = paragraph.lineCount > 1 ? qMax(0.0, region.right - (blockRight + slack)) : 0.0;
            break;
        case LayoutAlign::Justify:
            paragraph.left = bodyLeft - region.left;
            paragraph.firstLine = lefts.at(0) - bodyLeft;
            paragraph.right = region.right - blockRight;
            break;
        case LayoutAlign::Center: {
            const double centre = (blockLeft + blockRight) / 2;
            const double half = width / 2 + slack;
            paragraph.left = centre - half - region.left;
            paragraph.right = region.right - (centre + half);
            break;
        }
        case LayoutAlign::Right:
            paragraph.left = qMax(0.0, blockLeft - slack - region.left);
            paragraph.right = region.right - blockRight;
            break;
        }

        // ---- a heading, by size ----
        if (paragraph.lineCount <= 3 && bodySize > 0) {
            bool bold = true;
            for (const LayoutRun &run : std::as_const(paragraph.runs))
                bold = bold && (run.style.bold || run.text.trimmed().isEmpty());
            if (size >= bodySize * 1.6)
                paragraph.outlineLevel = 0;
            else if (size >= bodySize * 1.3)
                paragraph.outlineLevel = 1;
            else if (size >= bodySize * 1.12 && bold)
                paragraph.outlineLevel = 2;
        }
        paragraphs.append(paragraph);
        i = j + 1;
    }
    return paragraphs;
}

// ---------------------------------------------------------------------------
// A region: the page's body, or the inside of a table cell
// ---------------------------------------------------------------------------

struct Context
{
    Page *page = nullptr;
    const AnalysisOptions *options = nullptr;
    QVector<LayoutAnchor> *anchors = nullptr;
    int figures = 0;
};

QImage figureImage(const Context &context, const QRectF &region)
{
    if (!context.options->renderRegion)
        return {};
    return context.options->renderRegion(region, 200.0);
}

// The glyphs of the page that are not used yet and sit inside `box`.
QVector<int> glyphsIn(const Page &page, const QRectF &box)
{
    QVector<int> found;
    for (int i = 0; i < page.glyphs.size(); ++i) {
        const Glyph &glyph = page.glyphs.at(i);
        if (!glyph.used && box.contains(QPointF(glyph.centreX(), glyph.centreY())))
            found.append(i);
    }
    return found;
}

// A drawn rectangle, a rule or a panel, as a picture of its own colour anchored to
// the page behind the text.
void anchorShape(Context &context, const QRectF &box, const QColor &colour)
{
    if (box.width() < 0.1 || box.height() < 0.1)
        return;
    LayoutAnchor anchor;
    anchor.behindText = true;
    anchor.image.box = box;
    anchor.image.extension = QStringLiteral("png");
    anchor.image.bytes = pngOfColour(colour, QSize(qMax(1, int(std::ceil(box.width()))),
                                                    qMax(1, int(std::ceil(box.height())))));
    context.anchors->append(anchor);
}

QVector<LayoutBlock> layoutRegion(Context &context, const Region &region, QVector<int> glyphIndexes,
                                  bool allowTables);

// ---------------------------------------------------------------------------
// Two columns of running text
// ---------------------------------------------------------------------------

// The glyphs [from, to) of a row as a row of their own.
Row rowSlice(const QVector<Glyph> &glyphs, const Row &row, int from, int to)
{
    Row piece = row;
    piece.glyphs = row.glyphs.mid(from, to - from);
    piece.breaks.clear();
    for (int b : row.breaks) {
        if (b > from && b < to)
            piece.breaks.append(b - from);
    }
    piece.box = QRectF();
    for (int index : std::as_const(piece.glyphs)) {
        const Glyph &glyph = glyphs.at(index);
        const QRectF box = glyph.loose.isNull() ? glyph.box : glyph.loose;
        piece.box = piece.box.isNull() ? box : piece.box.united(box);
    }
    return piece;
}

struct ColumnZone
{
    double gutterLeft = 0;
    double gutterRight = 0;
    // Rows (by index into the row list) in the zone, top to bottom.
    int first = 0;
    int last = -1;
    QVector<Row> left;
    QVector<Row> right;
};

// A stretch of rows set in two columns: a gap down the middle that none of them crosses, with
// lines of running text on both sides of it. Read straight across, such a page came out with each
// left-column line and the right-column line beside it in one paragraph, separated by a tab --
// right on the screen, and sentences cut in half the moment anyone edited it.
//
// Running text, not a two-column table: on both sides most lines reach most of the way across
// their column (a key/value list, or code with comments beside it, has short left entries), and
// there are at least five lines a side.
QVector<ColumnZone> columnZones(const QVector<Glyph> &glyphs, const QVector<Row> &rows, const Region &region)
{
    QVector<ColumnZone> zones;
    const double width = region.right - region.left;
    if (rows.size() < 10 || width < 200)
        return zones;
    // Where each row has ink, as segments: glyphs closer than half their size are one segment.
    struct Segment
    {
        double from;
        double to;
    };
    QVector<QVector<Segment>> segments(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        for (int index : rows.at(r).glyphs) {
            const Glyph &glyph = glyphs.at(index);
            if (isSpace(glyph.code))
                continue;
            QVector<Segment> &mine = segments[r];
            if (!mine.isEmpty() && glyph.box.left() - mine.last().to < 0.5 * qMax(1.0, glyph.size))
                mine.last().to = qMax(mine.last().to, glyph.box.right());
            else
                mine.append({glyph.box.left(), glyph.box.right()});
        }
    }
    // The gutter: the stretch of the middle of the region that the fewest rows cross.
    const int bins = int(width);
    QVector<int> crossing(bins, 0);
    for (const QVector<Segment> &mine : std::as_const(segments)) {
        for (const Segment &s : mine) {
            const int from = qBound(0, int(s.from - region.left), bins - 1);
            const int to = qBound(0, int(std::ceil(s.to - region.left)), bins - 1);
            for (int x = from; x <= to; ++x)
                ++crossing[x];
        }
    }
    const int lo = int(width * 0.3);
    const int hi = int(width * 0.7);
    int best = INT_MAX;
    for (int x = lo; x <= hi; ++x)
        best = qMin(best, crossing.at(x));
    if (best > rows.size() / 4)
        return zones;
    // A few rows may cross it -- a heading across the page cuts the gutter into pieces narrower than
    // a word gap otherwise -- so the stretch is the longest one no more of them cross than that.
    const int allowed = qMax(best, int(rows.size()) / 6);
    int bestStart = -1, bestLength = 0;
    for (int x = lo; x <= hi;) {
        if (crossing.at(x) > allowed) {
            ++x;
            continue;
        }
        int end = x;
        while (end + 1 < bins && crossing.at(end + 1) <= allowed)
            ++end;
        int start = x;
        while (start - 1 >= 0 && crossing.at(start - 1) <= allowed)
            --start;
        // Inside that stretch, only the part within one row of its own minimum: the ragged ends of a
        // column's lines are crossed by few rows too, and a gutter grown into them cut the column.
        int least = INT_MAX;
        for (int k = start; k <= end; ++k)
            least = qMin(least, crossing.at(k));
        int tightStart = -1, tightLength = 0;
        for (int k = start; k <= end;) {
            if (crossing.at(k) > least + 1) {
                ++k;
                continue;
            }
            int stop = k;
            while (stop + 1 <= end && crossing.at(stop + 1) <= least + 1)
                ++stop;
            if (stop - k + 1 > tightLength) {
                tightLength = stop - k + 1;
                tightStart = k;
            }
            k = stop + 1;
        }
        if (tightLength > bestLength) {
            bestLength = tightLength;
            bestStart = tightStart;
        }
        x = end + 1;
    }
    if (bestLength < 10)
        return zones;
    const double gutterLeft = region.left + bestStart;
    const double gutterRight = region.left + bestStart + bestLength;

    // Runs of rows that do not cross it.
    const auto crosses = [&](int r) {
        for (const Segment &s : segments.at(r)) {
            if (s.to > gutterLeft + 1 && s.from < gutterRight - 1)
                return true;
        }
        return false;
    };
    int r = 0;
    while (r < rows.size()) {
        if (crosses(r)) {
            ++r;
            continue;
        }
        int end = r;
        while (end + 1 < rows.size() && !crosses(end + 1))
            ++end;
        ColumnZone zone;
        zone.gutterLeft = gutterLeft;
        zone.gutterRight = gutterRight;
        zone.first = r;
        zone.last = end;
        QVector<double> leftWidths, rightWidths;
        for (int k = r; k <= end; ++k) {
            const Row &row = rows.at(k);
            int cut = 0;
            while (cut < row.glyphs.size() && glyphs.at(row.glyphs.at(cut)).centreX() < gutterLeft)
                ++cut;
            if (cut > 0) {
                Row piece = rowSlice(glyphs, row, 0, cut);
                leftWidths.append(piece.box.right() - region.left);
                zone.left.append(piece);
            }
            if (cut < row.glyphs.size()) {
                Row piece = rowSlice(glyphs, row, cut, row.glyphs.size());
                rightWidths.append(piece.box.right() - gutterRight);
                zone.right.append(piece);
            }
        }
        const double leftColumn = gutterLeft - region.left;
        const double rightColumn = region.right - gutterRight;
        // And lines of prose have no gaps of their own: a two-part table (a symbol and its meaning
        // on each side, in a real book) has one in every line, and is a table, not two columns.
        int gapped = 0;
        for (const QVector<Row> *side : {&zone.left, &zone.right}) {
            for (const Row &piece : *side)
                gapped += piece.breaks.isEmpty() ? 0 : 1;
        }
        const int pieces = int(zone.left.size() + zone.right.size());
        if (leftWidths.size() >= 5 && rightWidths.size() >= 5 && median(leftWidths) >= 0.7 * leftColumn
            && median(rightWidths) >= 0.7 * rightColumn && gapped * 5 <= pieces)
            zones.append(zone);
        r = end + 1;
    }
    return zones;
}

void fillCell(Context &context, LayoutCell &cell)
{
    Page &page = *context.page;
    const QRectF inside = cell.box.adjusted(0.3, 0.3, -0.3, -0.3);
    QVector<int> glyphs = glyphsIn(page, inside);
    double contentLeft = 1e9, contentRight = -1e9, contentTop = 1e9;
    for (int index : std::as_const(glyphs)) {
        const Glyph &glyph = page.glyphs.at(index);
        if (isSpace(glyph.code))
            continue;
        contentLeft = qMin(contentLeft, glyph.box.left());
        contentRight = qMax(contentRight, glyph.box.right());
        contentTop = qMin(contentTop, glyph.loose.top());
    }
    for (const Picture &picture : std::as_const(page.pictures)) {
        if (!picture.used && inside.contains(picture.box.center())) {
            contentLeft = qMin(contentLeft, picture.box.left());
            contentRight = qMax(contentRight, picture.box.right());
            contentTop = qMin(contentTop, picture.box.top());
        }
    }
    if (contentLeft < 1e8) {
        // The left margin is exact -- it is where the text starts. The right one
        // gives back a few points: a cell's words must not wrap in a reader whose
        // glyphs are a hair wider than the PDF's (measured: "Q1" in a 25 pt cell
        // and "Red disc" in a 57 pt one both broke onto two lines when the right
        // margin was the PDF's own gap).
        cell.padLeft = qBound(0.0, contentLeft - cell.box.left(), 14.0);
        cell.padRight = qBound(0.0, cell.box.right() - contentRight - 4.0, 10.0);
        cell.padTop = 0;
    } else {
        cell.padLeft = cell.padRight = 2;
    }
    Region region;
    region.box = inside;
    region.left = cell.box.left() + cell.padLeft;
    region.right = cell.box.right() - cell.padRight;
    region.top = cell.box.top();
    cell.blocks = layoutRegion(context, region, glyphs, false);

    // A fill that covers the cell is its shading.
    for (Fill &fill : page.fills) {
        if (fill.used)
            continue;
        const QRectF overlap = fill.box.intersected(cell.box);
        if (overlap.width() * overlap.height() >= 0.8 * cell.box.width() * cell.box.height()) {
            cell.shading = fill.color;
            // A fill that is the cell (not a panel behind the whole table) is used up.
            if (fill.box.width() * fill.box.height() <= 1.3 * cell.box.width() * cell.box.height())
                fill.used = true;
        }
    }
}

QVector<LayoutBlock> layoutRegion(Context &context, const Region &region, QVector<int> glyphIndexes,
                                  bool allowTables)
{
    Page &page = *context.page;
    QVector<LayoutBlock> blocks;

    // ---- tables ----
    if (allowTables) {
        QVector<bool> used;
        const QVector<std::shared_ptr<LayoutTable>> tables = tablesFromRules(page.rules, &used);
        for (const std::shared_ptr<LayoutTable> &table : tables) {
            if (!region.box.adjusted(-2, -2, 2, 2).contains(table->box))
                continue;
            // A grid with nothing in it is a drawing (a chart's gridlines, a frame),
            // not a table: its lines stay graphics.
            bool content = false;
            for (int index : std::as_const(glyphIndexes)) {
                const Glyph &glyph = page.glyphs.at(index);
                content = content || (!glyph.used && !isSpace(glyph.code)
                                      && table->box.contains(QPointF(glyph.centreX(), glyph.centreY())));
            }
            for (const Picture &picture : std::as_const(page.pictures))
                content = content || (!picture.used && table->box.contains(picture.box.center()));
            if (!content)
                continue;
            // Mark the lines used: the ones that belong to this table's group.
            for (int k = 0; k < page.rules.size(); ++k) {
                if (used.at(k) && table->box.adjusted(-kGridSnap, -kGridSnap, kGridSnap, kGridSnap)
                                      .intersects(page.rules.at(k).horizontal
                                                      ? QRectF(page.rules.at(k).from, page.rules.at(k).position - 0.5,
                                                               page.rules.at(k).to - page.rules.at(k).from, 1)
                                                      : QRectF(page.rules.at(k).position - 0.5, page.rules.at(k).from,
                                                               1, page.rules.at(k).to - page.rules.at(k).from)))
                    page.ruleUsed[k] = true;
            }
            for (LayoutCell &cell : table->cells)
                fillCell(context, cell);
            LayoutBlock block;
            block.kind = LayoutBlock::Kind::Table;
            block.table = table;
            block.top = table->box.top();
            block.bottom = table->box.bottom();
            block.indent = table->box.left() - region.left;
            blocks.append(block);
        }
    }

    // ---- pictures and drawings ----
    // Everything drawn in the region that is not a table line is gathered into
    // clusters of things that touch: one photograph is one cluster; a chart made of
    // bars, axes, gridlines and labels is one cluster; a lone separator rule is one.
    struct Item
    {
        QRectF box;
        enum Kind
        {
            Art,
            PictureItem,
            FillItem,
            RuleItem,
        } kind;
        int index;
    };
    QVector<Item> items;
    for (int k = 0; k < page.art.size(); ++k) {
        if (!page.art.at(k).used && region.box.contains(page.art.at(k).box.center()))
            items.append({page.art.at(k).box, Item::Art, k});
    }
    for (int k = 0; k < page.pictures.size(); ++k) {
        if (!page.pictures.at(k).used && region.box.contains(page.pictures.at(k).box.center()))
            items.append({page.pictures.at(k).box, Item::PictureItem, k});
    }
    for (int k = 0; k < page.fills.size(); ++k) {
        const Fill &fill = page.fills.at(k);
        if (fill.used || !region.box.contains(fill.box.center()))
            continue;
        // A panel: a coloured box with words on it and nothing drawn inside. It
        // goes behind the words, and the words stay words.
        bool words = false;
        for (int index : std::as_const(glyphIndexes)) {
            const Glyph &glyph = page.glyphs.at(index);
            words = words || (!glyph.used && fill.box.contains(QPointF(glyph.centreX(), glyph.centreY())));
        }
        if (words) {
            anchorShape(context, fill.box, fill.color);
            page.fills[k].used = true;
            continue;
        }
        items.append({fill.box, Item::FillItem, k});
    }
    for (int k = 0; k < page.rules.size(); ++k) {
        if (page.ruleUsed.at(k))
            continue;
        const RuleLine &line = page.rules.at(k);
        const QRectF box = line.horizontal
            ? QRectF(line.from, line.position - line.width / 2, line.to - line.from, line.width)
            : QRectF(line.position - line.width / 2, line.from, line.width, line.to - line.from);
        if (!region.box.contains(box.center()))
            continue;
        // An underline: a thin line just below a run of text.
        if (line.horizontal && line.width <= 1.6) {
            QVector<int> above;
            double covered = 0;
            for (int index : std::as_const(glyphIndexes)) {
                Glyph &glyph = page.glyphs[index];
                if (glyph.used || isSpace(glyph.code))
                    continue;
                if (line.position >= glyph.baseline - 0.5 && line.position <= glyph.baseline + 0.3 * glyph.size
                    && glyph.box.center().x() >= line.from - 1 && glyph.box.center().x() <= line.to + 1) {
                    above.append(index);
                    covered += glyph.box.width();
                }
            }
            if (!above.isEmpty() && covered >= 0.5 * (line.to - line.from)) {
                for (int index : std::as_const(above))
                    page.glyphs[index].style.underline = true;
                // Spaces between underlined glyphs are underlined too.
                page.ruleUsed[k] = true;
                continue;
            }
        }
        items.append({box, Item::RuleItem, k});
    }

    QVector<int> parent(items.size());
    std::iota(parent.begin(), parent.end(), 0);
    const std::function<int(int)> root = [&](int i) {
        while (parent[i] != i)
            i = parent[i] = parent[parent[i]];
        return i;
    };
    for (int a = 0; a < items.size(); ++a) {
        for (int b = a + 1; b < items.size(); ++b) {
            if (items.at(a).kind == Item::RuleItem || items.at(b).kind == Item::RuleItem)
                continue;
            // Parts of one drawing are joined by arrows and connectors with gaps of a
            // few points between them; 8 keeps a block diagram in one piece.
            if (items.at(a).box.adjusted(-8, -8, 8, 8).intersects(items.at(b).box))
                parent[root(a)] = root(b);
        }
    }
    // Rules that touch each other are one drawing when there are enough of them: a
    // block diagram drawn as stroked boxes and connector lines is nothing but rules
    // (a real book's figure 1-1: five boxes, their arrows' shafts), and left as
    // separate shapes its labels stayed in the text, one merged into each body line
    // beside it. A frame around a note is four rules and stays shapes, its words
    // words. The arrowheads (small filled shapes) then join the drawing they touch.
    for (int a = 0; a < items.size(); ++a) {
        if (items.at(a).kind != Item::RuleItem)
            continue;
        for (int b = a + 1; b < items.size(); ++b) {
            if (items.at(b).kind == Item::RuleItem && items.at(a).box.adjusted(-2, -2, 2, 2).intersects(items.at(b).box))
                parent[root(a)] = root(b);
        }
    }
    {
        QHash<int, int> ruleCount;
        QHash<int, QRectF> ruleBox;
        for (int k = 0; k < items.size(); ++k) {
            if (items.at(k).kind != Item::RuleItem)
                continue;
            ++ruleCount[root(k)];
            QRectF &box = ruleBox[root(k)];
            box = box.isNull() ? items.at(k).box : box.united(items.at(k).box);
        }
        for (auto it = ruleCount.constBegin(); it != ruleCount.constEnd(); ++it) {
            if (it.value() < kDrawingRules)
                continue;
            const QRectF box = ruleBox.value(it.key()).adjusted(-3, -3, 3, 3);
            for (int k = 0; k < items.size(); ++k) {
                if (items.at(k).kind != Item::RuleItem && box.intersects(items.at(k).box))
                    parent[root(k)] = root(it.key());
            }
        }
    }
    // A rule joins a drawing only when most of it lies within the drawing -- a
    // chart's axis does; a page header's rule that a logo happens to sit on does
    // not, and joining it made the whole header band (its text included) one
    // picture, while that text was also written as the running header.
    {
        QHash<int, QRectF> boxes;
        for (int k = 0; k < items.size(); ++k) {
            if (items.at(k).kind == Item::RuleItem)
                continue;
            QRectF &box = boxes[root(k)];
            box = box.isNull() ? items.at(k).box : box.united(items.at(k).box);
        }
        for (int k = 0; k < items.size(); ++k) {
            if (items.at(k).kind != Item::RuleItem)
                continue;
            const QRectF &rule = items.at(k).box;
            const double length = qMax(rule.width(), rule.height());
            for (auto it = boxes.constBegin(); it != boxes.constEnd(); ++it) {
                const QRectF inside = rule.intersected(it.value().adjusted(-6, -6, 6, 6));
                if (!inside.isEmpty() && qMax(inside.width(), inside.height()) >= 0.6 * length) {
                    parent[root(k)] = root(it.key());
                    break;
                }
            }
        }
    }
    QHash<int, QVector<int>> clusters;
    for (int k = 0; k < items.size(); ++k)
        clusters[root(k)].append(k);

    struct Figure
    {
        QRectF box;
        QByteArray bytes;
        QString extension;
    };
    QVector<Figure> figures;
    for (const QVector<int> &cluster : std::as_const(clusters)) {
        QRectF box;
        bool onlyRules = true;
        bool onlyPicture = cluster.size() == 1 && items.at(cluster.first()).kind == Item::PictureItem;
        for (int k : cluster) {
            box = box.isNull() ? items.at(k).box : box.united(items.at(k).box);
            onlyRules = onlyRules && items.at(k).kind == Item::RuleItem;
        }
        if (onlyRules && cluster.size() < kDrawingRules) {
            for (int k : cluster) {
                const RuleLine &line = page.rules.at(items.at(k).index);
                anchorShape(context, items.at(k).box, line.color);
                page.ruleUsed[items.at(k).index] = true;
            }
            continue;
        }
        if (cluster.size() == 1 && items.at(cluster.first()).kind == Item::FillItem) {
            const Fill &fill = page.fills.at(items.at(cluster.first()).index);
            anchorShape(context, fill.box, fill.color);
            page.fills[items.at(cluster.first()).index].used = true;
            continue;
        }
        if (onlyPicture) {
            // Words set on a picture (a diagram's labels typed over white patches
            // on a scanned drawing, in a real book) belong to it: the picture's own
            // bytes would lose them and they would land in the body text instead.
            // A picture the size of the page is a background and keeps its words.
            const QRectF on = box.adjusted(1, 1, -1, -1);
            bool labelled = false;
            if (box.width() * box.height() < 0.5 * page.size.width() * page.size.height()) {
                for (int index : std::as_const(glyphIndexes)) {
                    const Glyph &glyph = page.glyphs.at(index);
                    labelled = labelled
                        || (!glyph.used && !isSpace(glyph.code) && on.contains(QPointF(glyph.centreX(), glyph.centreY())));
                }
            }
            onlyPicture = !labelled;
        }
        if (onlyPicture) {
            Picture &picture = page.pictures[items.at(cluster.first()).index];
            figures.append({picture.box, picture.bytes, picture.extension});
            picture.used = true;
            continue;
        }
        // A drawing: drawn on white as the page has it, labels and all. Its words
        // are in the picture, so they leave the text.
        const QRectF region_ = box.adjusted(-1.5, -1.5, 1.5, 1.5).intersected(QRectF(QPointF(0, 0), page.size));
        const QImage image = figureImage(context, region_);
        for (int k : cluster) {
            const Item &item = items.at(k);
            if (item.kind == Item::PictureItem)
                page.pictures[item.index].used = true;
            else if (item.kind == Item::FillItem)
                page.fills[item.index].used = true;
            else if (item.kind == Item::RuleItem)
                page.ruleUsed[item.index] = true;
            else
                page.art[item.index].used = true;
        }
        if (image.isNull())
            continue;
        Figure figure;
        figure.box = region_;
        figure.bytes = encodedPicture(image, &figure.extension);
        for (int index : std::as_const(glyphIndexes)) {
            Glyph &glyph = page.glyphs[index];
            if (!glyph.used && region_.contains(QPointF(glyph.centreX(), glyph.centreY())))
                glyph.used = true;
        }
        if (!figure.bytes.isEmpty())
            figures.append(figure);
    }

    // ---- the words ----
    QVector<int> free;
    for (int index : std::as_const(glyphIndexes)) {
        if (!page.glyphs.at(index).used)
            free.append(index);
    }
    QVector<Row> rows = rowsOf(page.glyphs, free);
    // Text in a table goes into its cells; what is left on the rows here is the
    // region's own text.
    splitAtChannels(rows, page.glyphs);

    // A figure with words beside it is anchored where it stands; one on its own
    // goes into the flow.
    QVector<QRectF> besideFigures;
    for (const Figure &figure : std::as_const(figures)) {
        bool beside = false;
        for (const Row &row : std::as_const(rows)) {
            const double overlap = qMin(row.box.bottom(), figure.box.bottom()) - qMax(row.box.top(), figure.box.top());
            if (overlap > 0.5 * row.box.height()
                && (row.box.right() <= figure.box.left() + 1 || row.box.left() >= figure.box.right() - 1))
                beside = true;
        }
        // One that starts above the region's top -- a logo in the page's header band,
        // above the top margin -- cannot go into the flow either: the flow starts at
        // the margin, so the picture would be put there and push the whole page down
        // by its own height (that, page after page, was most of a 172-page book
        // coming back as 318).
        const bool besideText = beside;
        if (figure.box.top() < region.top - 0.5)
            beside = true;
        // One that reaches below the bottom margin (a scan running to the page's
        // edge) does not fit the flow's page either; in it, it went to the next
        // page and took the rest of its own page with it.
        if (region.bottom > 0 && figure.box.bottom() > region.bottom + 0.5)
            beside = true;
        if (beside) {
            LayoutAnchor anchor;
            anchor.image.bytes = figure.bytes;
            anchor.image.extension = figure.extension;
            anchor.image.box = figure.box;
            context.anchors->append(anchor);
            if (besideText)
                besideFigures.append(figure.box);
            continue;
        }
        LayoutBlock block;
        block.kind = LayoutBlock::Kind::Image;
        block.image.bytes = figure.bytes;
        block.image.extension = figure.extension;
        block.image.box = figure.box;
        block.top = figure.box.top();
        block.bottom = figure.box.bottom();
        block.indent = figure.box.left() - region.left;
        blocks.append(block);
        ++context.figures;
    }

    // The words in an anchored figure's own column -- its caption, its labels set
    // beside it -- are placed where they are, in frames. In the flow they came
    // between two lines of the text beside the figure, a line of their own each,
    // and pushed the page's last lines onto the next.
    QVector<Row> sideRows;
    if (allowTables && !besideFigures.isEmpty()) {
        QVector<Row> flowRows;
        const auto inColumn = [&](const QRectF &box) {
            for (const QRectF &figure : std::as_const(besideFigures)) {
                const double reach = 3.0 * qMax(1.0, box.height());
                if (box.left() >= figure.left() - 4 && box.right() <= figure.right() + 4
                    && box.top() >= figure.top() - reach && box.bottom() <= figure.bottom() + reach)
                    return true;
            }
            return false;
        };
        // A piece at the end of a row of the text beside a figure that lies in the
        // figure's column (a caption on the same baseline) is cut off into its own
        // row first.
        QVector<Row> cut;
        for (const Row &row : std::as_const(rows)) {
            if (row.breaks.isEmpty()) {
                cut.append(row);
                continue;
            }
            int keep = row.glyphs.size();
            for (int b = row.breaks.size() - 1; b >= 0; --b) {
                QRectF piece;
                for (int k = row.breaks.at(b); k < keep; ++k) {
                    const QRectF box = page.glyphs.at(row.glyphs.at(k)).box;
                    piece = piece.isNull() ? box : piece.united(box);
                }
                if (piece.isNull() || !inColumn(piece))
                    break;
                keep = row.breaks.at(b);
            }
            if (keep == row.glyphs.size() || keep == 0) {
                cut.append(row);
                continue;
            }
            const auto part = [&](int from, int to) {
                Row piece = row;
                piece.glyphs = row.glyphs.mid(from, to - from);
                piece.breaks.clear();
                for (int b : row.breaks) {
                    if (b > from && b < to)
                        piece.breaks.append(b - from);
                }
                piece.box = QRectF();
                for (int index : std::as_const(piece.glyphs)) {
                    const Glyph &glyph = page.glyphs.at(index);
                    const QRectF box = glyph.loose.isNull() ? glyph.box : glyph.loose;
                    piece.box = piece.box.isNull() ? box : piece.box.united(box);
                }
                return piece;
            };
            cut.append(part(0, keep));
            cut.append(part(keep, row.glyphs.size()));
        }
        rows = cut;
        for (const Row &row : std::as_const(rows)) {
            bool side = false;
            for (const QRectF &figure : std::as_const(besideFigures)) {
                const double reach = 3.0 * row.size;
                side = side
                    || (row.box.left() >= figure.left() - 4 && row.box.right() <= figure.right() + 4
                        && row.box.top() >= figure.top() - reach && row.box.bottom() <= figure.bottom() + reach);
            }
            (side ? sideRows : flowRows).append(row);
        }
        rows = flowRows;
    }
    for (LayoutParagraph &paragraph : paragraphsOf(page.glyphs, sideRows, region, page.bodySize, {})) {
        paragraph.framed = true;
        LayoutBlock block;
        block.kind = LayoutBlock::Kind::Paragraph;
        block.top = paragraph.box.top();
        block.bottom = paragraph.box.bottom();
        block.paragraph = paragraph;
        blocks.append(block);
    }

    // Two columns of running text: each column laid out on its own, as one block.
    if (allowTables) {
        const QVector<ColumnZone> zones = columnZones(page.glyphs, rows, region);
        QVector<bool> taken(rows.size(), false);
        for (const ColumnZone &zone : zones) {
            double top = page.size.height(), bottom = 0;
            for (int k = zone.first; k <= zone.last; ++k) {
                top = qMin(top, rows.at(k).box.top());
                bottom = qMax(bottom, rows.at(k).box.bottom());
            }
            // A table or a picture in the flow beside or inside the zone would have to go into a
            // column too; those pages keep the plain layout.
            bool clear = true;
            for (const LayoutBlock &block : std::as_const(blocks)) {
                if (block.kind != LayoutBlock::Kind::Paragraph && block.bottom > top && block.top < bottom)
                    clear = false;
            }
            if (!clear)
                continue;
            auto columns = std::make_shared<LayoutColumns>();
            columns->leftWidth = zone.gutterLeft - region.left;
            columns->gap = zone.gutterRight - zone.gutterLeft;
            columns->rightWidth = region.right - zone.gutterRight;
            Region leftRegion = region;
            leftRegion.right = zone.gutterLeft;
            Region rightRegion = region;
            rightRegion.left = zone.gutterRight;
            const auto blocksOf = [&](const QVector<Row> &side, const Region &within) {
                QVector<LayoutBlock> made;
                for (LayoutParagraph &paragraph : paragraphsOf(page.glyphs, side, within, page.bodySize, {})) {
                    LayoutBlock block;
                    block.top = paragraph.box.top();
                    block.bottom = paragraph.box.bottom();
                    block.paragraph = paragraph;
                    made.append(block);
                }
                return made;
            };
            columns->left = blocksOf(zone.left, leftRegion);
            columns->right = blocksOf(zone.right, rightRegion);
            LayoutBlock block;
            block.kind = LayoutBlock::Kind::Columns;
            block.columns = columns;
            block.top = top;
            block.bottom = bottom;
            blocks.append(block);
            for (int k = zone.first; k <= zone.last; ++k)
                taken[k] = true;
        }
        QVector<Row> rest;
        for (int k = 0; k < rows.size(); ++k) {
            if (taken.at(k))
                continue;
            // The columns' gutter is a channel the rows outside them were split at too: a heading
            // across the page came out with a tab where its word gap met the gutter.
            Row row = rows.at(k);
            for (const ColumnZone &zone : zones) {
                for (qsizetype b = row.breaks.size() - 1; b >= 0; --b) {
                    const int at = row.breaks.at(b);
                    if (at <= 0 || at >= row.glyphs.size())
                        continue;
                    const double gapLeft = page.glyphs.at(row.glyphs.at(at - 1)).box.right();
                    const double gapRight = page.glyphs.at(row.glyphs.at(at)).box.left();
                    if (gapLeft < zone.gutterRight && gapRight > zone.gutterLeft)
                        row.breaks.removeAt(b);
                }
            }
            rest.append(row);
        }
        rows = rest;
    }

    QVector<double> barriers;
    for (const LayoutBlock &block : std::as_const(blocks)) {
        if (block.kind != LayoutBlock::Kind::Paragraph || !block.paragraph.framed)
            barriers.append((block.top + block.bottom) / 2);
    }
    for (LayoutParagraph &paragraph : paragraphsOf(page.glyphs, rows, region, page.bodySize, barriers)) {
        LayoutBlock block;
        block.kind = LayoutBlock::Kind::Paragraph;
        block.top = paragraph.box.top();
        block.bottom = paragraph.box.bottom();
        block.paragraph = paragraph;
        blocks.append(block);
    }
    for (int index : std::as_const(free))
        page.glyphs[index].used = true;

    std::stable_sort(blocks.begin(), blocks.end(), [](const LayoutBlock &a, const LayoutBlock &b) {
        return a.top < b.top;
    });

    // ---- vertical placement ----
    // Word lays a paragraph with exact line spacing L out as lines of height L whose
    // baseline sits a descent above each line's bottom. So the room above a
    // paragraph is whatever puts its first baseline where the PDF has it, measured
    // from where the previous block ended in Word -- not in the PDF, so that a
    // difference anywhere is not carried down the page.
    const std::function<double(QVector<LayoutBlock> &, double)> place = [&](QVector<LayoutBlock> &list,
                                                                          double flow) {
        for (LayoutBlock &block : list) {
            switch (block.kind) {
            case LayoutBlock::Kind::Paragraph: {
                LayoutParagraph &paragraph = block.paragraph;
                const double size = paragraph.runs.isEmpty() ? 11 : paragraph.runs.first().style.size;
                const double descent = 0.21 * size;
                const double top = paragraph.firstBaseline - (paragraph.lineSpacing - descent);
                if (paragraph.framed || (allowTables && paragraph.box.top() < region.top - 1.0)) {
                    // Its words above where the flow starts: placed where it is, out of the
                    // flow. Only the words count -- the line box of exact spacing reaches
                    // above them by half a line, and testing that framed the first lines of
                    // a page of code (two lines welded into one frame, the page's flow
                    // then starting at the heading under them).
                    paragraph.framed = true;
                    paragraph.spaceBefore = 0;
                    break;
                }
                paragraph.spaceBefore = qMax(0.0, top - flow);
                flow = flow + paragraph.spaceBefore + paragraph.lineCount * paragraph.lineSpacing;
                break;
            }
            case LayoutBlock::Kind::Image:
                block.spaceBefore = qMax(0.0, block.top - flow);
                flow = flow + block.spaceBefore + (block.bottom - block.top) + 0.4;
                break;
            case LayoutBlock::Kind::Table:
                block.spaceBefore = qMax(0.0, block.top - flow);
                flow = flow + block.spaceBefore + (block.bottom - block.top);
                break;
            case LayoutBlock::Kind::Columns: {
                // The section break in front of the columns is a 1-point paragraph and the room
                // above the columns goes to each column's first block: WPS lays the paragraph that
                // carries a section break out at no height at all, whatever its line spacing says,
                // so room given to it was room lost (measured: the columns came out 26 points high).
                // Each column then starts at the same height, and what follows starts under the
                // longer one. The column break and the closing section break are each a 1-point
                // paragraph at the end of a column.
                block.spaceBefore = 0;
                const double start = flow + 1.0;
                const double left = place(block.columns->left, start) + 1.0;
                const double right = place(block.columns->right, start) + 1.0;
                flow = qMax(left, right);
                break;
            }
            }
        }
        return flow;
    };
    place(blocks, region.top);
    return blocks;
}

LayoutParagraph bandParagraph(const Page &page, const QVector<int> &glyphIndexes, int pageNumber,
                              double regionLeft, double regionRight)
{
    LayoutParagraph paragraph;
    const QVector<Row> rows = rowsOf(page.glyphs, glyphIndexes);
    if (rows.isEmpty())
        return paragraph;
    const Row &row = rows.first();
    appendRowRuns(paragraph.runs, page.glyphs, row, 0, row.glyphs.size());
    trimRuns(paragraph.runs);
    paragraph.box = row.box;
    paragraph.firstBaseline = paragraph.lastBaseline = row.baseline;
    paragraph.lineSpacing = qMax(row.size * 1.17, row.box.height());
    // The page number becomes a field: the same number on this page, the right one
    // on any page the reader adds.
    if (pageNumber > 0) {
        const QString number = QString::number(pageNumber);
        for (int k = 0; k < paragraph.runs.size(); ++k) {
            LayoutRun &run = paragraph.runs[k];
            const int at = run.text.indexOf(QRegularExpression(QStringLiteral("(?<!\\d)%1(?!\\d)").arg(number)));
            if (at < 0)
                continue;
            LayoutRun before = run;
            before.text = run.text.left(at);
            LayoutRun field = run;
            field.text = number;
            field.field = QStringLiteral("PAGE");
            LayoutRun after = run;
            after.text = run.text.mid(at + number.size());
            QVector<LayoutRun> replacement;
            if (!before.text.isEmpty())
                replacement.append(before);
            replacement.append(field);
            if (!after.text.isEmpty())
                replacement.append(after);
            paragraph.runs.remove(k);
            for (int r = replacement.size() - 1; r >= 0; --r)
                paragraph.runs.insert(k, replacement.at(r));
            break;
        }
    }
    const double centre = (row.box.left() + row.box.right()) / 2;
    if (std::fabs(centre - (regionLeft + regionRight) / 2) <= 6)
        paragraph.align = LayoutAlign::Center;
    else if (std::fabs(row.box.right() - regionRight) <= 6 || row.box.left() > (regionLeft + regionRight) / 2)
        paragraph.align = LayoutAlign::Right;
    if (paragraph.align == LayoutAlign::Left)
        paragraph.left = row.box.left() - regionLeft;
    else if (paragraph.align == LayoutAlign::Right)
        paragraph.right = regionRight - row.box.right();
    return paragraph;
}

} // namespace

LayoutPage analysePage(const PageContent &content, const AnalysisOptions &options)
{
    LayoutPage result;
    result.size = content.size;
    Page page = prepared(content);
    if (options.bodySize > 0)
        page.bodySize = options.bodySize;

    // ---- a page with no words is a picture of itself ----
    int visible = 0;
    for (const Glyph &glyph : std::as_const(page.glyphs))
        visible += isSpace(glyph.code) ? 0 : 1;
    // A scanned page with a line or two of real text (a running header typed over
    // the scan) is a picture too: its scan is the page, and anchored as a picture
    // the size of the page, beside a header line in the flow, WPS drew it nowhere.
    double pictured = 0;
    const QRectF whole(QPointF(0, 0), page.size);
    for (const Picture &picture : std::as_const(page.pictures)) {
        const QRectF on = picture.box.intersected(whole);
        pictured += on.width() * on.height();
    }
    if (visible == 0 || (visible <= 40 && pictured >= 0.6 * page.size.width() * page.size.height())) {
        result.pictureOnly = true;
        return result;
    }
    // A drawing the size of the page -- a circuit schematic: hundreds of wires and
    // symbols, its words a few characters each, pin numbers and net names set
    // wherever the wire is. There is no text to flow; laid out as text and tables it
    // came apart (the wires became tables, the labels lines of their own). It is
    // the picture it is.
    if (page.rules.size() + page.art.size() >= 300) {
        QVector<int> all;
        for (int i = 0; i < page.glyphs.size(); ++i)
            all.append(i);
        // Words set apart, counted along each row: labels on one baseline across a
        // schematic are one row but dozens of pieces.
        int pieces = 0;
        for (const Row &row : rowsOf(page.glyphs, all)) {
            double previousRight = -1e9;
            for (int index : row.glyphs) {
                const Glyph &glyph = page.glyphs.at(index);
                if (isSpace(glyph.code))
                    continue;
                if (glyph.box.left() - previousRight > 1.5 * qMax(1.0, glyph.size))
                    ++pieces;
                previousRight = glyph.box.right();
            }
        }
        if (pieces > 0 && double(visible) / pieces < 8.0) {
            result.pictureOnly = true;
            return result;
        }
    }

    // ---- running header and footer ----
    const auto takeBand = [&](const QVector<QRectF> &rects, QVector<LayoutParagraph> *into) {
        for (const QRectF &rect : rects) {
            QVector<int> inside;
            for (int i = 0; i < page.glyphs.size(); ++i) {
                Glyph &glyph = page.glyphs[i];
                if (!glyph.used && rect.adjusted(-1, -1, 1, 1).contains(QPointF(glyph.centreX(), glyph.centreY()))) {
                    inside.append(i);
                    glyph.used = true;
                }
            }
            if (!inside.isEmpty())
                into->append(bandParagraph(page, inside, options.pageNumber, 0, page.size.width()));
        }
    };
    takeBand(options.headerRows, &result.header);
    takeBand(options.footerRows, &result.footer);
    // Each running line goes where it was, in a frame: in the header's own flow two
    // lines side by side at different heights (a book's "2  Verilog HDL" and its
    // "download" tag) stacked one under the other, the header grew past the top
    // margin and every body line on the page moved down 40 points.
    for (LayoutParagraph &paragraph : result.header)
        paragraph.framed = true;
    for (LayoutParagraph &paragraph : result.footer)
        paragraph.framed = true;

    // ---- margins: where the content is ----
    double left = page.size.width(), right = 0, top = page.size.height(), bottom = 0;
    const auto extend = [&](const QRectF &box) {
        left = qMin(left, box.left());
        right = qMax(right, box.right());
        top = qMin(top, box.top());
        bottom = qMax(bottom, box.bottom());
    };
    for (const Glyph &glyph : std::as_const(page.glyphs)) {
        if (!glyph.used && !isSpace(glyph.code))
            extend(glyph.loose.isNull() ? glyph.box : glyph.loose);
    }
    for (const Picture &picture : std::as_const(page.pictures))
        extend(picture.box);
    for (const RuleLine &line : std::as_const(page.rules)) {
        extend(line.horizontal ? QRectF(line.from, line.position, line.to - line.from, 0.1)
                               : QRectF(line.position, line.from, 0.1, line.to - line.from));
    }
    for (const Art &art : std::as_const(page.art))
        extend(art.box);
    for (const Fill &fill : std::as_const(page.fills))
        extend(fill.box);
    if (right <= left) {
        left = 72;
        right = page.size.width() - 72;
    }
    if (bottom <= top) {
        top = 72;
        bottom = page.size.height() - 72;
    }
    left = qBound(0.0, left, page.size.width() / 2);
    right = qBound(page.size.width() / 2, right, page.size.width());
    result.marginLeft = qMax(0.0, left - 0.5);
    result.marginRight = qMax(0.0, page.size.width() - right - 0.5);
    // The top margin is where the first block starts; analysis gives that block no
    // room above it. The bottom keeps some of the page's own space as slack: a line
    // that comes out a little taller in Word must not push the page's last block
    // onto a page of its own.
    result.marginTop = qMax(0.0, top - 2.0);
    const double bottomGap = qMax(0.0, page.size.height() - bottom);
    result.marginBottom = qBound(2.0, bottomGap * 0.4, 28.0);

    // Header and footer distances: where those rows sit. And the margins then hold
    // them, the way a document made in Word has its header inside the top margin
    // and its footer inside the bottom one -- a footer that reaches above the
    // bottom margin is laid out differently by different readers (WPS put this
    // one 20 points lower than the PDF had it). Done before the body is laid out,
    // because the body's first block is placed against the top margin.
    if (!result.header.isEmpty())
        result.headerDistance = page.size.height();
    for (const LayoutParagraph &paragraph : std::as_const(result.header)) {
        result.headerDistance = qMax(0.0, qMin(result.headerDistance, paragraph.box.top() - 1));
        result.marginTop = qMax(result.marginTop, paragraph.box.bottom() + 1);
    }
    if (!result.footer.isEmpty())
        result.footerDistance = page.size.height();
    for (const LayoutParagraph &paragraph : std::as_const(result.footer)) {
        result.footerDistance = qMax(0.0, qMin(result.footerDistance, page.size.height() - paragraph.box.bottom() - 1));
        result.marginBottom = qMax(result.marginBottom, page.size.height() - paragraph.box.top() + 1);
    }

    Context context;
    context.page = &page;
    context.options = &options;
    context.anchors = &result.anchors;
    Region region;
    region.box = QRectF(QPointF(0, 0), page.size);
    region.left = result.marginLeft;
    region.right = page.size.width() - result.marginRight;
    region.top = result.marginTop;
    region.bottom = page.size.height() - result.marginBottom;
    QVector<int> all;
    for (int i = 0; i < page.glyphs.size(); ++i) {
        if (!page.glyphs.at(i).used)
            all.append(i);
    }
    result.blocks = layoutRegion(context, region, all, true);
    // A scanned page whose only words are its running header: its body is one
    // picture the size of the page. Placed as that, it did not fit the page's text
    // area and WPS moved it on, page after page, and drew it on none of them (a real
    // book's 13-page appendix came back as 26 pages of headers). As a picture of the
    // whole page it is the page.
    {
        bool words = false;
        for (const LayoutBlock &block : std::as_const(result.blocks))
            words = words || block.kind != LayoutBlock::Kind::Image;
        if (!words && (!result.anchors.isEmpty() || !result.blocks.isEmpty())) {
            LayoutPage picture;
            picture.size = result.size;
            picture.pictureOnly = true;
            return picture;
        }
    }

    for (LayoutParagraph &paragraph : result.header) {
        if (paragraph.align == LayoutAlign::Left)
            paragraph.left -= result.marginLeft;
        else if (paragraph.align == LayoutAlign::Right)
            paragraph.right -= result.marginRight;
    }
    for (LayoutParagraph &paragraph : result.footer) {
        if (paragraph.align == LayoutAlign::Left)
            paragraph.left -= result.marginLeft;
        else if (paragraph.align == LayoutAlign::Right)
            paragraph.right -= result.marginRight;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Running header and footer
// ---------------------------------------------------------------------------

QVector<BandRow> bandRows(const PageContent &content)
{
    QVector<BandRow> found;
    Page page = prepared(content);
    QVector<int> all;
    for (int i = 0; i < page.glyphs.size(); ++i)
        all.append(i);
    const QVector<Row> rows = rowsOf(page.glyphs, all);
    const double height = page.size.height();
    static const QRegularExpression pageNumber(QStringLiteral(
        "^[\\s\\-–—·•\\[\\(]*(page|p\\.|第)?\\s*\\d+\\s*(页|/\\s*\\d+|of\\s*\\d+|共\\s*\\d+\\s*页)?[\\s\\-–—·•\\]\\)]*$"),
                                                 QRegularExpression::CaseInsensitiveOption);
    for (const Row &row : rows) {
        // The band a running header or footer can be in. Wide, because what makes a
        // row a running header is that it repeats in the same place on other pages
        // (a real book's header box ends at 11% of its page); a row that is only a
        // page number is held to the narrow band in headerFooterRows().
        const bool topBand = row.box.bottom() <= height * 0.15;
        const bool bottomBand = row.box.top() >= height * 0.85;
        if (!topBand && !bottomBand)
            continue;
        BandRow band;
        band.box = row.box;
        band.text = rowText(page.glyphs, row);
        band.folded = band.text;
        band.folded.replace(QRegularExpression(QStringLiteral("\\d+")), QStringLiteral("#"));
        band.top = topBand;
        band.pageNumber = pageNumber.match(band.text).hasMatch();
        if (!band.text.isEmpty())
            found.append(band);
    }
    return found;
}

RunningRows headerFooterRows(const QVector<QVector<BandRow>> &pages, const QVector<QSizeF> &sizes)
{
    RunningRows result;
    result.header.resize(pages.size());
    result.footer.resize(pages.size());
    // Twice elsewhere in a longer document: once is what a code listing's ". . ."
    // or "end" manages by chance, at the same height on another page.
    const int needed = pages.size() >= 6 ? 2 : 1;
    for (int p = 0; p < pages.size(); ++p) {
        QVector<QRectF> header, footer;
        // Only rows between the page's edge and its first row that does not run:
        // a running header or footer is the outermost thing on the page. A line of
        // body text that happened to repeat, with the page's last body line still
        // below it, made a 67-point footer and pushed that last line off its page.
        // A row seen once elsewhere (a chapter's title in the header of the only
        // other odd page of a short chapter) runs too when it is no further in than
        // a row that clearly runs.
        double headerLimit = sizes.value(p).height();
        double footerLimit = 0;
        double strongHeaderBottom = -1;
        double strongFooterTop = sizes.value(p).height() + 1;
        QVector<QRectF> weakHeader, weakFooter;
        for (const BandRow &row : pages.at(p)) {
            int repeats = 0;
            for (int q = 0; q < pages.size(); ++q) {
                if (q == p)
                    continue;
                for (const BandRow &other : pages.at(q)) {
                    if (other.top == row.top && other.folded == row.folded
                        && std::fabs(other.box.center().y() - row.box.center().y()) <= 4
                        && std::fabs(other.box.center().x() - row.box.center().x()) <= 30) {
                        ++repeats;
                        break;
                    }
                }
            }
            const double edge = row.top ? row.box.bottom() : sizes.value(p).height() - row.box.top();
            const bool strong = repeats >= needed || (row.pageNumber && edge <= sizes.value(p).height() * 0.09);
            const bool weak = !strong && repeats >= 1;
            if (!strong && !weak) {
                if (row.top)
                    headerLimit = qMin(headerLimit, row.box.top());
                else
                    footerLimit = qMax(footerLimit, row.box.bottom());
                continue;
            }
            if (strong) {
                (row.top ? header : footer).append(row.box);
                if (row.top)
                    strongHeaderBottom = qMax(strongHeaderBottom, row.box.bottom());
                else
                    strongFooterTop = qMin(strongFooterTop, row.box.top());
            } else {
                (row.top ? weakHeader : weakFooter).append(row.box);
            }
        }
        for (const QRectF &box : std::as_const(weakHeader)) {
            if (box.top() <= strongHeaderBottom)
                header.append(box);
            else
                headerLimit = qMin(headerLimit, box.top());
        }
        for (const QRectF &box : std::as_const(weakFooter)) {
            if (box.bottom() >= strongFooterTop)
                footer.append(box);
            else
                footerLimit = qMax(footerLimit, box.bottom());
        }
        for (const QRectF &box : std::as_const(header)) {
            if (box.bottom() <= headerLimit + 1)
                result.header[p].append(box);
        }
        for (const QRectF &box : std::as_const(footer)) {
            if (box.top() >= footerLimit - 1)
                result.footer[p].append(box);
        }
    }
    return result;
}
