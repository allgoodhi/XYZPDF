// A ZIP file, written from scratch, because a .docx is one.
//
// ---- why this exists rather than a library ----
//
// The program has no third-party dependencies and this is not the feature that
// breaks that rule: a ZIP is a header, the data, a directory and a trailer, and
// all four fit in this file. Qt's own writer (QZipWriter) is in a private header,
// which is a promise about the build rather than about the format.
//
// ---- and why every entry is STORED rather than deflated ----
//
// Deflating would make the file a few times smaller, and it would mean either a
// zlib dependency or a dependence on the exact shape of what qCompress() returns
// -- which is a zlib stream wrapped in a length prefix that Qt does not promise
// to keep. For a document whose content is XML and text this is a few hundred
// kilobytes either way, and it is a file somebody is going to open in Word: the
// format that cannot be wrong beats the one that is smaller. Method 0 is part of
// the format and every unpacker reads it.
//
// There is no reader here. Nothing in the program opens a ZIP, and the tests
// that check what this wrote unpack it themselves -- which is the point of a
// test: a reader and a writer from the same hand agree with each other by
// construction and prove nothing.
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

class ZipWriter
{
public:
    // Entries are written in the order they are added, which is the order an
    // unpacker lists them in.
    void addFile(const QString &name, const QByteArray &content);

    int fileCount() const { return int(entries.size()); }

    QByteArray toBytes() const;

    // Writes the archive, and says why when it cannot.
    bool write(const QString &path, QString *error) const;

private:
    struct Entry
    {
        QByteArray name;
        QByteArray content;
        quint32 crc = 0;
        // Where this entry's local header starts, which is what the central
        // directory points at.
        quint32 offset = 0;
    };

    QVector<Entry> entries;
};
