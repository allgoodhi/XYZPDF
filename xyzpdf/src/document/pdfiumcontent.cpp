// pageContentOf(): the page's own objects, read out of PDFium. See pagecontent.h for
// why the Word export works from these rather than from pixels.
//
// Linked only into the targets that export (the program and the export tests): the
// two shell DLLs draw pages and never need this, which is the same reason the form
// input half lives in its own file (AGENTS.md, "表单代码分两半").
#include "pagecontent.h"

#include "pdfiumbackend.h"
#include "pdfiumbackend_p.h"

#include <QBuffer>
#include <QHash>
#include <QMutexLocker>
#include <QPainter>
#include <QRegularExpression>
#include <QTransform>

#include <fpdf_edit.h>

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

// User space to the displayed page, as an exact affine map. FPDF_PageToDevice is
// the library's own answer -- it knows the page's /Rotate and its crop box -- but it
// answers in whole device pixels, so it is asked on a grid a hundred times finer
// than a point and the three answers that pin down an affine map are divided back.
QTransform displayTransform(FPDF_PAGE page, QSizeF displayed)
{
    constexpr int kScale = 100;
    const int width = int(std::lround(displayed.width() * kScale));
    const int height = int(std::lround(displayed.height() * kScale));
    const auto map = [&](double x, double y) {
        int dx = 0;
        int dy = 0;
        FPDF_PageToDevice(page, 0, 0, width, height, 0, x, y, &dx, &dy);
        return QPointF(dx / double(kScale), dy / double(kScale));
    };
    constexpr double kSpan = 1000.0;
    const QPointF origin = map(0, 0);
    const QPointF alongX = (map(kSpan, 0) - origin) / kSpan;
    const QPointF alongY = (map(0, kSpan) - origin) / kSpan;
    return QTransform(alongX.x(), alongX.y(), alongY.x(), alongY.y(), origin.x(), origin.y());
}

QTransform transformOf(const FS_MATRIX &m)
{
    return QTransform(m.a, m.b, m.c, m.d, m.e, m.f);
}

QRectF mappedRect(const QTransform &transform, double left, double bottom, double right,
                  double top)
{
    return transform.mapRect(QRectF(QPointF(left, bottom), QPointF(right, top)).normalized());
}

QColor rgba(unsigned r, unsigned g, unsigned b, unsigned a)
{
    return QColor(int(r), int(g), int(b), int(a));
}

QString strippedFontName(const QByteArray &raw)
{
    QString name = QString::fromLatin1(raw).trimmed();
    // A subset font is named "ABCDEF+RealName": six capitals and a plus.
    if (name.size() > 7 && name.at(6) == QLatin1Char('+')) {
        bool subset = true;
        for (int i = 0; i < 6; ++i)
            subset = subset && name.at(i).isUpper();
        if (subset)
            name = name.mid(7);
    }
    return name;
}

QImage imageOfBitmap(FPDF_BITMAP bitmap)
{
    if (!bitmap)
        return {};
    const int width = FPDFBitmap_GetWidth(bitmap);
    const int height = FPDFBitmap_GetHeight(bitmap);
    const int stride = FPDFBitmap_GetStride(bitmap);
    const auto *buffer = static_cast<const uchar *>(FPDFBitmap_GetBuffer(bitmap));
    if (width <= 0 || height <= 0 || !buffer)
        return {};
    switch (FPDFBitmap_GetFormat(bitmap)) {
    case FPDFBitmap_Gray:
        return QImage(buffer, width, height, stride, QImage::Format_Grayscale8).copy();
    case FPDFBitmap_BGR:
        // Three bytes a pixel in blue-green-red order, which QImage reads as BGR888.
        return QImage(buffer, width, height, stride, QImage::Format_BGR888).copy();
    case FPDFBitmap_BGRx:
        return QImage(buffer, width, height, stride, QImage::Format_RGB32).copy();
    case FPDFBitmap_BGRA:
        return QImage(buffer, width, height, stride, QImage::Format_ARGB32).copy();
    default:
        return {};
    }
}

bool hasTransparency(const QImage &image)
{
    if (!image.hasAlphaChannel())
        return false;
    const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(line[x]) < 250)
                return true;
        }
    }
    return false;
}

// A picture as the bytes a document carries: JPEG for anything large and opaque,
// where PNG would be several times the size for no visible difference; PNG for a
// small picture (line art, an icon) and for anything with transparency.
QByteArray encoded(const QImage &image, QString *extension)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    const bool transparent = hasTransparency(image);
    const bool large = qint64(image.width()) * image.height() >= 160 * 160;
    if (!transparent && large && image.save(&buffer, "JPEG", 90)) {
        *extension = QStringLiteral("jpeg");
        return bytes;
    }
    bytes.clear();
    QBuffer second(&bytes);
    second.open(QIODevice::WriteOnly);
    if (!image.save(&second, "PNG"))
        return {};
    *extension = QStringLiteral("png");
    return bytes;
}

} // namespace

// Holds the backend's lock for the whole read: every FPDF_* call below is a call
// into the one library instance the renderer is also using (fpdfview.h: none of it
// is thread-safe).
struct PdfiumContentReader
{
    static PageContent read(PdfiumBackend &backend, int pageIndex, bool graphics);
    static bool userSpacePoint(PdfiumBackend &backend, int pageIndex, QPointF displayed,
                               QPointF *userSpace);
};

namespace {

struct Walk
{
    FPDF_DOCUMENT document = nullptr;
    FPDF_PAGE page = nullptr;
    QTransform toDisplay;
    QSizeF displayed;
    PageContent *content = nullptr;

    // A region of the page drawn on white at a given resolution: what a picture
    // turns into when its own pixels cannot be taken as they are (a mask, a
    // rotation, an encoding a Word document cannot hold).
    QImage renderRegion(const QRectF &region, double dpi) const
    {
        const double scale = dpi / 72.0;
        const QRect pixels = QRectF(region.left() * scale, region.top() * scale,
                                    region.width() * scale, region.height() * scale)
                                 .toAlignedRect();
        if (pixels.width() <= 0 || pixels.height() <= 0
            || qint64(pixels.width()) * pixels.height() > 40LL * 1024 * 1024)
            return {};
        QImage image(pixels.size(), QImage::Format_RGB32);
        image.fill(Qt::white);
        FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(image.width(), image.height(), FPDFBitmap_BGRx,
                                                 image.bits(), int(image.bytesPerLine()));
        if (!bitmap)
            return {};
        const int fullWidth = int(std::lround(displayed.width() * scale));
        const int fullHeight = int(std::lround(displayed.height() * scale));
        FPDF_RenderPageBitmap(bitmap, page, -pixels.left(), -pixels.top(), fullWidth, fullHeight,
                              0, FPDF_ANNOT | FPDF_PRINTING);
        FPDFBitmap_Destroy(bitmap);
        return image;
    }

    void image(FPDF_PAGEOBJECT object, const QTransform &total)
    {
        ContentImage picture;
        picture.box = total.mapRect(QRectF(0, 0, 1, 1));
        if (picture.box.width() < 0.5 || picture.box.height() < 0.5)
            return;
        // Only what is on the page: a picture placed half off the edge is cut
        // where the page is.
        picture.box = picture.box.intersected(QRectF(QPointF(0, 0), displayed));
        if (picture.box.isEmpty())
            return;

        const bool axisAligned = std::fabs(total.m12()) < 1e-6 && std::fabs(total.m21()) < 1e-6
            && total.m11() > 0 && total.m22() > 0;

        FPDF_IMAGEOBJ_METADATA meta{};
        FPDFImageObj_GetImageMetadata(object, page, &meta);
        picture.pixels = QSize(int(meta.width), int(meta.height));

        // The PDF's own JPEG, untouched, when that is exactly what is shown: one
        // DCT filter, a colour space a JPEG reader understands, no mask (the
        // rendered bitmap says whether there is one), drawn upright.
        const int filters = FPDFImageObj_GetImageFilterCount(object);
        QByteArray filter;
        if (filters == 1) {
            const unsigned long length = FPDFImageObj_GetImageFilter(object, 0, nullptr, 0);
            filter.resize(int(length));
            FPDFImageObj_GetImageFilter(object, 0, filter.data(), length);
            filter = filter.left(filter.indexOf('\0') < 0 ? filter.size() : filter.indexOf('\0'));
        }
        FPDF_BITMAP rendered = FPDFImageObj_GetRenderedBitmap(document, page, object);
        const QImage renderedImage = imageOfBitmap(rendered);
        if (rendered)
            FPDFBitmap_Destroy(rendered);
        const bool masked = hasTransparency(renderedImage);

        if (axisAligned && !masked && filter == "DCTDecode"
            && (meta.colorspace == FPDF_COLORSPACE_DEVICERGB
                || meta.colorspace == FPDF_COLORSPACE_DEVICEGRAY)) {
            const unsigned long length = FPDFImageObj_GetImageDataRaw(object, nullptr, 0);
            if (length > 0) {
                picture.bytes.resize(int(length));
                FPDFImageObj_GetImageDataRaw(object, picture.bytes.data(), length);
                picture.extension = QStringLiteral("jpeg");
                content->images.append(picture);
                return;
            }
        }

        // The decoded pixels at their own resolution, when nothing about the way
        // the picture is drawn changes them.
        if (axisAligned && !masked) {
            FPDF_BITMAP native = FPDFImageObj_GetBitmap(object);
            const QImage nativeImage = imageOfBitmap(native);
            if (native)
                FPDFBitmap_Destroy(native);
            if (!nativeImage.isNull()) {
                picture.bytes = encoded(nativeImage, &picture.extension);
                if (!picture.bytes.isEmpty()) {
                    content->images.append(picture);
                    return;
                }
            }
        }

        // Otherwise the page itself, drawn where the picture is: that bakes in the
        // mask, the rotation and any colour transform, at a resolution worth
        // printing.
        const QImage region = renderRegion(picture.box, 200.0);
        if (region.isNull())
            return;
        picture.bytes = encoded(region, &picture.extension);
        picture.pixels = region.size();
        if (!picture.bytes.isEmpty())
            content->images.append(picture);
    }

    void path(FPDF_PAGEOBJECT object, const QTransform &total)
    {
        ContentPath shape;
        int fillMode = FPDF_FILLMODE_NONE;
        FPDF_BOOL stroke = false;
        FPDFPath_GetDrawMode(object, &fillMode, &stroke);
        shape.fill = fillMode != FPDF_FILLMODE_NONE;
        shape.stroke = stroke;
        if (!shape.fill && !shape.stroke)
            return; // a clipping path, or one drawn with nothing
        unsigned r = 0, g = 0, b = 0, a = 255;
        if (shape.fill && FPDFPageObj_GetFillColor(object, &r, &g, &b, &a))
            shape.fillColor = rgba(r, g, b, a);
        if (shape.stroke && FPDFPageObj_GetStrokeColor(object, &r, &g, &b, &a))
            shape.strokeColor = rgba(r, g, b, a);
        float width = 0;
        if (shape.stroke && FPDFPageObj_GetStrokeWidth(object, &width)) {
            const double scale = std::sqrt(std::fabs(total.determinant()));
            shape.strokeWidth = qMax(0.0, double(width) * scale);
        }

        QPolygonF current;
        QPointF last;
        const int count = FPDFPath_CountSegments(object);
        for (int index = 0; index < count; ++index) {
            FPDF_PATHSEGMENT segment = FPDFPath_GetPathSegment(object, index);
            float x = 0, y = 0;
            if (!segment || !FPDFPathSegment_GetPoint(segment, &x, &y))
                continue;
            const QPointF point = total.map(QPointF(x, y));
            const int type = FPDFPathSegment_GetType(segment);
            if (type == FPDF_SEGMENT_MOVETO) {
                if (current.size() > 1)
                    shape.polygons.append(current);
                current = QPolygonF{point};
            } else if (type == FPDF_SEGMENT_BEZIERTO) {
                // Three segments make one curve: two control points and the end.
                // Flattened, because nothing downstream draws a curve -- it only
                // needs to know where the shape is and that it is not a rule.
                shape.curved = true;
                if (index + 2 < count) {
                    float x2 = 0, y2 = 0, x3 = 0, y3 = 0;
                    FPDFPathSegment_GetPoint(FPDFPath_GetPathSegment(object, index + 1), &x2, &y2);
                    FPDFPathSegment_GetPoint(FPDFPath_GetPathSegment(object, index + 2), &x3, &y3);
                    const QPointF c1 = point;
                    const QPointF c2 = total.map(QPointF(x2, y2));
                    const QPointF end = total.map(QPointF(x3, y3));
                    const QPointF start = current.isEmpty() ? last : current.last();
                    for (int step = 1; step <= 8; ++step) {
                        const double t = step / 8.0;
                        const double u = 1 - t;
                        current.append(u * u * u * start + 3 * u * u * t * c1 + 3 * u * t * t * c2
                                       + t * t * t * end);
                    }
                    if (FPDFPathSegment_GetClose(FPDFPath_GetPathSegment(object, index + 2))
                        && !current.isEmpty())
                        current.append(current.first());
                    index += 2;
                    last = end;
                    continue;
                }
                current.append(point);
            } else {
                if (current.isEmpty())
                    current.append(last);
                current.append(point);
            }
            if (FPDFPathSegment_GetClose(segment) && !current.isEmpty())
                current.append(current.first());
            last = point;
        }
        if (current.size() > 1)
            shape.polygons.append(current);
        if (shape.polygons.isEmpty())
            return;
        for (const QPolygonF &polygon : std::as_const(shape.polygons))
            shape.bounds = shape.bounds.isNull() ? polygon.boundingRect()
                                                 : shape.bounds.united(polygon.boundingRect());
        // A stroked line has width even when its geometry has none.
        const double half = shape.stroke ? shape.strokeWidth / 2 : 0;
        shape.bounds.adjust(-half, -half, half, half);
        content->paths.append(shape);
    }

    // Paint order of every object visited, and of the text objects by handle, so a
    // character can be told it lies under something drawn after it.
    int order = 0;
    QHash<FPDF_PAGEOBJECT, int> textOrder;

    void object(FPDF_PAGEOBJECT object, const QTransform &parent, int depth)
    {
        const int z = ++order;
        const qsizetype imagesBefore = content->images.size();
        const qsizetype pathsBefore = content->paths.size();
        visit(object, parent, depth, z);
        for (qsizetype i = imagesBefore; i < content->images.size(); ++i)
            content->images[i].z = z;
        for (qsizetype i = pathsBefore; i < content->paths.size(); ++i)
            content->paths[i].z = z;
    }

    void visit(FPDF_PAGEOBJECT object, const QTransform &parent, int depth, int z)
    {
        FS_MATRIX m{};
        if (!FPDFPageObj_GetMatrix(object, &m))
            m = FS_MATRIX{1, 0, 0, 1, 0, 0};
        switch (FPDFPageObj_GetType(object)) {
        case FPDF_PAGEOBJ_IMAGE:
            image(object, transformOf(m) * parent);
            break;
        case FPDF_PAGEOBJ_PATH:
            path(object, transformOf(m) * parent);
            break;
        case FPDF_PAGEOBJ_SHADING: {
            // A gradient: drawn, not ruled, so it goes in as part of a figure.
            float left = 0, bottom = 0, right = 0, top = 0;
            if (FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)) {
                ContentPath shape;
                shape.fill = true;
                shape.curved = true; // never a rule or a plain panel
                shape.fillColor = QColor(128, 128, 128);
                shape.bounds = mappedRect(parent, left, bottom, right, top);
                shape.polygons.append(QPolygonF(shape.bounds));
                content->paths.append(shape);
            }
            break;
        }
        case FPDF_PAGEOBJ_FORM:
            if (depth < 12) {
                const QTransform inner = transformOf(m) * parent;
                const int count = FPDFFormObj_CountObjects(object);
                for (int index = 0; index < count; ++index) {
                    if (FPDF_PAGEOBJECT child = FPDFFormObj_GetObject(object, index))
                        this->object(child, inner, depth + 1);
                }
            }
            break;
        case FPDF_PAGEOBJ_TEXT:
            textOrder.insert(object, z); // the text itself is read through the text page
            break;
        default:
            break;
        }
    }
};

} // namespace

PageContent PdfiumContentReader::read(PdfiumBackend &backend, int pageIndex, bool graphics)
{
    PageContent content;
    QMutexLocker locker(&pdfiumMutex());
    PdfiumBackend::Impl &impl = *backend.impl;
    if (!impl.document)
        return content;
    const FPDF_PAGE page = impl.holdPage(pageIndex, true);
    const FPDF_TEXTPAGE text = impl.heldText(pageIndex);
    if (!page)
        return content;

    content.size = QSizeF(FPDF_GetPageWidthF(page), FPDF_GetPageHeightF(page));
    const QTransform toDisplay = displayTransform(page, content.size);

    // ---- characters ----
    QVector<FPDF_PAGEOBJECT> owners; // per character, for its paint order below
    if (text) {
        const int count = FPDFText_CountChars(text);
        content.chars.reserve(qMax(0, count));
        // The font facts are the same for every character of one text object;
        // asked once per object rather than once per character.
        struct FontFacts
        {
            QString name;
            QString family;
            bool italic = false;
            bool invisible = false;
        };
        QHash<FPDF_PAGEOBJECT, FontFacts> facts;
        QByteArray nameBuffer(256, '\0');
        for (int index = 0; index < count; ++index) {
            const unsigned code = FPDFText_GetUnicode(text, index);
            if (code == 0)
                continue;
            ContentChar character;
            character.code = char32_t(code);
            character.generated = FPDFText_IsGenerated(text, index) == 1;

            FPDF_PAGEOBJECT owner = FPDFText_GetTextObject(text, index);
            auto known = facts.find(owner);
            if (known == facts.end()) {
                FontFacts fresh;
                int flags = 0;
                const unsigned long length =
                    FPDFText_GetFontInfo(text, index, nameBuffer.data(), nameBuffer.size(), &flags);
                if (length > 0 && length <= unsigned(nameBuffer.size()))
                    fresh.name = strippedFontName(nameBuffer.left(int(length) - 1));
                // Italic, believed when the name says so, or when the descriptor's
                // Italic flag (PDF 32000-1, table 123, bit 7) and a real slant agree.
                // Either one alone is not enough: a FounderType Song face in a real
                // Chinese book (FZSSJW--GB1-0) carries the flag and is upright, and the
                // whole body of that book came out italic when the flag was trusted.
                const bool flagged = (flags & (1 << 6)) != 0;
                bool slanted = false;
                if (owner) {
                    const FPDF_TEXT_RENDERMODE mode = FPDFTextObj_GetTextRenderMode(owner);
                    fresh.invisible = mode == FPDF_TEXTRENDERMODE_INVISIBLE
                        || mode == FPDF_TEXTRENDERMODE_CLIP;
                    if (FPDF_FONT font = FPDFTextObj_GetFont(owner)) {
                        int angle = 0;
                        slanted = FPDFFont_GetItalicAngle(font, &angle) && std::abs(angle) >= 4;
                        // Only an embedded font's family is its own: for one that is
                        // not, PDFium answers with the face it substitutes.
                        if (FPDFFont_GetIsEmbedded(font) == 1) {
                            QByteArray family(256, '\0');
                            const size_t got = FPDFFont_GetFamilyName(font, family.data(), size_t(family.size()));
                            if (got > 1 && got <= size_t(family.size()))
                                fresh.family = QString::fromUtf8(family.constData(), int(got) - 1).trimmed();
                        }
                    }
                }
                const QString lower = fresh.name.toLower();
                fresh.italic = lower.contains(QLatin1String("italic")) || lower.contains(QLatin1String("oblique"))
                    || (flagged && slanted);
                known = facts.insert(owner, fresh);
            }
            if (known->invisible && !character.generated) {
                ++content.invisibleChars;
                continue;
            }
            character.font = known->name;
            character.family = known->family;
            character.italic = known->italic;

            double left = 0, right = 0, bottom = 0, top = 0;
            if (FPDFText_GetCharBox(text, index, &left, &right, &bottom, &top))
                character.box = mappedRect(toDisplay, left, bottom, right, top);
            FS_RECTF loose{};
            if (FPDFText_GetLooseCharBox(text, index, &loose))
                character.loose = mappedRect(toDisplay, loose.left, loose.bottom, loose.right, loose.top);
            if (character.loose.isNull())
                character.loose = character.box;
            double originX = 0, originY = 0;
            if (FPDFText_GetCharOrigin(text, index, &originX, &originY))
                character.baseline = toDisplay.map(QPointF(originX, originY)).y();
            else
                character.baseline = character.box.bottom();
            // FPDFText_GetFontSize is the size the font was selected at (Tf), before
            // the text matrix and the page transform scale it: a page written at
            // 300 dpi selects 46 and draws at 11. The size a reader sees is that
            // times the scale of the character's own matrix (measured on a Qt PDF:
            // 46 x 0.24 = 11.04 for an 11 pt line).
            character.size = FPDFText_GetFontSize(text, index);
            FS_MATRIX charMatrix{};
            if (FPDFText_GetMatrix(text, index, &charMatrix)) {
                const double scale = std::sqrt(std::fabs(double(charMatrix.a) * charMatrix.d
                                                         - double(charMatrix.b) * charMatrix.c));
                const double display = std::sqrt(std::fabs(toDisplay.determinant()));
                if (scale > 0)
                    character.size *= scale * display;
            }
            const int weight = FPDFText_GetFontWeight(text, index);
            // A weight past 1000 is no weight (a real manual's descriptors say 4140
            // and 3696 for its regular text).
            character.weight = weight > 0 && weight <= 1000 ? weight : 400;
            const QString lower = character.font.toLower();
            // Qt's PDF writer names a font "Family-style-weight" ("Arial-0-700"), and
            // that weight is the one it was asked for; the descriptor it writes says
            // 520 for the same bold. The name is the better witness when it has one.
            static const QRegularExpression qtWeight(QStringLiteral("-\\d+-(\\d{3})$"));
            // Except that one subset can carry the name of the first weight it was used
            // at: a face with no bold of its own (SimSun) is named "SimSun-0-700" for
            // all its text once a bold heading used it first, and its descriptor then
            // says 230 -- a regular face, which is what every glyph in it is.
            const QRegularExpressionMatch named = qtWeight.match(character.font);
            if (named.hasMatch() && !(weight > 0 && weight <= 300))
                character.weight = named.captured(1).toInt();
            // The middle number is QFont::Style: 1 italic, 2 oblique ("TimesNewRoman-1-400"
            // is a caption set in italic, with nothing else in the name to say so).
            static const QRegularExpression qtStyle(QStringLiteral("-([12])-\\d{3}$"));
            if (qtStyle.match(character.font).hasMatch())
                character.italic = true;
            if (character.weight < 600
                && (lower.contains(QLatin1String("bold")) || lower.contains(QLatin1String("black"))
                    || lower.contains(QLatin1String("heavy")) || lower.contains(QLatin1String("semibold"))
                    || lower.contains(QLatin1String("demi"))))
                character.weight = 700;
            unsigned r = 0, g = 0, b = 0, a = 255;
            if (FPDFText_GetFillColor(text, index, &r, &g, &b, &a))
                character.color = rgba(r, g, b, a);
            // As the reader sees it: PDFium's angle is in the page's own space, and a
            // page marked /Rotate 90 shows its upright text running down the screen.
            character.angle = FPDFText_GetCharAngle(text, index) + FPDFPage_GetRotation(page) * kPi / 2;
            content.chars.append(character);
            owners.append(owner);
        }
    }

    // ---- pictures and drawn paths ----
    Walk walk;
    walk.document = impl.document;
    walk.page = page;
    walk.toDisplay = toDisplay;
    walk.displayed = content.size;
    walk.content = &content;
    const int objects = graphics ? FPDFPage_CountObjects(page) : 0;
    for (int index = 0; index < objects; ++index) {
        if (FPDF_PAGEOBJECT object = FPDFPage_GetObject(page, index))
            walk.object(object, toDisplay, 0);
    }
    for (qsizetype i = 0; i < content.chars.size() && i < owners.size(); ++i)
        content.chars[i].z = walk.textOrder.value(owners.at(i), 0);

    // ---- links ----
    int position = 0;
    FPDF_LINK link = nullptr;
    while (FPDFLink_Enumerate(page, &position, &link)) {
        FS_RECTF rect{};
        if (!FPDFLink_GetAnnotRect(link, &rect))
            continue;
        ContentLink target;
        target.box = mappedRect(toDisplay, rect.left, rect.bottom, rect.right, rect.top);
        FPDF_DEST dest = FPDFLink_GetDest(impl.document, link);
        if (FPDF_ACTION action = FPDFLink_GetAction(link)) {
            if (FPDFAction_GetType(action) == PDFACTION_URI) {
                const unsigned long length = FPDFAction_GetURIPath(impl.document, action, nullptr, 0);
                QByteArray uri(int(length), '\0');
                FPDFAction_GetURIPath(impl.document, action, uri.data(), length);
                target.uri = QString::fromUtf8(uri.left(uri.indexOf('\0') < 0 ? uri.size()
                                                                               : uri.indexOf('\0')));
            } else if (!dest) {
                dest = FPDFAction_GetDest(impl.document, action);
            }
        }
        if (dest)
            target.targetPage = FPDFDest_GetDestPageIndex(impl.document, dest);
        if (!target.uri.isEmpty() || target.targetPage >= 0)
            content.links.append(target);
    }

    content.ok = true;
    return content;
}

PageContent pageContentOf(Document &document, int page, bool graphics)
{
    auto *pdfium = dynamic_cast<PdfiumBackend *>(&document);
    if (!pdfium)
        return PageContent();
    return PdfiumContentReader::read(*pdfium, page, graphics);
}

bool PdfiumContentReader::userSpacePoint(PdfiumBackend &backend, int pageIndex,
                                         QPointF displayed, QPointF *userSpace)
{
    QMutexLocker locker(&pdfiumMutex());
    PdfiumBackend::Impl &impl = *backend.impl;
    if (!impl.document)
        return false;
    const FPDF_PAGE page = impl.holdPage(pageIndex, false);
    if (!page)
        return false;

    // The same hundred-times-a-point device grid displayTransform() asks the other
    // direction at. FPDF_DeviceToPage and FPDF_PageToDevice are one matrix and its
    // inverse inside the library, so asking on a fine grid makes the two round trips
    // agree to a hundredth of a point rather than to a whole one -- and the library is
    // the one applying /Rotate and the crop box, both ways.
    constexpr int kScale = 100;
    const int width = int(std::lround(FPDF_GetPageWidthF(page) * kScale));
    const int height = int(std::lround(FPDF_GetPageHeightF(page) * kScale));
    if (width <= 0 || height <= 0)
        return false;
    double x = 0;
    double y = 0;
    if (!FPDF_DeviceToPage(page, 0, 0, width, height, 0,
                           int(std::lround(displayed.x() * kScale)),
                           int(std::lround(displayed.y() * kScale)), &x, &y))
        return false;
    *userSpace = QPointF(x, y);
    return true;
}

bool userSpacePoint(Document &document, int page, QPointF displayed, QPointF *userSpace)
{
    auto *pdfium = dynamic_cast<PdfiumBackend *>(&document);
    if (!pdfium)
        return false;
    return PdfiumContentReader::userSpacePoint(*pdfium, page, displayed, userSpace);
}
