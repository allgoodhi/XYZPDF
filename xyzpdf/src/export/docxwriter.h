// A Word document written from laid-out pages (layoutmodel.h).
//
// ---- what is written, and why each piece is there ----
//
// One section per PDF page, with that page's size, orientation and margins, so the
// pages of the PDF stay pages of the document. Paragraphs carry exact line
// spacing, the room above them, their indents, alignment and tab stops: the
// numbers that put each line where the PDF had it, whatever font metrics the
// reader's Word has. Runs carry the family, size, weight, slant, underline, colour
// and position of the PDF's text, and links stay links. Tables are fixed-layout
// with the PDF's grid, merged cells, per-edge borders, shading and row heights.
// Pictures are inline where they stand alone and anchored to the page where text
// sits beside them; drawn rules and panels are anchored behind the text. Repeated
// header and footer lines go into Word's header and footer, with the page number
// as a field.
//
// Nothing here guesses: it writes what it is given. The guessing is the layout
// analysis's (pageanalysis.h), which is why the two are apart and tested apart.
#pragma once

#include "layoutmodel.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

class ZipWriter;

class DocxWriter
{
public:
    // Appends a page: a section of its own.
    void addPage(const LayoutPage &page);

    // The font and size most of the document is set in, for Word's own default.
    void setDefaultFont(const QString &family, double size);

    int pageCount() const { return int(sections.size()); }
    int paragraphCount() const { return paragraphs; }
    int imageCount() const { return images; }
    int tableCount() const { return tables; }

    // Every part of the package by name, as it will be written -- what a test
    // reads instead of unpacking an archive.
    QHash<QString, QByteArray> parts() const;

    QByteArray toBytes() const;
    bool save(const QString &path, QString *error) const;

private:
    struct Media
    {
        QString partName; // "media/image3.png"
        QString relationshipId;
        QByteArray bytes;
    };
    struct Relationship
    {
        QString id;
        QString type;
        QString target;
        bool external = false;
    };
    struct HeaderPart
    {
        QString partName; // "header2.xml"
        QString relationshipId;
        bool footer = false;
        QByteArray xml;
    };
    // What a page's section break says, kept until the whole document is known:
    // a page with no header of its own must point at an empty one, or Word gives
    // it the previous page's.
    struct Section
    {
        QByteArray body;
        LayoutPage geometry;
        QString headerId;
        QString footerId;
    };

    QByteArray paragraphXml(const LayoutParagraph &paragraph, const QByteArray &leading,
                            bool sectionBreak);
    QByteArray imageParagraphXml(const LayoutBlock &block, const QByteArray &leading,
                                 bool sectionBreak);
    QByteArray spacerXml(double height, const QByteArray &leading, bool sectionBreak);
    QByteArray tableXml(const LayoutTable &table, double indent);
    QByteArray blocksXml(const QVector<LayoutBlock> &blocks, QByteArray leading,
                         bool sectionBreak);
    QByteArray drawingXml(const LayoutImage &image, bool anchored, bool behind);
    QByteArray runsXml(const QVector<LayoutRun> &runs);
    QString mediaFor(const QByteArray &bytes, const QString &extension);
    QString relationship(const QString &type, const QString &target, bool external);
    QString headerFor(const QVector<LayoutParagraph> &lines, bool footer);
    QByteArray sectionXml(const Section &section) const;
    QByteArray documentXml() const;
    void addParts(ZipWriter &zip) const;

    QVector<Section> sections;
    QVector<Media> media;
    QHash<QByteArray, QString> mediaByHash;
    QVector<Relationship> relationships;
    QVector<HeaderPart> headers;
    QHash<QByteArray, QString> headerByContent;
    QString emptyHeaderId;
    QString emptyFooterId;
    QString defaultFamily = QStringLiteral("Calibri");
    double defaultSize = 11;
    int paragraphs = 0;
    int images = 0;
    int tables = 0;
    int drawingId = 0;
    int nextRelationship = 10;
};
