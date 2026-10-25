// A presentation written from laid-out pages (layoutmodel.h): one slide per PDF
// page, every shape where the PDF had it.
//
// ---- why this is not the Word writer with different tags ----
//
// Word lays text out in a flow and the writer's job is to keep that flow from
// moving: line breaks, exact spacing, indents. A slide has no flow at all -- a
// shape is at a position and stays there -- so the numbers come straight from the
// page: a paragraph's box in points becomes the shape's offset and extent in EMU,
// its first baseline becomes the top of the text, and its character spacing is
// the same difference the Word writer measures per line. Pages are scaled only
// when the document's pages are not all the same size, and then proportionally,
// around the slide's centre.
//
// ---- tables ----
//
// A ruled table is written as one text box per cell, plus a rectangle per border
// and one per shaded cell, rather than as an `a:tbl`. The reason is the one this
// file exists for: an `a:tbl` has flow semantics -- PowerPoint grows a row to fit
// its text and can push the rows below it down -- and the PDF's grid would stop
// being the PDF's grid. Per-cell boxes also take the pictures and the inset the
// layout analysis measured inside a cell without re-deriving them, and the
// borders stay where the PDF drew them instead of where the reader's table style
// puts them. The cells are still separate shapes, so a reader can edit each one;
// the report says what they are.
//
// ---- what is written, and why each part is there ----
//
// The package is the smallest one PowerPoint and WPS accept: the content types,
// the presentation with `sldSz` (the first page's size) and `notesSz`, one slide
// per page with its own relationships, a slide master, one blank slide layout,
// and a theme. Drop any of the last three and the presentation is refused rather
// than opened blank.
#pragma once

#include "layoutmodel.h"

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QSizeF>
#include <QString>
#include <QVector>

class PptxWriter
{
public:
    // Appends a page as a slide. The first page fixes the slide size; every other
    // page is scaled to fit it and centred.
    void addSlide(const LayoutPage &page);

    int slideCount() const { return int(slides.size()); }
    // Pages that were not the first page's size and had to be scaled.
    int scaledSlideCount() const { return scaledSlides; }
    QSizeF slideSize() const { return slide; }

    // What was placed, for the finish report and the tests.
    int textBoxCount() const { return textBoxes; }
    int pictureCount() const { return pictures; }
    int shapeCount() const { return shapes; }
    int tableCount() const { return tables; }
    int tableCellCount() const { return cells; }

    // Every part of the package by name, as it will be written -- what a test
    // reads instead of unpacking an archive.
    QHash<QString, QByteArray> parts() const;

    QByteArray toBytes() const;
    bool save(const QString &path, QString *error) const;

private:
    struct Relationship
    {
        QString id;
        QString type;
        QString target;
        bool external = false;
    };
    struct Slide
    {
        QByteArray tree; // the spTree's children
        QVector<Relationship> rels; // pictures, links: this slide's own part references
        int number = 0;
        bool scaled = false;
    };
    struct Media
    {
        QString partName; // "media/image3.png"
        QByteArray bytes;
    };
    struct Placement
    {
        double scale = 1;
        double dx = 0;
        double dy = 0;
    };

    Placement placementFor(const QSizeF &page) const;
    qint64 emu(double points) const;
    QString relationship(const QString &type, const QString &target, bool external = false);
    QString mediaFor(const QByteArray &bytes, const QString &extension);
    QString hyperlinkFor(const LayoutRun &run);
    QByteArray runsXml(const QVector<LayoutRun> &runs, const Placement &place, int slideNumber,
                       bool leadWithTab);
    QByteArray paragraphXml(const LayoutParagraph &paragraph, const Placement &place, double regionLeft,
                            double regionWidth, int slideNumber, bool framed);
    void addParagraphBox(const LayoutParagraph &paragraph, const Placement &place, double regionLeft,
                         double regionWidth, bool framed);
    void addImage(const LayoutImage &image, const Placement &place);
    void addPicture(const LayoutImage &image, const Placement &place);
    QByteArray pictureXml(const QString &relationshipId, const LayoutImage &image, const Placement &place);
    void addRectangle(const QRectF &box, const QColor &colour, const Placement &place);
    void addTable(const LayoutTable &table, const Placement &place);
    void addCell(const LayoutCell &cell, const Placement &place);
    void addBlocks(const QVector<LayoutBlock> &blocks, const Placement &place, double regionLeft,
                   double regionWidth);
    void addAnchors(const QVector<LayoutAnchor> &anchors, const Placement &place);
    QByteArray slideXml(const Slide &slide) const;
    QByteArray masterXml() const;
    QByteArray layoutXml() const;
    QByteArray themeXml() const;
    QByteArray presentationXml() const;
    void addParts(class ZipWriter &zip) const;

    QVector<Slide> slides;
    QVector<Relationship> currentRelationships;
    QVector<Media> media;
    QHash<QByteArray, QString> mediaByHash;
    QSizeF slide;
    int scaledSlides = 0;
    int textBoxes = 0;
    int pictures = 0;
    int shapes = 0;
    int tables = 0;
    int cells = 0;
    // Per slide while it is being built.
    int nextRelationship = 2; // rId1 is the slide layout
    int nextShape = 2;
    int fieldId = 0;
};
